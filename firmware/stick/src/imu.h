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
	uint8_t accel_first;    /* FIFO order: 1 = accel then gyro, 0 = gyro then accel (ST's, the default) */
	uint8_t order_known;    /* the order was checked on samples near rest */
};

void imu_get_stats(struct imu_stats *stats);

/* Stops streaming and leaves the sensor watching for motion: accelerometer alone at 104 Hz,
 * INT1 pulsing on a sharp knock (1.5 g of slope), and INT1 set to wake the nRF52840 from System OFF (level
 * sense). The IMU thread ends. Returns 0, or a negative errno with streaming stopped anyway.
 */
int imu_prepare_sleep(void);

#endif /* VDRUM_IMU_H */
