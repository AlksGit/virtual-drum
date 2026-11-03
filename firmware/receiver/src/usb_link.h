/*
 * USB CDC ACM link to the host: device descriptors and a buffered, interrupt-driven
 * transmit path. Frames are written whole or dropped (never split by a full buffer).
 * In the other direction, host command frames are parsed and handed to a callback.
 */
#ifndef VDRUM_USB_LINK_H
#define VDRUM_USB_LINK_H

#include <stddef.h>
#include <stdint.h>

/* Called from the system work queue with the payload of each valid host command frame. */
typedef void (*usb_link_command_cb)(const uint8_t *payload, uint8_t len);

/* Registers the USB device and enables it (or waits for VBUS). Returns 0 or a negative errno. */
int usb_link_init(usb_link_command_cb on_command);

/*
 * Queues bytes for the host. Thread context only. Returns 0, -ENOTCONN if no host application
 * has the port open (nothing queued), or -ENOSPC if the whole block does not fit (it is then
 * dropped entirely, so the host never sees half a frame).
 */
int usb_link_send(const uint8_t *data, size_t len);

/* Blocks dropped because the host was not reading fast enough. */
uint32_t usb_link_dropped(void);

/* Incremented each time a host application opens the port (DTR raised). */
uint32_t usb_link_session(void);

#endif /* VDRUM_USB_LINK_H */
