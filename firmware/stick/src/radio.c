/*
 * ESB transmitter of a stick. Setup follows nrf/samples/esb/esb_ptx (NCS v3.4.1).
 *
 * After a failed transmission (no ACK once retransmits are exhausted) ESB suspends TX with the
 * failed payload still at the head of its FIFO (nrf/subsys/esb/esb.c, on_radio_disabled_tx_wait
 * _for_ack): the payload is dropped (the receiver sees a gap in seq) and TX restarted, so that a
 * lost packet never stalls the stream.
 */

#include "radio.h"

#include <errno.h>
#include <string.h>

#include <esb.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "imu.h"
#include "radio_config.h"

BUILD_ASSERT(CONFIG_ESB_MAX_PAYLOAD_LENGTH >= VDRUM_ESB_MIN_PAYLOAD_CAPACITY,
	     "CONFIG_ESB_MAX_PAYLOAD_LENGTH too small for RAW packets");

#define FIRMWARE_VERSION 1

static uint8_t stick;
static uint16_t next_seq;
static struct k_spinlock seq_lock;

static atomic_t queued, sent, failed, full;

/* RAW packets come from the IMU thread, STATUS from the main thread: one writer at a time. */
static K_MUTEX_DEFINE(write_lock);

/* RAW packet being filled by the IMU thread. */
static struct pkt_raw raw;
static uint32_t raw_first_time;

static void esb_event(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		atomic_inc(&sent);
		break;
	case ESB_EVENT_TX_FAILED:
		atomic_inc(&failed);
		(void)esb_pop_tx();
		(void)esb_start_tx(); /* -ENODATA when nothing else is queued */
		break;
	default:
		break;
	}
}

static uint16_t take_seq(void)
{
	k_spinlock_key_t key = k_spin_lock(&seq_lock);
	const uint16_t seq = next_seq++;

	k_spin_unlock(&seq_lock, key);
	return seq;
}

static void send_payload(const void *data, uint8_t length)
{
	struct esb_payload payload = {
		.length = length,
		.pipe = VDRUM_ESB_PIPE_FOR_STICK(stick),
		.noack = false,
	};

	memcpy(payload.data, data, length);

	(void)k_mutex_lock(&write_lock, K_FOREVER);
	const int err = esb_write_payload(&payload);

	k_mutex_unlock(&write_lock);

	if (err == 0) {
		atomic_inc(&queued);
	} else {
		atomic_inc(&full);
	}
}

int radio_start(uint8_t stick_id)
{
	uint8_t base_addr_0[4] = VDRUM_ESB_BASE_ADDR_0;
	uint8_t base_addr_1[4] = VDRUM_ESB_BASE_ADDR_1;
	uint8_t prefixes[VDRUM_ESB_PIPE_COUNT] = VDRUM_ESB_PREFIXES;
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	stick = stick_id;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.mode = ESB_MODE_PTX;
	config.event_handler = esb_event;
	config.selective_auto_ack = true;
	config.retransmit_delay = VDRUM_ESB_RETRANSMIT_DELAY_US(stick_id);
	config.retransmit_count = VDRUM_ESB_RETRANSMIT_COUNT;
	config.tx_mode = ESB_TXMODE_AUTO;

	err = esb_init(&config);
	err = err ? err : esb_set_rf_channel(VDRUM_ESB_RF_CHANNEL);
	err = err ? err : esb_set_base_address_0(base_addr_0);
	err = err ? err : esb_set_base_address_1(base_addr_1);
	err = err ? err : esb_set_prefixes(prefixes, ARRAY_SIZE(prefixes));
	return err;
}

void radio_send_samples(const struct imu_sample *samples, uint32_t count, uint32_t first_time_us)
{
	for (uint32_t i = 0; i < count; i++) {
		if (raw.n == 0) {
			raw_first_time = first_time_us + i * IMU_SAMPLE_PERIOD_US;
		}
		raw.samples[raw.n++] = samples[i];

		if (raw.n == RAW_MAX_SAMPLES) {
			raw.hdr.type = PKT_RAW;
			raw.hdr.stick_id = stick;
			raw.hdr.seq = take_seq();
			raw.t0_us = raw_first_time;
			send_payload(&raw, PKT_RAW_SIZE(raw.n));
			raw.n = 0;
		}
	}
}

void radio_send_status(uint16_t vbat_mv)
{
	struct pkt_status status = {
		.hdr = { .type = PKT_STATUS, .stick_id = stick, .seq = take_seq() },
		.vbat_mv = vbat_mv,
		.gyro_bias = { 0, 0, 0 }, /* bias calibration comes with phase 2 */
		.fw_version = FIRMWARE_VERSION,
	};

	send_payload(&status, sizeof(status));
}

void radio_get_stats(struct radio_stats *stats)
{
	stats->packets_queued = (uint32_t)atomic_get(&queued);
	stats->packets_sent = (uint32_t)atomic_get(&sent);
	stats->packets_failed = (uint32_t)atomic_get(&failed);
	stats->queue_full = (uint32_t)atomic_get(&full);
}
