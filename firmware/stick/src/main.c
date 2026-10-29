/*
 * Virtual Drum - baguette (XIAO nRF52840 Sense).
 * Phase 0 bring-up: blinks the blue LED to show that the build and the UF2 flash work.
 *
 * LEDs of the XIAO nRF52840 (active low): red P0.26 = led0, green P0.30 = led1,
 * blue P0.06 = led2. Source: zephyr/boards/seeed/xiao_ble/xiao_ble_common.dtsi (NCS v3.4.1).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "protocol.h"

#define BLINK_PERIOD_MS 500

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

int main(void)
{
	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
		return 0;
	}

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
