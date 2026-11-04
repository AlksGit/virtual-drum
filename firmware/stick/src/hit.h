/*
 * On-board hit detection: port of tools/detection/detector.py (HitDetector), one sample at a
 * time, a few state variables, no buffers.
 */
#ifndef VDRUM_HIT_H
#define VDRUM_HIT_H

#include <stdint.h>

#include "protocol.h"

/* Called from the IMU thread when a hit fires: velocity 1..127, flags HIT_FLAG_*. */
typedef void (*hit_cb)(uint32_t t_us, uint8_t velocity, uint8_t flags);

void hit_init(hit_cb on_hit);

/* IMU thread context. Samples are consecutive at the IMU rate, the first one at first_time_us. */
void hit_process(const struct imu_sample *samples, uint32_t count, uint32_t first_time_us);

#endif /* VDRUM_HIT_H */
