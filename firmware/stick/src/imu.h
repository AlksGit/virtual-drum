/*
 * LSM6DS3TR-C streaming through its FIFO: accelerometer +-16 g and gyroscope +-2000 dps,
 * both at 1.66 kHz, read in batches on the FIFO threshold interrupt (INT1).
 */
#ifndef VDRUM_IMU_H
#define VDRUM_IMU_H

#include <stdint.h>

#include "protocol.h"

#define IMU_ODR_HZ 1666u
#define IMU_SAMPLE_PERIOD_US 600u /* 1e6 / 1666, rounded */

/* Called from the IMU thread for each batch of consecutive samples (oldest first). */
typedef void (*imu_batch_cb)(const struct imu_sample *samples, uint32_t count, uint32_t first_sample_time_us);

/* Configures the sensor and starts streaming. Returns 0 or a negative errno. */
int imu_start(imu_batch_cb callback);

struct imu_stats {
	uint32_t samples;
	uint32_t batches;
	uint32_t fifo_overruns; /* FIFO filled up before it was read: samples were lost */
	uint32_t bus_errors;
	uint8_t accel_first;    /* order detected in the FIFO: 1 = accel then gyro, 0 = gyro then accel */
	uint8_t order_known;
};

void imu_get_stats(struct imu_stats *stats);

#endif /* VDRUM_IMU_H */
