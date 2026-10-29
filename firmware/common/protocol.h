/*
 * Shared packet definitions — stick <-> receiver <-> host.
 * Source of truth for docs/PROTOCOL.md. Any change: bump PROTO_VERSION
 * and update the Python parser in tools/.
 *
 * DRAFT (phase 0): RAW packet sizing still to be decided (static 32 B vs ESB DPL).
 */
#ifndef VDRUM_PROTOCOL_H
#define VDRUM_PROTOCOL_H

#include <stdint.h>

#define PROTO_VERSION 0

enum pkt_type {
	PKT_RAW    = 0x01,
	PKT_HIT    = 0x02,
	PKT_ORIENT = 0x03,
	PKT_STATUS = 0x04,
};

struct __attribute__((packed)) pkt_header {
	uint8_t  type;     /* enum pkt_type */
	uint8_t  stick_id; /* 0 = left, 1 = right */
	uint16_t seq;      /* per-stick counter, for loss detection */
};

struct __attribute__((packed)) imu_sample {
	int16_t ax, ay, az; /* raw sensor units */
	int16_t gx, gy, gz;
};

#define RAW_MAX_SAMPLES 1 /* TODO(phase0): raise if ESB dynamic payload is enabled */

struct __attribute__((packed)) pkt_raw {
	struct pkt_header hdr;
	uint32_t t0_us;
	uint8_t  n;
	struct imu_sample samples[RAW_MAX_SAMPLES];
};

struct __attribute__((packed)) pkt_hit {
	struct pkt_header hdr;
	uint32_t t_us;
	uint8_t  velocity; /* 1..127 */
	int16_t  quat[4];  /* Q14 */
	uint8_t  flags;
};

struct __attribute__((packed)) pkt_orient {
	struct pkt_header hdr;
	uint32_t t_us;
	int16_t  quat[4]; /* Q14 */
};

struct __attribute__((packed)) pkt_status {
	struct pkt_header hdr;
	uint16_t vbat_mv;
	int16_t  gyro_bias[3];
	uint8_t  fw_version;
};

#define HIT_FLAG_GYRO_SAT  (1u << 0)
#define HIT_FLAG_ACCEL_SAT (1u << 1)

/* USB CDC framing (receiver -> host) */
#define USB_SYNC0 0xA5
#define USB_SYNC1 0x5A
/* frame: SYNC0 SYNC1 | len u8 | rx_time_us u32 | rssi i8 | payload[len] | crc8 */

#endif /* VDRUM_PROTOCOL_H */
