/*
 * LSM6DS3TR-C FIFO streaming, through ST's driver shipped with NCS v3.4.1
 * (modules/hal/st/sensor/stmemsc/lsm6ds3tr-c_STdC).
 *
 * Hardware (zephyr/boards/seeed/xiao_ble/xiao_ble_nrf52840_sense.dts, NCS v3.4.1): sensor on
 * i2c0 at 0x6A, INT1 on P0.11 (irq-gpios), power switched by P1.08 through the
 * "lsm6ds3tr-c-en" fixed regulator, which is on at boot (CONFIG_REGULATOR).
 *
 * FIFO: gyroscope and accelerometer batched at the same rate, so each sample is a pattern of
 * 6 words (two data sets of X, Y, Z). The order of the two data sets is detected at start-up
 * (the accelerometer reads ~1 g, i.e. ~2049 LSB at +-16 g) rather than assumed; to be
 * confirmed on hardware (see docs/HARDWARE.md).
 *
 * TO VERIFY ON HARDWARE: multi-byte reads of FIFO_DATA_OUT rely on the output address rolling
 * back from FIFO_DATA_OUT_H to FIFO_DATA_OUT_L, as in ST's FIFO examples (compare one burst read
 * with word-by-word reads at bring-up).
 *
 * INT1 (FIFO threshold, FTH) follows the FIFO_STATUS2 "waterm" flag, high while the FIFO holds
 * at least the threshold. It is armed on its rising edge, so every drain reads until the level is
 * back below the threshold: otherwise INT1 would stay high, no new edge would come, and only the
 * 20 ms timeout would read the FIFO (expert review, 2026-10-29).
 *
 * Timestamps: the INT1 edge marks the arrival of the FIFO_THRESHOLD_SAMPLES-th unread sample. The
 * ISR records that time; sample times follow a continuous count at the nominal period, pulled
 * slowly towards each edge (drift of the IMU clock), and snapped to it after a disruption.
 */

#include "imu.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zephyr/sys/atomic.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "lsm6ds3tr-c_reg.h"

#define IMU_NODE DT_NODELABEL(lsm6ds3tr_c)

#define WORDS_PER_SAMPLE 6u
#define FIFO_THRESHOLD_SAMPLES RAW_MAX_SAMPLES /* one RAW packet per interrupt */
#define READ_SAMPLES_PER_TRANSFER 8u          /* 96 bytes per I2C read */
#define ORDER_DETECTION_SAMPLES 64u
#define MAX_DRAIN_ROUNDS 8u                   /* bounded: the FIFO refills while it is read */

/* Timestamp tracking: snap when the edge disagrees by more than this, else move 1/8 of the way. */
#define SNAP_THRESHOLD_US (2u * IMU_SAMPLE_PERIOD_US)
#define DRIFT_GAIN_SHIFT 3

/* lsm6ds3tr_c_fifo_raw_data_get takes a uint8_t length. */
BUILD_ASSERT(READ_SAMPLES_PER_TRANSFER * WORDS_PER_SAMPLE * sizeof(int16_t) <= UINT8_MAX,
	     "FIFO read too long for one transfer");
BUILD_ASSERT(FIFO_THRESHOLD_SAMPLES >= 1u, "FIFO threshold must be at least one sample");

/* 1 g at +-16 g full scale: 1000 mg / 0.488 mg/LSB (lsm6ds3tr_c_from_fs16g_to_mg). */
#define ONE_G_LSB 2049

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(IMU_NODE);
static const struct gpio_dt_spec int1 = GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios);

static struct gpio_callback int1_callback;
static K_SEM_DEFINE(fifo_ready, 0, 1);
static imu_batch_cb batch_callback;
static struct imu_stats stats;
static stmdev_ctx_t ctx;

/* Time of the last INT1 edge, and whether a drain has not used it yet. */
static volatile uint32_t edge_time_us;
static atomic_t edge_pending;

/* Expected time of the oldest unread sample in the FIFO; valid once timing_known is set. */
static uint32_t head_time_us;
static bool timing_known;

/* Order detection: sum of |first triplet| - 1 g and |second triplet| - 1 g over the first samples. */
static int64_t first_error_sum;
static int64_t second_error_sum;
static uint32_t detection_count;

static int32_t bus_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
	ARG_UNUSED(handle);
	return i2c_burst_write_dt(&bus, reg, data, len);
}

static int32_t bus_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
	ARG_UNUSED(handle);
	return i2c_burst_read_dt(&bus, reg, data, len);
}

static void bus_delay(uint32_t ms)
{
	k_msleep(ms);
}

static void int1_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	edge_time_us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64());
	atomic_set(&edge_pending, 1);
	k_sem_give(&fifo_ready);
}

static int32_t magnitude_error(const int16_t *triplet)
{
	/* Integer |v| - 1 g, without square roots: compare squared norms scaled down. */
	const int64_t x = triplet[0], y = triplet[1], z = triplet[2];
	const int64_t norm_sq = x * x + y * y + z * z;
	const int64_t target_sq = (int64_t)ONE_G_LSB * ONE_G_LSB;

	return (int32_t)(llabs(norm_sq - target_sq) >> 10);
}

