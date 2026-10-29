/*
 * ESB radio settings shared by the sticks (PTX) and the receiver (PRX).
 * See docs/PROTOCOL.md (payloads) and docs/DECISIONS.md (RAW packet size, collisions).
 */
#ifndef VDRUM_RADIO_CONFIG_H
#define VDRUM_RADIO_CONFIG_H

#include <stdint.h>

#include "protocol.h"

/* RF channel: 2400 MHz + n. Away from the most used Wi-Fi channels (1, 6, 11). */
#define VDRUM_ESB_RF_CHANNEL 80

/*
 * Addresses: base address 0 is used by pipe 0, base address 1 by pipes 1-7; each pipe adds
 * its own prefix byte. Arbitrary project values (not a secret: ESB is not encrypted).
 */
#define VDRUM_ESB_BASE_ADDR_0 { 0x56, 0x44, 0x52, 0x4D } /* "VDRM" */
#define VDRUM_ESB_BASE_ADDR_1 { 0x76, 0x64, 0x72, 0x6D } /* "vdrm" */
#define VDRUM_ESB_PREFIXES    { 0xA1, 0xB2 }             /* pipe 0, pipe 1 */
#define VDRUM_ESB_PIPE_COUNT  2

/* One pipe per stick: stick_id 0 (left) -> pipe 0, stick_id 1 (right) -> pipe 1. */
#define VDRUM_ESB_PIPE_FOR_STICK(id) ((uint8_t)(id))

/*
 * Different retransmit delays per stick, so that two packets that collide do not collide
 * again on every retry (see DECISIONS.md, RAW packet size).
 */
#define VDRUM_ESB_RETRANSMIT_DELAY_US(id) ((id) == 0 ? 250u : 340u)
#define VDRUM_ESB_RETRANSMIT_COUNT        3

/* CONFIG_ESB_MAX_PAYLOAD_LENGTH must be at least this (checked in each app). */
#define VDRUM_ESB_MIN_PAYLOAD_CAPACITY RADIO_MAX_PAYLOAD

#endif /* VDRUM_RADIO_CONFIG_H */
