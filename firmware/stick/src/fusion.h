/*
 * Orientation fusion (Mahony, proportional only) with gyro bias calibration at rest: port of
 * tools/detection/fusion.py. The quaternion takes the stick frame to the world frame
 * (docs/PROTOCOL.md).
 */
#ifndef VDRUM_FUSION_H
#define VDRUM_FUSION_H

#include <stdint.h>

#include "protocol.h"

/* Before the IMU starts: takes the gyro bias kept through System OFF, if any. */
void fusion_init(void);

/* Keeps the gyro bias through System OFF. Call with the IMU thread stopped, before powering off. */
void fusion_store(void);

/* IMU thread context: one call per sample, at the IMU rate. */
void fusion_process(const struct imu_sample *sample);

/* Current orientation, w, x, y, z in Q14 (IMU thread context). */
void fusion_get_quat_q14(int16_t quat[4]);

/* Gyro bias in raw gyro counts (any thread: a torn read only mixes two close estimates). */
void fusion_get_bias(int16_t bias[3]);

#endif /* VDRUM_FUSION_H */
