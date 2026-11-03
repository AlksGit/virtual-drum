/*
 * USB CDC ACM link, using Zephyr's USB device stack ("device_next").
 * Device setup follows zephyr/samples/subsys/usb/common/sample_usbd_init.c (NCS v3.4.1) and the
 * interrupt-driven TX path follows zephyr/samples/subsys/usb/cdc_acm/src/main.c.
 */

#include "usb_link.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usbd.h>

#include "protocol.h"

/* ~70 full RAW frames: about 0.15 s of both sticks streaming RAW data. */
#define TX_BUFFER_SIZE 8192

/* Host commands are rare and small (USB_CMD_FRAME_OVERHEAD + a pkt_cmd). */
#define RX_BUFFER_SIZE 256
#define CMD_MAX_PAYLOAD 32

/*
 * The board already defines one CDC ACM function (board_cdc_acm_uart, from
 * boards/common/usb/cdc_acm_serial.dtsi included by xiao_ble_common.dtsi): use it, so that the
 * device exposes a single serial port.
 */
static const struct device *const cdc_dev = DEVICE_DT_GET(DT_NODELABEL(board_cdc_acm_uart));

RING_BUF_DECLARE(tx_ring, TX_BUFFER_SIZE);
RING_BUF_DECLARE(rx_ring, RX_BUFFER_SIZE);
static struct k_spinlock rx_lock;
static usb_link_command_cb command_cb;
static void rx_work_handler(struct k_work *work);
static K_WORK_DEFINE(rx_work, rx_work_handler);
static struct k_spinlock tx_lock;
static uint32_t dropped_blocks;
static atomic_t sessions;
static atomic_t host_open; /* DTR raised: a host application has the port open */

USBD_DEVICE_DEFINE(vdrum_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), USB_RECEIVER_VID, USB_RECEIVER_PID);

USBD_DESC_LANG_DEFINE(vdrum_lang);
USBD_DESC_MANUFACTURER_DEFINE(vdrum_manufacturer, "Virtual Drum");
USBD_DESC_PRODUCT_DEFINE(vdrum_product, USB_RECEIVER_PRODUCT);
USBD_DESC_CONFIG_DEFINE(vdrum_fs_config_desc, "FS Configuration");

/* Bus powered, 100 mA. */
USBD_CONFIGURATION_DEFINE(vdrum_fs_config, 0, 100, &vdrum_fs_config_desc);

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			/* Host commands: parsed in the system work queue. Bytes that do not fit are
			 * dropped; the parser resynchronises on the next frame.
			 */
			uint8_t scratch[16];
			const int len = uart_fifo_read(dev, scratch, sizeof(scratch));

			if (len > 0) {
				k_spinlock_key_t key = k_spin_lock(&rx_lock);

				(void)ring_buf_put(&rx_ring, scratch, (uint32_t)len);
				k_spin_unlock(&rx_lock, key);
				(void)k_work_submit(&rx_work);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t *chunk;
			k_spinlock_key_t key = k_spin_lock(&tx_lock);
			uint32_t len = ring_buf_get_claim(&tx_ring, &chunk, 64);

			if (len == 0) {
				uart_irq_tx_disable(dev);
				k_spin_unlock(&tx_lock, key);
				continue;
			}

			int sent = uart_fifo_fill(dev, chunk, (int)len);

			(void)ring_buf_get_finish(&tx_ring, sent > 0 ? (uint32_t)sent : 0);
			k_spin_unlock(&tx_lock, key);
		}
	}
}

/*
 * Host -> receiver command frames (docs/PROTOCOL.md):
 *   A5 5A | len | payload[len] | crc8 (over len and payload)
 * Any error (bad length, bad crc) drops one byte and resynchronises on the next sync bytes.
 */
