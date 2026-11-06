/*
 * Orientation fusion (Mahony, proportional only) with gyro bias calibration at rest: port of
 * tools/detection/fusion.py. The quaternion takes the stick frame to the world frame
 * (docs/PROTOCOL.md).
 */
#ifndef VDRUM_FUSION_H
#define VDRUM_FUSION_H

#include <stdint.h>

#include "protocol.h"

/* IMU thread context: one call per sample, at the IMU rate. */
void fusion_process(const struct imu_sample *sample);

/* Current orientation, w, x, y, z in Q14 (IMU thread context). */
void fusion_get_quat_q14(int16_t quat[4]);

/* Gyro bias in raw gyro counts (any thread: a torn read only mixes two close estimates). */
void fusion_get_bias(int16_t bias[3]);

/* Consecutive samples with the stick at rest, the test that also gates the bias calibration
 * (any thread: one aligned 32-bit word).
 */
uint32_t fusion_still_samples(void);

#endif /* VDRUM_FUSION_H */
