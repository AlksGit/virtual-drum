/*
 * Virtual Drum - USB receiver (XIAO nRF52840).
 *
 * Receives the sticks' ESB packets (PRX, one pipe per stick) and relays each of them to the host
 * over USB CDC as a frame of docs/PROTOCOL.md:
 *   A5 5A | len | rx_time_us u32 | rssi i8 | payload[len] | crc8
 *
 * ESB setup follows nrf/samples/esb/esb_prx (NCS v3.4.1). The radio event handler runs in
 * interrupt context: it only copies payloads into a message queue; a thread builds the frames.
 *
 * LEDs (active low, xiao_ble_common.dtsi): green blinks slowly while idle and flickers while
 * packets arrive; red lights when frames had to be dropped (host not reading).
 */

#include <string.h>

#include <esb.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "protocol.h"
#include "radio_config.h"
#include "usb_link.h"

BUILD_ASSERT(CONFIG_ESB_MAX_PAYLOAD_LENGTH >= VDRUM_ESB_MIN_PAYLOAD_CAPACITY,
	     "CONFIG_ESB_MAX_PAYLOAD_LENGTH too small for RAW packets");

struct rx_packet {
	uint32_t rx_time_us;
	int8_t rssi;
	uint8_t length;
	uint8_t data[RADIO_MAX_PAYLOAD];
};

/* ~0.15 s of both sticks streaming RAW packets (2 x 208 packets/s). */
K_MSGQ_DEFINE(rx_queue, sizeof(struct rx_packet), 64, 4);

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static atomic_t queue_overflows;

static uint32_t now_us(void)
{
	/* System timer (RTC, 32768 Hz on nRF52): ~30.5 us resolution, enough for phase 0. */
	return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64());
}

static void esb_event(struct esb_evt const *event)
{
	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	const uint32_t rx_time = now_us();
	struct esb_payload payload;

	while (esb_read_rx_payload(&payload) == 0) {
		if (payload.length == 0 || payload.length > RADIO_MAX_PAYLOAD) {
			continue;
		}

		struct rx_packet packet = {
			.rx_time_us = rx_time,
			.rssi = payload.rssi,
			.length = payload.length,
		};

		memcpy(packet.data, payload.data, payload.length);
		if (k_msgq_put(&rx_queue, &packet, K_NO_WAIT) != 0) {
			atomic_inc(&queue_overflows);
		}
	}
}

static int radio_init(void)
{
	uint8_t base_addr_0[4] = VDRUM_ESB_BASE_ADDR_0;
	uint8_t base_addr_1[4] = VDRUM_ESB_BASE_ADDR_1;
	uint8_t prefixes[VDRUM_ESB_PIPE_COUNT] = VDRUM_ESB_PREFIXES;
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.mode = ESB_MODE_PRX;
	config.event_handler = esb_event;
	config.selective_auto_ack = true;

	err = esb_init(&config);
	err = err ? err : esb_set_rf_channel(VDRUM_ESB_RF_CHANNEL);
	err = err ? err : esb_set_base_address_0(base_addr_0);
	err = err ? err : esb_set_base_address_1(base_addr_1);
	err = err ? err : esb_set_prefixes(prefixes, ARRAY_SIZE(prefixes));
	err = err ? err : esb_enable_pipes(BIT(VDRUM_ESB_PIPE_FOR_STICK(0)) | BIT(VDRUM_ESB_PIPE_FOR_STICK(1)));
	err = err ? err : esb_start_rx();
	return err;
}

/* Builds one USB frame around a radio payload. Returns the frame size. */
static size_t build_frame(const struct rx_packet *packet, uint8_t *frame)
{
	frame[0] = USB_SYNC0;
	frame[1] = USB_SYNC1;
	frame[2] = packet->length;
	sys_put_le32(packet->rx_time_us, &frame[3]);
	frame[7] = (uint8_t)packet->rssi;
	memcpy(&frame[USB_FRAME_HEADER_SIZE], packet->data, packet->length);

	const size_t crc_index = USB_FRAME_HEADER_SIZE + packet->length;

	/* The crc covers everything between the sync bytes and the crc itself. */
	frame[crc_index] = vdrum_crc8_update(CRC8_INIT, &frame[2], (uint32_t)(crc_index - 2));
	return crc_index + 1;
}

static void leds_init(void)
{
	if (gpio_is_ready_dt(&led_green)) {
		(void)gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	}
	if (gpio_is_ready_dt(&led_red)) {
		(void)gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	}
}

int main(void)
{
	static uint8_t frame[USB_FRAME_OVERHEAD + RADIO_MAX_PAYLOAD];
	struct rx_packet packet;
	uint32_t relayed = 0;

	leds_init();

	if (usb_link_init() != 0 || radio_init() != 0) {
		/* Fatal setup error: steady red. */
		(void)gpio_pin_set_dt(&led_red, 1);
		return 0;
	}

	while (1) {
		if (k_msgq_get(&rx_queue, &packet, K_MSEC(500)) != 0) {
			/* Idle heartbeat. */
			(void)gpio_pin_toggle_dt(&led_green);
			continue;
		}

		const size_t size = build_frame(&packet, frame);

		(void)usb_link_send(frame, size);

		if ((++relayed & 0x3F) == 0) {
			(void)gpio_pin_toggle_dt(&led_green);
		}
		(void)gpio_pin_set_dt(&led_red, usb_link_dropped() > 0 || atomic_get(&queue_overflows) > 0);
	}

	return 0;
}