static void detect_order(const int16_t *words)
{
	if (stats.order_known) {
		return;
	}

	first_error_sum += magnitude_error(&words[0]);
	second_error_sum += magnitude_error(&words[3]);
	if (++detection_count >= ORDER_DETECTION_SAMPLES) {
		stats.accel_first = first_error_sum < second_error_sum;
		stats.order_known = 1;
	}
}

static int configure(void)
{
	uint8_t id = 0;
	uint8_t resetting = 1;
	int32_t err = 0;

	ctx.write_reg = bus_write;
	ctx.read_reg = bus_read;
	ctx.mdelay = bus_delay;
	ctx.handle = NULL;

	if (lsm6ds3tr_c_device_id_get(&ctx, &id) != 0) {
		return -EIO;
	}
	if (id != LSM6DS3TR_C_ID) {
		return -ENODEV;
	}

	err |= lsm6ds3tr_c_reset_set(&ctx, PROPERTY_ENABLE);
	for (int i = 0; i < 100 && resetting; i++) {
		err |= lsm6ds3tr_c_reset_get(&ctx, &resetting);
		k_msleep(1);
	}

	err |= lsm6ds3tr_c_auto_increment_set(&ctx, PROPERTY_ENABLE);
	err |= lsm6ds3tr_c_block_data_update_set(&ctx, PROPERTY_ENABLE);
	err |= lsm6ds3tr_c_xl_full_scale_set(&ctx, LSM6DS3TR_C_16g);
	err |= lsm6ds3tr_c_gy_full_scale_set(&ctx, LSM6DS3TR_C_2000dps);

	/* FIFO: both sensors, no decimation, continuous (stream) mode, threshold in words. */
	err |= lsm6ds3tr_c_fifo_mode_set(&ctx, LSM6DS3TR_C_BYPASS_MODE); /* clears the FIFO */
	err |= lsm6ds3tr_c_fifo_watermark_set(&ctx, (uint16_t)(FIFO_THRESHOLD_SAMPLES * WORDS_PER_SAMPLE));
	err |= lsm6ds3tr_c_fifo_xl_batch_set(&ctx, LSM6DS3TR_C_FIFO_XL_NO_DEC);
	err |= lsm6ds3tr_c_fifo_gy_batch_set(&ctx, LSM6DS3TR_C_FIFO_GY_NO_DEC);
	err |= lsm6ds3tr_c_fifo_data_rate_set(&ctx, LSM6DS3TR_C_FIFO_1k66Hz);
	err |= lsm6ds3tr_c_fifo_mode_set(&ctx, LSM6DS3TR_C_STREAM_MODE);

	/* INT1 pulses on the FIFO threshold. */
	lsm6ds3tr_c_int1_route_t route = { 0 };

	route.int1_fth = PROPERTY_ENABLE;
	err |= lsm6ds3tr_c_pin_int1_route_set(&ctx, route);

	return err == 0 ? 0 : -EIO;
}

/* Started last, after INT1 is armed, so that the first threshold edge cannot be missed. */
static int start_sensors(void)
{
	int32_t err = 0;

	err |= lsm6ds3tr_c_xl_data_rate_set(&ctx, LSM6DS3TR_C_XL_ODR_1k66Hz);
	err |= lsm6ds3tr_c_gy_data_rate_set(&ctx, LSM6DS3TR_C_GY_ODR_1k66Hz);
	return err == 0 ? 0 : -EIO;
}

struct fifo_status {
	uint16_t level;   /* unread words */
	uint16_t pattern; /* index of the next word in the 6-word sample pattern */
	bool overrun;
};

/*
 * FIFO_STATUS1..4 (0x3A-0x3D) in one burst instead of five single-byte reads. Layout from
 * lsm6ds3tr-c_reg.h (NCS v3.4.1): level = STATUS2[2:0] << 8 | STATUS1, overrun = STATUS2 bit 6,
 * pattern = STATUS4[1:0] << 8 | STATUS3. Register auto-increment (IF_INC) is set in configure().
 */
static int read_fifo_status(struct fifo_status *out)
{
	uint8_t reg[4];

	if (lsm6ds3tr_c_read_reg(&ctx, LSM6DS3TR_C_FIFO_STATUS1, reg, sizeof(reg)) != 0) {
		stats.bus_errors++;
		return -EIO;
	}
	out->level = (uint16_t)(((reg[1] & 0x07u) << 8) | reg[0]);
	out->overrun = (reg[1] & 0x40u) != 0;
	out->pattern = (uint16_t)(((reg[3] & 0x03u) << 8) | reg[2]);
	if (out->overrun) {
		stats.fifo_overruns++;
	}
	return 0;
}

