/*
 * Mahony fusion, ported from tools/detection/fusion.py (Mahony.process); constants are the
 * FusionConfig defaults, see that file for the reasoning.
 *
 * - Gyro bias averaged while the stick is still, so each pause recalibrates it.
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
#define STILL_GYRO_DPS 6.0f
#define STILL_ACCEL_G 0.05f
#define STILL_SAMPLES (IMU_ODR_HZ / 2u)
#define BIAS_ALPHA 0.002f

static float q[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
static float bias_dps[3];
static bool bias_valid;
static uint32_t still_samples;
static uint32_t samples;

void fusion_process(const struct imu_sample *s)
{
	const float g[3] = { s->gx * GYRO_DPS_PER_LSB, s->gy * GYRO_DPS_PER_LSB, s->gz * GYRO_DPS_PER_LSB };
	const float a[3] = { s->ax * ACCEL_G_PER_LSB, s->ay * ACCEL_G_PER_LSB, s->az * ACCEL_G_PER_LSB };
	const float a_norm = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

	samples++;

	bool quiet = fabsf(a_norm - 1.0f) < STILL_ACCEL_G;

	for (int i = 0; i < 3; i++) {
		quiet = quiet && fabsf(g[i] - bias_dps[i]) < STILL_GYRO_DPS;
	}
	still_samples = quiet ? still_samples + 1 : 0;
	if (still_samples > STILL_SAMPLES) {
		/* First pause: running mean, then a slow average. */
		const float alpha = bias_valid ? BIAS_ALPHA : 1.0f / (float)(still_samples - STILL_SAMPLES);

		for (int i = 0; i < 3; i++) {
			bias_dps[i] += alpha * (g[i] - bias_dps[i]);
		}
		if (still_samples > 2 * STILL_SAMPLES) {
			bias_valid = true;
		}
	}

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
