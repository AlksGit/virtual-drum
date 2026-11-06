/*
 * Virtual Drum - drumstick (XIAO nRF52840 Sense).
 *
 * Reads the IMU at 1.66 kHz (accelerometer +-16 g, gyroscope +-2000 dps), tracks the stick
 * orientation, detects hits on board and sends them as HIT packets over ESB, with ORIENT at
 * ~100 Hz and a STATUS packet every second. With
 * CONFIG_VDRUM_STREAM_RAW (phase 0 recorder), every sample also goes out in RAW packets of 8.
 * The stick id (0 = left, 1 = right) is CONFIG_VDRUM_STICK_ID.
 *
 * After CONFIG_VDRUM_SLEEP_AFTER_S at rest, the stick powers off (System OFF, radio and LEDs
 * off) with the IMU watching for motion; picking it up wakes it through a reset: a normal boot.
 * The rest test is looser than the fusion's (6 dps and 0.05 g on every sample): lying on a
 * table, the left stick broke that one after 63 s at most (gyro noise) and the right one came
 * within 0.001 g of it (accelerometer reading 1.03 g), session_20261104_143641. Here motion is
 * MOVING_RUN consecutive samples beyond 20 dps from the bias or 0.15 g from 1 g: that session
 * then rests 363 s (one knock) and 600 s, and play sessions never rest more than 21 s.
 *
 * LEDs (active low, zephyr/boards/seeed/xiao_ble/xiao_ble_common.dtsi): blue blinks while
 * packets go out (about twice per second), red stays on if the IMU or the radio could not be
 * started, or while samples or packets are being lost.
 */

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

#include "fusion.h"
#include "hit.h"
#include "imu.h"
#include "radio.h"

static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* One ORIENT packet every 16 samples: 104 Hz at 1.66 kHz. */
#define ORIENT_EVERY_SAMPLES 16u

#define SLEEP_AFTER_SAMPLES ((uint32_t)CONFIG_VDRUM_SLEEP_AFTER_S * IMU_ODR_HZ)

/* Rest test (see the top). LSM6DS3TR-C: 70 mdps/LSB at +-2000 dps, 0.488 mg/LSB at +-16 g
 * (docs/HARDWARE.md).
 */
#define MOVING_GYRO_COUNTS 286   /* 20 dps */
#define ONE_G_COUNTS 2049
#define MOVING_ACCEL_COUNTS 307  /* 0.15 g */
#define MOVING_RUN 3u

/* Samples since the stick last moved: IMU thread writes, main thread reads (one word). */
static volatile uint32_t rest_samples;

static void on_hit(uint32_t t_us, uint8_t velocity, uint8_t flags)
{
	int16_t quat[4];

	fusion_get_quat_q14(quat);
	radio_send_hit(t_us, velocity, quat, flags);
}

static bool sample_moves(const struct imu_sample *s, const int16_t bias[3])
{
	const int32_t g[3] = { s->gx - bias[0], s->gy - bias[1], s->gz - bias[2] };
	/* Up to 3 x 2^30 at full scale: unsigned. */
	const uint32_t norm_sq = (uint32_t)(s->ax * s->ax) + (uint32_t)(s->ay * s->ay) + (uint32_t)(s->az * s->az);
	const uint32_t low = ONE_G_COUNTS - MOVING_ACCEL_COUNTS, high = ONE_G_COUNTS + MOVING_ACCEL_COUNTS;

	for (int i = 0; i < 3; i++) {
		if (g[i] > MOVING_GYRO_COUNTS || g[i] < -MOVING_GYRO_COUNTS) {
			return true;
		}
	}
	return norm_sq < low * low || norm_sq > high * high;
}

static void track_rest(const struct imu_sample *samples, uint32_t count)
{
	static uint32_t moving_run;
	int16_t bias[3];

	fusion_get_bias(bias);
	for (uint32_t i = 0; i < count; i++) {
		moving_run = sample_moves(&samples[i], bias) ? moving_run + 1 : 0;
		rest_samples = moving_run >= MOVING_RUN ? 0 : rest_samples + 1;
	}
}

static void on_samples(const struct imu_sample *samples, uint32_t count, uint32_t first_time_us)
{
	static uint32_t orient_countdown = ORIENT_EVERY_SAMPLES;

	for (uint32_t i = 0; i < count; i++) {
		const uint32_t t_us = first_time_us + i * IMU_SAMPLE_PERIOD_US;

		fusion_process(&samples[i]);
		hit_process(&samples[i], 1, t_us);
		if (--orient_countdown == 0) {
			int16_t quat[4];

			orient_countdown = ORIENT_EVERY_SAMPLES;
			fusion_get_quat_q14(quat);
			radio_send_orient(t_us, quat);
		}
	}
	track_rest(samples, count);
	if (IS_ENABLED(CONFIG_VDRUM_STREAM_RAW)) {
		radio_send_samples(samples, count, first_time_us);
	}
}

static void power_off(void)
{
	(void)gpio_pin_set_dt(&led_blue, 0);
	(void)gpio_pin_set_dt(&led_red, 0); /* outputs keep their level through System OFF */
	if (imu_prepare_sleep() != 0) {
		/* No wake-up source, and streaming has stopped: restart instead. */
		sys_reboot(SYS_REBOOT_COLD);
	}
	sys_poweroff();
}

int main(void)
{
	(void)gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);

	hit_init(on_hit);
	if (radio_start(CONFIG_VDRUM_STICK_ID) != 0 || imu_start(on_samples) != 0) {
		(void)gpio_pin_set_dt(&led_red, 1);
		return 0;
	}

	struct imu_stats imu = { 0 };
	struct radio_stats radio = { 0 };
	uint32_t last_sent = 0, last_lost = 0;

	for (uint32_t tick = 0;; tick++) {
		k_msleep(250);

		if (CONFIG_VDRUM_SLEEP_AFTER_S > 0 && rest_samples > SLEEP_AFTER_SAMPLES) {
			power_off();
		}

		if (tick % 4 == 0) {
			/* Battery measurement needs the divider ratio (docs/HARDWARE.md): 0 = unknown. */
			int16_t bias[3];

			imu_get_stats(&imu);
			fusion_get_bias(bias);
			radio_send_status(0, imu.fifo_overruns, bias);
		}

		imu_get_stats(&imu);
		radio_get_stats(&radio);

		if (radio.packets_sent != last_sent) {
			(void)gpio_pin_toggle_dt(&led_blue);
		}
		last_sent = radio.packets_sent;

		const uint32_t lost = imu.fifo_overruns + imu.bus_errors + radio.packets_failed + radio.queue_full;

		(void)gpio_pin_set_dt(&led_red, lost != last_lost);
		last_lost = lost;
	}

	return 0;
}
