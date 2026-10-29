/*
 * USB CDC ACM link, using Zephyr's USB device stack ("device_next").
 * Device setup follows zephyr/samples/subsys/usb/common/sample_usbd_init.c (NCS v3.4.1) and the
 * interrupt-driven TX path follows zephyr/samples/subsys/usb/cdc_acm/src/main.c.
 */

#include "usb_link.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usbd.h>

#include "protocol.h"

/* ~70 full RAW frames: about 0.15 s of both sticks streaming RAW data. */
#define TX_BUFFER_SIZE 8192

static const struct device *const cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

RING_BUF_DECLARE(tx_ring, TX_BUFFER_SIZE);
static struct k_spinlock tx_lock;
static uint32_t dropped_blocks;

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
			/* Nothing is expected from the host yet: drain and discard. */
			uint8_t scratch[16];

			(void)uart_fifo_read(dev, scratch, sizeof(scratch));
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
}

int usb_link_init(void)
{
	int err;

	if (!device_is_ready(cdc_dev)) {
		return -ENODEV;
	}

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