static void rx_work_handler(struct k_work *work)
{
	static uint8_t frame[USB_CMD_FRAME_OVERHEAD + CMD_MAX_PAYLOAD];
	static uint32_t fill;

	ARG_UNUSED(work);

	while (true) {
		k_spinlock_key_t key = k_spin_lock(&rx_lock);

		fill += ring_buf_get(&rx_ring, &frame[fill], sizeof(frame) - fill);
		const bool more = !ring_buf_is_empty(&rx_ring);

		k_spin_unlock(&rx_lock, key);

		/* Drop bytes until the buffer starts with the sync sequence. */
		uint32_t start = 0;

		while (start < fill && !(frame[start] == USB_SYNC0 &&
					 (start + 1 == fill || frame[start + 1] == USB_SYNC1))) {
			start++;
		}
		if (start > 0) {
			memmove(frame, &frame[start], fill - start);
			fill -= start;
		}
		if (fill < 3) {
			if (!more) {
				return;
			}
			continue;
		}

		const uint8_t len = frame[2];
		const uint32_t size = USB_CMD_FRAME_OVERHEAD + len;

		if (len == 0 || len > CMD_MAX_PAYLOAD) {
			memmove(frame, &frame[1], --fill);
			continue;
		}
		if (fill < size) {
			if (!more) {
				return;
			}
			continue;
		}
		if (frame[size - 1] == vdrum_crc8_update(CRC8_INIT, &frame[2], len + 1)) {
			if (command_cb != NULL) {
				command_cb(&frame[3], len);
			}
			memmove(frame, &frame[size], fill - size);
			fill -= size;
		} else {
			memmove(frame, &frame[1], --fill);
		}
	}
}

static void usbd_message(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	/* The nRF52840 detects VBUS: enable the device when a host is plugged in. */
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			(void)usbd_enable(ctx);
		} else if (msg->type == USBD_MSG_VBUS_REMOVED) {
			(void)usbd_disable(ctx);
		}
	}

	/*
	 * A host application opened the port (DTR raised): drop what piled up while nobody was
	 * reading, and restart the drop counter so that it reflects this session only. While the
	 * port is closed nothing is queued (usb_link_send), so that a new session does not start
	 * with old packets; the CDC ACM driver may still hold the few bytes it had already taken
	 * (measured: up to ~2 min old at the next open), which host tools skip (tools/logger.py).
	 */
	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0;

		(void)uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		k_spinlock_key_t key = k_spin_lock(&tx_lock);

		ring_buf_reset(&tx_ring);
		if (dtr) {
			dropped_blocks = 0;
		}
		atomic_set(&host_open, dtr ? 1 : 0);
		k_spin_unlock(&tx_lock, key);
		if (dtr) {
			atomic_inc(&sessions);
		}
	}
}

int usb_link_init(usb_link_command_cb on_command)
{
	int err;

	if (!device_is_ready(cdc_dev)) {
		return -ENODEV;
	}
	command_cb = on_command;

	err = usbd_add_descriptor(&vdrum_usbd, &vdrum_lang);
	err = err ? err : usbd_add_descriptor(&vdrum_usbd, &vdrum_manufacturer);
	err = err ? err : usbd_add_descriptor(&vdrum_usbd, &vdrum_product);
	err = err ? err : usbd_add_configuration(&vdrum_usbd, USBD_SPEED_FS, &vdrum_fs_config);
	err = err ? err : usbd_register_all_classes(&vdrum_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		return err;
	}

	/* Composite-style class codes so that hosts read the CDC interface descriptors (IAD). */
	usbd_device_set_code_triple(&vdrum_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02, 0x01);

	err = usbd_msg_register_cb(&vdrum_usbd, usbd_message);
	err = err ? err : usbd_init(&vdrum_usbd);
	if (err) {
		return err;
	}

	uart_irq_callback_set(cdc_dev, uart_isr);
	uart_irq_rx_enable(cdc_dev);

	if (!usbd_can_detect_vbus(&vdrum_usbd)) {
		return usbd_enable(&vdrum_usbd);
	}
	return 0;
}

int usb_link_send(const uint8_t *data, size_t len)
{
	if (!atomic_get(&host_open)) {
		return -ENOTCONN; /* nobody reading: not a drop */
	}

	k_spinlock_key_t key = k_spin_lock(&tx_lock);

	if (ring_buf_space_get(&tx_ring) < len) {
		dropped_blocks++;
		k_spin_unlock(&tx_lock, key);
		return -ENOSPC;
	}

	(void)ring_buf_put(&tx_ring, data, (uint32_t)len);
	k_spin_unlock(&tx_lock, key);

	uart_irq_tx_enable(cdc_dev);
	return 0;
}

uint32_t usb_link_dropped(void)
{
	return dropped_blocks;
}

uint32_t usb_link_session(void)
{
	return (uint32_t)atomic_get(&sessions);
}