/* Updates head_time_us from a fresh INT1 edge, if any (see the timestamps note at the top). */
static void track_edge(bool disrupted)
{
	if (!atomic_cas(&edge_pending, 1, 0)) {
		return;
	}

	const uint32_t estimate = edge_time_us - (FIFO_THRESHOLD_SAMPLES - 1u) * IMU_SAMPLE_PERIOD_US;
	const int32_t error = (int32_t)(estimate - head_time_us);

	if (!timing_known || disrupted || (uint32_t)abs(error) > SNAP_THRESHOLD_US) {
		head_time_us = estimate;
		timing_known = true;
	} else {
		head_time_us += (uint32_t)(error / (1 << DRIFT_GAIN_SHIFT));
	}
}

/* Skips the words of a partly read sample, so that reads start on a sample boundary. */
static int realign(struct fifo_status *status)
{
	while (status->pattern != 0 && status->level > 0) {
		int16_t discard;

		if (lsm6ds3tr_c_fifo_raw_data_get(&ctx, (uint8_t *)&discard, sizeof(discard)) != 0) {
			stats.bus_errors++;
			return -EIO;
		}
		status->level--;
		status->pattern = (uint16_t)((status->pattern + 1u) % WORDS_PER_SAMPLE);
	}
	return 0;
}

/* Reads `available` complete samples and hands them over in batches. */
static int read_samples(uint32_t available, bool order_known_at_start)
{
	while (available > 0) {
		static int16_t words[READ_SAMPLES_PER_TRANSFER * WORDS_PER_SAMPLE];
		static struct imu_sample samples[READ_SAMPLES_PER_TRANSFER];
		const uint32_t count = MIN(available, READ_SAMPLES_PER_TRANSFER);

		if (lsm6ds3tr_c_fifo_raw_data_get(&ctx, (uint8_t *)words,
						  (uint8_t)(count * WORDS_PER_SAMPLE * sizeof(int16_t))) != 0) {
			stats.bus_errors++;
			timing_known = false; /* unknown how much was consumed: resync on the next edge */
			return -EIO;
		}

		for (uint32_t i = 0; i < count; i++) {
			const int16_t *w = &words[i * WORDS_PER_SAMPLE];

			detect_order(w);
			const int16_t *accel = stats.accel_first ? &w[0] : &w[3];
			const int16_t *gyro = stats.accel_first ? &w[3] : &w[0];

			samples[i] = (struct imu_sample){
				.ax = accel[0], .ay = accel[1], .az = accel[2],
				.gx = gyro[0], .gy = gyro[1], .gz = gyro[2],
			};
		}

		stats.samples += count;
		stats.batches++;
		/* A batch during which the order detection completes would mix both orders: skip it.
		 * Samples read before the first edge have no time base: skipped too.
		 */
		if (batch_callback != NULL && order_known_at_start && timing_known) {
			batch_callback(samples, count, head_time_us);
		}

		head_time_us += count * IMU_SAMPLE_PERIOD_US;
		available -= count;
	}
	return 0;
}

/* Reads until the FIFO is back below the threshold, so that INT1 can rise again. */
static void drain_fifo(void)
{
	const bool order_known_at_start = stats.order_known;

	for (uint32_t round = 0; round < MAX_DRAIN_ROUNDS; round++) {
		struct fifo_status status;

		if (read_fifo_status(&status) != 0) {
			return;
		}
		if (round == 0) {
			track_edge(status.overrun || status.pattern != 0);
		}
		if (realign(&status) != 0) {
			return;
		}

		const uint32_t available = status.level / WORDS_PER_SAMPLE;

		if (round > 0 && available < FIFO_THRESHOLD_SAMPLES) {
			return; /* below the threshold: INT1 is low, the next edge will come */
		}
		if (available == 0 || read_samples(available, order_known_at_start) != 0) {
			return;
		}
	}
}

static void imu_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		/* The timeout catches a missed edge: the FIFO is drained in any case. */
		(void)k_sem_take(&fifo_ready, K_MSEC(20));
		drain_fifo();
	}
}

K_THREAD_STACK_DEFINE(imu_stack, 2048);
static struct k_thread imu_thread_data;

int imu_start(imu_batch_cb callback)
{
	int err;

	if (!i2c_is_ready_dt(&bus) || !gpio_is_ready_dt(&int1)) {
		return -ENODEV;
	}

	batch_callback = callback;
	err = configure();
	if (err) {
		return err;
	}

	err = gpio_pin_configure_dt(&int1, GPIO_INPUT);
	err = err ? err : gpio_pin_interrupt_configure_dt(&int1, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		return err;
	}
	gpio_init_callback(&int1_callback, int1_isr, BIT(int1.pin));
	err = gpio_add_callback(int1.port, &int1_callback);
	err = err ? err : start_sensors();
	if (err) {
		return err;
	}

	/* Cooperative priority: the hit detection that will run here is latency critical. */
	k_thread_create(&imu_thread_data, imu_stack, K_THREAD_STACK_SIZEOF(imu_stack), imu_thread,
			NULL, NULL, NULL, K_PRIO_COOP(2), 0, K_NO_WAIT);
	k_thread_name_set(&imu_thread_data, "imu");
	return 0;
}

void imu_get_stats(struct imu_stats *out)
{
	*out = stats;
}
