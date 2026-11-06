/*
 * Mahony fusion, ported from tools/detection/fusion.py (Mahony.process); constants are the
 * FusionConfig defaults, see that file for the reasoning.
 *
 * - Gyro bias averaged while the stick is still, so each pause recalibrates it. Still = the
 *   means of blocks of STILL_BLOCK samples stay close to their mean over STILL_BLOCKS blocks,
 *   whatever the current bias (a wrong bias locked the left stick at 3 deg/s of drift on
 *   2026-11-06 with a test against the bias).
 * - Accelerometer correction only when its norm is close to 1 g and the stick turns slowly:
 *   strokes read 10+ g, and at the butt the centripetal acceleration points along the stick,
 *   where it passes for a tilt (hits read +35 deg of elevation on 2026-11-06).
 */

#include "fusion.h"

#include <math.h>
#include <stdbool.h>

#include "imu.h"

/* LSM6DS3TR-C (docs/HARDWARE.md): 70 mdps/LSB at +-2000 dps, 0.488 mg/LSB at +-16 g. */
#define GYRO_DPS_PER_LSB 0.070f
#define ACCEL_G_PER_LSB 0.488e-3f
#define DEG_TO_RAD 0.017453293f

#define DT (1.0f / (float)IMU_ODR_HZ)
#define KP 1.0f
#define KP_START 10.0f
#define START_SAMPLES (IMU_ODR_HZ * 1u)
#define ACCEL_GATE_G 0.15f
#define ACCEL_GATE_DPS 30.0f
#define STILL_BLOCK 16u
#define STILL_BLOCKS ((IMU_ODR_HZ / 2u + STILL_BLOCK / 2u) / STILL_BLOCK) /* 0.5 s */
#define STILL_GYRO_DPS 1.5f
#define STILL_ACCEL_G 0.02f
#define STILL_NORM_G 0.1f
#define BIAS_ALPHA 0.005f /* per still block once calibrated */

static float q[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
static float bias_dps[3];
static bool bias_valid;
static uint32_t samples;

/* Rest detection: sums of the current block, then the last STILL_BLOCKS block means
 * (gx, gy, gz in dps, ax, ay, az in g) in a ring.
 */
static float block_sum[6];
static uint32_t block_count;
static float window[STILL_BLOCKS][6];
static uint32_t window_next;
static uint32_t window_fill;

static void track_rest(const float g[3], const float a[3])
{
	for (int i = 0; i < 3; i++) {
		block_sum[i] += g[i];
		block_sum[3 + i] += a[i];
	}
	if (++block_count < STILL_BLOCK) {
		return;
	}
	for (int i = 0; i < 6; i++) {
		window[window_next][i] = block_sum[i] / (float)STILL_BLOCK;
		block_sum[i] = 0.0f;
	}
	block_count = 0;
	window_next = (window_next + 1u) % STILL_BLOCKS;
	if (window_fill < STILL_BLOCKS) {
		window_fill++;
		return;
	}

	float mean[6] = { 0 };

	for (uint32_t b = 0; b < STILL_BLOCKS; b++) {
		for (int i = 0; i < 6; i++) {
			mean[i] += window[b][i] / (float)STILL_BLOCKS;
		}
	}
	for (uint32_t b = 0; b < STILL_BLOCKS; b++) {
		for (int i = 0; i < 6; i++) {
			const float tol = i < 3 ? STILL_GYRO_DPS : STILL_ACCEL_G;

			if (fabsf(window[b][i] - mean[i]) >= tol) {
				return;
			}
		}
	}
	if (fabsf(sqrtf(mean[3] * mean[3] + mean[4] * mean[4] + mean[5] * mean[5]) - 1.0f) >= STILL_NORM_G) {
		return;
	}
	for (int i = 0; i < 3; i++) {
		bias_dps[i] = bias_valid ? bias_dps[i] + BIAS_ALPHA * (mean[i] - bias_dps[i]) : mean[i];
	}
	bias_valid = true;
}

void fusion_process(const struct imu_sample *s)
{
	const float g[3] = { s->gx * GYRO_DPS_PER_LSB, s->gy * GYRO_DPS_PER_LSB, s->gz * GYRO_DPS_PER_LSB };
	const float a[3] = { s->ax * ACCEL_G_PER_LSB, s->ay * ACCEL_G_PER_LSB, s->az * ACCEL_G_PER_LSB };
	const float a_norm = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

	samples++;
	track_rest(g, a);

	float w[3];

	for (int i = 0; i < 3; i++) {
		w[i] = (g[i] - bias_dps[i]) * DEG_TO_RAD;
	}

	const float rate_sq = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
	const float gate_rad = ACCEL_GATE_DPS * DEG_TO_RAD;

	if (fabsf(a_norm - 1.0f) < ACCEL_GATE_G && rate_sq < gate_rad * gate_rad) {
		/* World up seen in the stick frame, crossed with the measured one. */
		const float vx = 2.0f * (q[1] * q[3] - q[0] * q[2]);
		const float vy = 2.0f * (q[0] * q[1] + q[2] * q[3]);
		const float vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];
		const float ax = a[0] / a_norm, ay = a[1] / a_norm, az = a[2] / a_norm;
		const float kp = samples < START_SAMPLES ? KP_START : KP;

		w[0] += kp * (ay * vz - az * vy);
		w[1] += kp * (az * vx - ax * vz);
		w[2] += kp * (ax * vy - ay * vx);
	}

	/* q += 0.5 * q * (0, w) * dt */
	const float qw = q[0], qx = q[1], qy = q[2], qz = q[3];
	const float h = 0.5f * DT;

	q[0] = qw + h * (-qx * w[0] - qy * w[1] - qz * w[2]);
	q[1] = qx + h * (qw * w[0] + qy * w[2] - qz * w[1]);
	q[2] = qy + h * (qw * w[1] - qx * w[2] + qz * w[0]);
	q[3] = qz + h * (qw * w[2] + qx * w[1] - qy * w[0]);

	const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

	for (int i = 0; i < 4; i++) {
		q[i] /= n;
	}
}

static int16_t to_q14(float v)
{
	const float scaled = v * 16384.0f;

	return scaled >= 32767.0f ? 32767 : (scaled <= -32768.0f ? -32768 : (int16_t)lroundf(scaled));
}

void fusion_get_quat_q14(int16_t quat[4])
{
	for (int i = 0; i < 4; i++) {
		quat[i] = to_q14(q[i]);
	}
}

void fusion_get_bias(int16_t bias[3])
{
	for (int i = 0; i < 3; i++) {
		bias[i] = (int16_t)lroundf(bias_dps[i] / GYRO_DPS_PER_LSB);
	}
}
