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

#define PROTO_VERSION 4

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
	/* Cumulative counters since boot, wrapping at 2^16 (compare with modular differences). */
	uint16_t tx_retries;     /* extra ESB attempts (approximate: ESB may merge TX events) */
	uint16_t tx_failed;      /* payloads dropped after all retransmits */
	uint16_t tx_queue_full;  /* payloads not queued: the radio could not keep up */
	uint16_t imu_overruns;   /* IMU FIFO overruns: samples lost before they were read */
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
VDRUM_STATIC_ASSERT(sizeof(struct pkt_status) == 21, "pkt_status must be 21 bytes");

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

/* Byte-wise table for CRC8_POLY: entry i is the CRC of the single byte i (bitwise algorithm). */
static const uint8_t vdrum_crc8_table[256] = {
	0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31,
	0x24, 0x23, 0x2A, 0x2D, 0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
	0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D, 0xE0, 0xE7, 0xEE, 0xE9,
	0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
	0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1,
	0xB4, 0xB3, 0xBA, 0xBD, 0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
	0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA, 0xB7, 0xB0, 0xB9, 0xBE,
	0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
	0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16,
	0x03, 0x04, 0x0D, 0x0A, 0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
	0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A, 0x89, 0x8E, 0x87, 0x80,
	0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
	0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8,
	0xDD, 0xDA, 0xD3, 0xD4, 0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
	0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44, 0x19, 0x1E, 0x17, 0x10,
	0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
	0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F,
	0x6A, 0x6D, 0x64, 0x63, 0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
	0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13, 0xAE, 0xA9, 0xA0, 0xA7,
	0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
	0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF,
	0xFA, 0xFD, 0xF4, 0xF3
};

static inline uint8_t vdrum_crc8_update(uint8_t crc, const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		crc = vdrum_crc8_table[crc ^ data[i]];
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
