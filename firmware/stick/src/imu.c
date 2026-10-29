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
 * back from FIFO_DATA_OUT_H to FIFO_DATA_OUT_L, as in ST's FIFO examples.
 */

#include "imu.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "lsm6ds3tr-c_reg.h"

#define IMU_NODE DT_NODELABEL(lsm6ds3tr_c)

#define WORDS_PER_SAMPLE 6u
#define FIFO_THRESHOLD_SAMPLES RAW_MAX_SAMPLES /* one RAW packet per interrupt */
#define READ_SAMPLES_PER_TRANSFER 8u          /* 96 bytes per I2C read */
#define ORDER_DETECTION_SAMPLES 64u

/* 1 g at +-16 g full scale: 1000 mg / 0.488 mg/LSB (lsm6ds3tr_c_from_fs16g_to_mg). */
#define ONE_G_LSB 2049

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(IMU_NODE);
static const struct gpio_dt_spec int1 = GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios);

static struct gpio_callback int1_callback;
static K_SEM_DEFINE(fifo_ready, 0, 1);
static imu_batch_cb batch_callback;
static struct imu_stats stats;
static stmdev_ctx_t ctx;

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

	/* Start the sensors last, so the FIFO fills from a clean state. */
	err |= lsm6ds3tr_c_xl_data_rate_set(&ctx, LSM6DS3TR_C_XL_ODR_1k66Hz);
	err |= lsm6ds3tr_c_gy_data_rate_set(&ctx, LSM6DS3TR_C_GY_ODR_1k66Hz);

	return err == 0 ? 0 : -EIO;
}

static uint32_t now_us(void)
{
	return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64());
}

/* Reads every complete sample in the FIFO and hands them over in batches. */
static void drain_fifo(void)
{
	uint16_t level = 0;
	uint16_t pattern = 0;
	lsm6ds3tr_c_fifo_status2_t status2;

	if (lsm6ds3tr_c_fifo_data_level_get(&ctx, &level) != 0 ||
	    lsm6ds3tr_c_read_reg(&ctx, LSM6DS3TR_C_FIFO_STATUS2, (uint8_t *)&status2, 1) != 0 ||
	    lsm6ds3tr_c_fifo_pattern_get(&ctx, &pattern) != 0) {
		stats.bus_errors++;
		return;
	}
	if (status2.over_run) {
		stats.fifo_overruns++;
	}

	const uint32_t read_time = now_us();

	/* Realign on a sample boundary: skip the words of a partly read pattern. */
	while (pattern != 0 && level > 0) {
		int16_t discard;

		if (lsm6ds3tr_c_fifo_raw_data_get(&ctx, (uint8_t *)&discard, sizeof(discard)) != 0) {
			stats.bus_errors++;
			return;
		}
		level--;
		pattern = (uint16_t)((pattern + 1u) % WORDS_PER_SAMPLE);
	}

	uint32_t available = level / WORDS_PER_SAMPLE;
	/* The newest sample was taken around read_time; older ones one period apart. */
	uint32_t first_time = read_time - (available > 0 ? (available - 1u) : 0u) * IMU_SAMPLE_PERIOD_US;

	/* A batch during which the order detection completes would mix both orders: skip it. */
	const bool order_known_at_start = stats.order_known;

	while (available > 0) {
		static int16_t words[READ_SAMPLES_PER_TRANSFER * WORDS_PER_SAMPLE];
		static struct imu_sample samples[READ_SAMPLES_PER_TRANSFER];
		const uint32_t count = MIN(available, READ_SAMPLES_PER_TRANSFER);

		if (lsm6ds3tr_c_fifo_raw_data_get(&ctx, (uint8_t *)words,
						  (uint8_t)(count * WORDS_PER_SAMPLE * sizeof(int16_t))) != 0) {
			stats.bus_errors++;
			return;
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
		if (batch_callback != NULL && order_known_at_start) {
			batch_callback(samples, count, first_time);
		}

		first_time += count * IMU_SAMPLE_PERIOD_US;
		available -= count;
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
