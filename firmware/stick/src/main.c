/*
 * Virtual Drum - drumstick (XIAO nRF52840 Sense).
 *
 * Reads the IMU at 1.66 kHz (accelerometer +-16 g, gyroscope +-2000 dps), detects hits on board
 * and sends them as HIT packets over ESB, plus a STATUS packet every second. With
 * CONFIG_VDRUM_STREAM_RAW (phase 0 recorder), every sample also goes out in RAW packets of 8.
 * The stick id (0 = left, 1 = right) is CONFIG_VDRUM_STICK_ID.
 *
 * LEDs (active low, zephyr/boards/seeed/xiao_ble/xiao_ble_common.dtsi): blue blinks while
 * packets go out (about twice per second), red stays on if the IMU or the radio could not be
 * started, or while samples or packets are being lost.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "hit.h"
#include "imu.h"
#include "radio.h"

static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* No orientation fusion yet (phase 2): every hit is sent aimed at the snare of the host's default
 * kit (host/src/core/DrumKit.h: azimuth 8 deg, elevation -16 deg), Quat::fromAzimuthElevation
 * in Q14. */
static const int16_t snare_quat[4] = { 16185, -159, 2275, 1132 };

static void on_hit(uint32_t t_us, uint8_t velocity, uint8_t flags)
{
	radio_send_hit(t_us, velocity, snare_quat, flags);
}

static void on_samples(const struct imu_sample *samples, uint32_t count, uint32_t first_time_us)
{
	hit_process(samples, count, first_time_us);
	if (IS_ENABLED(CONFIG_VDRUM_STREAM_RAW)) {
		radio_send_samples(samples, count, first_time_us);
	}
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

		if (tick % 4 == 0) {
			/* Battery measurement needs the divider ratio (docs/HARDWARE.md): 0 = unknown. */
			imu_get_stats(&imu);
			radio_send_status(0, imu.fifo_overruns);
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
