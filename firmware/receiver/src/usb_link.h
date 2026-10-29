/*
 * USB CDC ACM link to the host: device descriptors and a buffered, interrupt-driven
 * transmit path. Frames are written whole or dropped (never split by a full buffer).
 */
#ifndef VDRUM_USB_LINK_H
#define VDRUM_USB_LINK_H

#include <stddef.h>
#include <stdint.h>

/* Registers the USB device and enables it (or waits for VBUS). Returns 0 or a negative errno. */
int usb_link_init(void);

/*
 * Queues bytes for the host. Thread context only. Returns 0, or -ENOSPC if the whole block does
 * not fit (it is then dropped entirely, so the host never sees half a frame).
 */
int usb_link_send(const uint8_t *data, size_t len);

/* Blocks dropped because the host was not reading fast enough (or not connected). */
uint32_t usb_link_dropped(void);

/* Incremented each time a host application opens the port (DTR raised). */
uint32_t usb_link_session(void);

#endif /* VDRUM_USB_LINK_H */
