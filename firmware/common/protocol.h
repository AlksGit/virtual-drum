/*
 * Shared packet definitions — stick <-> receiver <-> host.
 * Source of truth for docs/PROTOCOL.md. Any change: bump PROTO_VERSION
 * and update the Python parser in tools/ and the host parser in host/.
 *
 * DRAFT (phase 0). RAW packets batch up to RAW_MAX_SAMPLES samples and need ESB dynamic
 * payload length (see docs/DECISIONS.md, 2026-10, RAW packet size).
 *
 * All fields are little-endian. Structures are packed; they describe the wire layout
 * but parsers should decode field by field rather than casting buffers.
 */
#ifndef VDRUM_PROTOCOL_H
#define VDRUM_PROTOCOL_H

#include <stdint.h>

#define PROTO_VERSION 3

/* Portable packing: GCC/Clang attribute, MSVC pragma. */
#if defined(_MSC_VER)
#define VDRUM_PACKED_BEGIN __pragma(pack(push, 1))
#define VDRUM_PACKED_END   __pragma(pack(pop))
#define VDRUM_PACKED
#else
#define VDRUM_PACKED_BEGIN
#define VDRUM_PACKED_END
#define VDRUM_PACKED __attribute__((packed))
#endif

#if defined(__cplusplus)
#define VDRUM_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define VDRUM_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

enum pkt_type {
	PKT_RAW    = 0x01,
	PKT_HIT    = 0x02,
	PKT_ORIENT = 0x03,
	PKT_STATUS = 0x04,
};

VDRUM_PACKED_BEGIN

struct VDRUM_PACKED pkt_header {
	uint8_t  type;     /* enum pkt_type */
	uint8_t  stick_id; /* 0 = left, 1 = right */
	uint16_t seq;      /* per-stick counter, for loss detection */
};

struct VDRUM_PACKED imu_sample {
	int16_t ax, ay, az; /* raw sensor units */
	int16_t gx, gy, gz;
};

/* 8 samples = one packet every 4.8 ms per stick at 1.66 kHz, 105-byte payload.
 * Requires ESB DPL with CONFIG_ESB_MAX_PAYLOAD_LENGTH >= RADIO_MAX_PAYLOAD. */
#define RAW_MAX_SAMPLES 8
#define PKT_RAW_SIZE(n) (9 + 12 * (n)) /* header + t0_us + n + samples */

struct VDRUM_PACKED pkt_raw {
	struct pkt_header hdr;
	uint32_t t0_us;
	uint8_t  n;
	struct imu_sample samples[RAW_MAX_SAMPLES];
};

struct VDRUM_PACKED pkt_hit {
	struct pkt_header hdr;
	uint32_t t_us;
	uint8_t  velocity; /* 1..127 */
	int16_t  quat[4];  /* w, x, y, z in Q14 (1.0 = 16384), stick -> world, see PROTOCOL.md */
	uint8_t  flags;
};

struct VDRUM_PACKED pkt_orient {
	struct pkt_header hdr;
	uint32_t t_us;
	int16_t  quat[4]; /* w, x, y, z in Q14 (1.0 = 16384) */
};

struct VDRUM_PACKED pkt_status {
	struct pkt_header hdr;
	uint16_t vbat_mv;
	int16_t  gyro_bias[3];
	uint8_t  fw_version;
};

VDRUM_PACKED_END

VDRUM_STATIC_ASSERT(sizeof(struct pkt_header) == 4, "pkt_header must be 4 bytes");
VDRUM_STATIC_ASSERT(sizeof(struct imu_sample) == 12, "imu_sample must be 12 bytes");
VDRUM_STATIC_ASSERT(sizeof(struct pkt_raw) == PKT_RAW_SIZE(RAW_MAX_SAMPLES), "pkt_raw size mismatch");

/* Largest radio payload: the full RAW packet. ESB supports up to 252 bytes. */
#define RADIO_MAX_PAYLOAD PKT_RAW_SIZE(RAW_MAX_SAMPLES)
VDRUM_STATIC_ASSERT(RADIO_MAX_PAYLOAD <= 252, "payload exceeds the ESB maximum");
VDRUM_STATIC_ASSERT(sizeof(struct pkt_hit) == 18, "pkt_hit must be 18 bytes");
VDRUM_STATIC_ASSERT(sizeof(struct pkt_orient) == 16, "pkt_orient must be 16 bytes");
VDRUM_STATIC_ASSERT(sizeof(struct pkt_status) == 13, "pkt_status must be 13 bytes");

#define HIT_FLAG_GYRO_SAT  (1u << 0)
#define HIT_FLAG_ACCEL_SAT (1u << 1)

#define QUAT_Q14_ONE 16384

/*
 * USB CDC framing (receiver -> host)
 * frame: SYNC0 SYNC1 | len u8 | rx_time_us u32 | rssi i8 | payload[len] | crc8
 * len = payload length only. crc8 covers len, rx_time_us, rssi and payload
 * (everything between the sync bytes and the crc).
 */
#define USB_SYNC0 0xA5
#define USB_SYNC1 0x5A
#define USB_FRAME_HEADER_SIZE 8  /* sync(2) + len(1) + rx_time_us(4) + rssi(1) */
#define USB_FRAME_OVERHEAD    9  /* header + crc8 */

/*
 * CRC-8/SMBUS: poly 0x07, init 0x00, no reflection, no final xor.
 * Check value: crc8("123456789") == 0xF4.
 */
#define CRC8_POLY 0x07
#define CRC8_INIT 0x00

static inline uint8_t vdrum_crc8_update(uint8_t crc, const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (uint8_t)((crc & 0x80u) ? ((crc << 1) ^ CRC8_POLY) : (crc << 1));
		}
	}
	return crc;
}

/*
 * USB identifiers of the receiver. Phase 0 keeps Zephyr's default development IDs
 * (CONFIG_USB_DEVICE_VID / CONFIG_USB_DEVICE_PID defaults, source: zephyr
 * subsys/usb/device/Kconfig); hosts also match the product
 * string. To be replaced if the device is ever distributed.
 */
#define USB_RECEIVER_VID 0x2FE3
#define USB_RECEIVER_PID 0x0100
#define USB_RECEIVER_PRODUCT "Virtual Drum Receiver"

#endif /* VDRUM_PROTOCOL_H */
