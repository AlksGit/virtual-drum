/*
 * ESB transmitter (PTX) of a stick: packs IMU samples into RAW packets of up to RAW_MAX_SAMPLES
 * samples and sends them, plus a STATUS packet once per second (docs/PROTOCOL.md).
 */
#ifndef VDRUM_RADIO_H
#define VDRUM_RADIO_H

#include <stdint.h>

#include "protocol.h"

int radio_start(uint8_t stick_id);

/* IMU thread context. Samples are consecutive at the IMU rate, the first one at first_time_us. */
void radio_send_samples(const struct imu_sample *samples, uint32_t count, uint32_t first_time_us);

/* Sends a STATUS packet (thread context). */
/* imu_overruns: IMU FIFO overrun count, reported in STATUS (protocol v4). */
void radio_send_status(uint16_t vbat_mv, uint32_t imu_overruns);

struct radio_stats {
	uint32_t packets_queued;
	uint32_t packets_sent;
	uint32_t packets_failed;  /* no ACK after all retransmits: lost */
	uint32_t queue_full;      /* not queued: the radio could not keep up */
	uint32_t retries;         /* extra attempts (approximate, see radio.c) */
};

void radio_get_stats(struct radio_stats *stats);

#endif /* VDRUM_RADIO_H */
