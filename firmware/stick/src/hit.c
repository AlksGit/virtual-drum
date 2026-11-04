/*
 * Hit detector, ported from tools/detection/detector.py (HitDetector, stroke_rate).
 *
 * The stroke rate is the gyro rate in the stick's y-z plane, signed by y: a downstroke rotates
 * the stick around +y (stick frame: x to the tip, z up, y left, measured 2026-11-03 in
 * docs/HARDWARE.md), so it reads positive. A hit fires when, after a downstroke faster than
 * ON_DPS, the rate falls below RELEASE_FRACTION x its peak: the stick is being braked.
 *
 * Velocity is the peak rate scaled to 1..127. The Python VelocityModel (peak predicted from the
 * rise acceleration when the gyro saturates) needs a per-player calibration: not ported yet; a
 * saturated stroke is sent at 127 with HIT_FLAG_GYRO_SAT.
 */

#include "hit.h"

#include "imu.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

/* LSM6DS3TR-C at +-2000 dps: 70 mdps/LSB (docs/HARDWARE.md, lsm6ds3tr_c_from_fs2000dps_to_mdps). */
#define GYRO_DPS_PER_LSB 0.070f

#define ON_DPS 260.0f           /* detector.py DetectorConfig.on_dps */
#define RELEASE_FRACTION 0.35f  /* detector.py DetectorConfig.release_fraction */
/* detector.py uses 40 ms. On real air strokes (session_20261103_153541) 40 ms leaves detections
 * 41-70 ms after the previous one (15 of 141): rebounds or fast roll strokes, which that session
 * cannot tell apart (no ground truth). Hits on a cushion (2026-11-04, 70 ms) still gave weak
 * doubles ~80 ms after a strong hit: rebounds. 100 ms still allows 10 strokes/s per hand. */
#define REFRACTORY_US 100000u
/* Rate mapped to velocity 127. Air strokes peaked at 1360 dps (docs/MEASUREMENTS.md,
 * 2026-11-03), but hits on a cushion (2026-11-04) peaked at 260-660 dps: at 1500 dps they came
 * out at velocity 22-56, nearly inaudible through the host's gain curve (v^1.6). */
#define MAX_SPEED_DPS 800.0f

static hit_cb callback;
static bool in_stroke;
static bool stroke_saturated;
static float peak_dps;
static uint32_t refractory_until_us;
static bool refractory;

void hit_init(hit_cb on_hit)
{
	callback = on_hit;
}

static bool saturated(int16_t value)
{
	return value == INT16_MAX || value == INT16_MIN;
}

static uint8_t velocity_from_peak(float peak, bool sat)
{
	if (sat) {
		return 127;
	}
	const float v = peak / MAX_SPEED_DPS * 127.0f;

	return v >= 127.0f ? 127 : (v < 1.0f ? 1 : (uint8_t)v);
}

static void process_one(const struct imu_sample *s, uint32_t t_us)
{
	const float gy = (float)s->gy * GYRO_DPS_PER_LSB;
	const float gz = (float)s->gz * GYRO_DPS_PER_LSB;
	const float magnitude = sqrtf(gy * gy + gz * gz);
	const float rate = gy >= 0.0f ? magnitude : -magnitude;
	const bool sat = saturated(s->gy) || saturated(s->gz);

	if (refractory) {
		if ((int32_t)(t_us - refractory_until_us) < 0) {
			return;
		}
		refractory = false;
	}

	if (!in_stroke) {
		if (rate > ON_DPS) {
			in_stroke = true;
			peak_dps = rate;
			stroke_saturated = sat;
		}
		return;
	}

	if (rate >= peak_dps) {
		peak_dps = rate;
	}
	stroke_saturated = stroke_saturated || sat;

	if (rate < RELEASE_FRACTION * peak_dps) {
		in_stroke = false;
		refractory = true;
		refractory_until_us = t_us + REFRACTORY_US;
		if (callback != NULL) {
			callback(t_us, velocity_from_peak(peak_dps, stroke_saturated),
				 stroke_saturated ? HIT_FLAG_GYRO_SAT : 0);
		}
	}
}

void hit_process(const struct imu_sample *samples, uint32_t count, uint32_t first_time_us)
{
	for (uint32_t i = 0; i < count; i++) {
		process_one(&samples[i], first_time_us + i * IMU_SAMPLE_PERIOD_US);
	}
}
