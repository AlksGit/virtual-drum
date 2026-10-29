/*
 * Virtual Drum - drumstick (XIAO nRF52840 Sense).
 *
 * Phase 0: streams the IMU at 1.66 kHz (accelerometer +-16 g, gyroscope +-2000 dps).
 * The radio link (RAW packets to the receiver) is added by TASK-007.
 *
 * LEDs (active low, zephyr/boards/seeed/xiao_ble/xiao_ble_common.dtsi): blue blinks while
 * samples flow (about twice per second), red stays on if the IMU could not be started or
 * samples are being lost.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "imu.h"

static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void on_batch(const struct imu_sample *samples, uint32_t count, uint32_t first_sample_time_us)
{
	ARG_UNUSED(samples);
	ARG_UNUSED(count);
	ARG_UNUSED(first_sample_time_us);
}

int main(void)
{
	(void)gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);

	if (imu_start(on_batch) != 0) {
		(void)gpio_pin_set_dt(&led_red, 1);
		return 0;
	}

	uint32_t last_samples = 0;

	while (1) {
		k_msleep(250);

		struct imu_stats stats;

		imu_get_stats(&stats);
		if (stats.samples != last_samples) {
			(void)gpio_pin_toggle_dt(&led_blue);
		}
		last_samples = stats.samples;
		(void)gpio_pin_set_dt(&led_red, stats.fifo_overruns > 0 || stats.bus_errors > 0);
	}

	return 0;
}
