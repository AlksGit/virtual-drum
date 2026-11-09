"""Streaming orientation fusion for one stick (Mahony, proportional only), written to be ported
to the firmware as is: one sample at a time, a few state variables.

Frames (docs/PROTOCOL.md): the quaternion takes the stick frame (x to the tip, y left, z up when
lying flat, LEDs up) to the world frame (x forward, y left, z up). The accelerometer measures the
specific force, +1 g along world z at rest, so gravity pulls pitch and roll back; yaw has no
reference and drifts with the residual gyro bias.

- Gyro bias: averaged while the stick is still, so each pause recalibrates it
  (docs/ARCHITECTURE.md, yaw drift strategy 1). Still = over STILL_S, the gyro and
  accelerometer means of blocks of STILL_BLOCK samples stay within a tolerance of their window
  mean. The test does not involve the current bias: the first version compared each sample with
  it, and on 2026-11-06 the left stick took a slow hand rotation for its bias (3.4 dps of yaw),
  after which its gyro noise (769 Hz line) failed that comparison and the bias never came back
  (3 deg/s of drift). The blocks also average that line out.
- Accelerometer correction only when its norm is close to 1 g and the stick turns slowly:
  during a stroke it reads 10+ g of centripetal acceleration (docs/MEASUREMENTS.md,
  2026-11-03). The norm alone let it in while the stick turned: the board sits at the butt,
  behind the grip, where the centripetal acceleration points along the stick and passes for a
  tilt. Played on 2026-11-06, hits then read +35 deg of elevation at the bottom of the
  strokes; with the rate gate the raw recordings of 2026-11-03 read 10-20 deg lower at the
  hits. So the fusion runs on the gyro alone through strokes.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .imu_sim import GYRO_LSB_PER_DPS, ODR_HZ

ACCEL_G_PER_LSB = 0.488e-3  # +-16 g: 0.488 mg/LSB (docs/HARDWARE.md)


@dataclass
class FusionConfig:
    odr_hz: float = ODR_HZ
    kp: float = 1.0              # accelerometer pull (1/s): ~1 s to settle pitch/roll
    kp_start: float = 10.0       # faster pull for the first START_S, from an identity start
    start_s: float = 1.0
    accel_gate_g: float = 0.15   # accelerometer used when | |a| - 1 g | < gate
    accel_gate_dps: float = 30.0  # ... and the bias-corrected rate is below this
    still_block: int = 16        # samples per block (~9.6 ms)
    still_gyro_dps: float = 1.5  # block means within this of the window mean, per axis
    still_accel_g: float = 0.02  # same for the accelerometer, per axis
    still_norm_g: float = 0.1    # and the accelerometer reads about 1 g
    still_s: float = 0.5
    bias_alpha: float = 0.005    # per still block once calibrated (~2 s time constant)


def quat_mul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def tip_angles(q):
    """Azimuth (positive to the left) and elevation of the stick tip, degrees."""
    w, x, y, z = q
    tip_x = 1 - 2 * (y * y + z * z)
    tip_y = 2 * (x * y + w * z)
    tip_z = 2 * (x * z - w * y)
    return math.degrees(math.atan2(tip_y, tip_x)), math.degrees(math.asin(max(-1.0, min(1.0, tip_z))))


class Mahony:
    def __init__(self, config: FusionConfig | None = None) -> None:
        self.config = config or FusionConfig()
        self.q = (1.0, 0.0, 0.0, 0.0)
        self.bias = [0.0, 0.0, 0.0]   # deg/s
        self.bias_valid = False
        self._samples = 0
        self._block_sum = [0.0] * 6
        self._block_count = 0
        self._window = []             # last block means (gx, gy, gz, ax, ay, az)

    def _track_rest(self, gyro_dps, accel_g) -> None:
        c = self.config
        for i, v in enumerate((*gyro_dps, *accel_g)):
            self._block_sum[i] += v
        self._block_count += 1
        if self._block_count < c.still_block:
            return
        block = [v / c.still_block for v in self._block_sum]
        self._block_sum, self._block_count = [0.0] * 6, 0
        self._window.append(block)
        blocks = round(c.still_s * c.odr_hz / c.still_block)
        if len(self._window) > blocks:
            self._window.pop(0)
        if len(self._window) < blocks:
            return
        mean = [sum(b[i] for b in self._window) / blocks for i in range(6)]
        tol = [c.still_gyro_dps] * 3 + [c.still_accel_g] * 3
        if any(abs(b[i] - mean[i]) >= tol[i] for b in self._window for i in range(6)):
            return
        if abs(math.sqrt(sum(m * m for m in mean[3:])) - 1.0) >= c.still_norm_g:
            return
        if self.bias_valid:
            self.bias = [b + c.bias_alpha * (m - b) for b, m in zip(self.bias, mean[:3])]
        else:
            self.bias = mean[:3]
            self.bias_valid = True

    def process(self, gyro_dps, accel_g) -> tuple:
        c = self.config
        dt = 1.0 / c.odr_hz
        self._samples += 1

        a_norm = math.sqrt(sum(a * a for a in accel_g))
        self._track_rest(gyro_dps, accel_g)

        w = [math.radians(g - b) for g, b in zip(gyro_dps, self.bias)]
        rate_dps = math.degrees(math.sqrt(sum(x * x for x in w)))
        if abs(a_norm - 1.0) < c.accel_gate_g and rate_dps < c.accel_gate_dps:
            qw, qx, qy, qz = self.q
            # world up (z) seen in the stick frame
            vx = 2 * (qx * qz - qw * qy)
            vy = 2 * (qw * qx + qy * qz)
            vz = qw * qw - qx * qx - qy * qy + qz * qz
            ax, ay, az = (a / a_norm for a in accel_g)
            ex, ey, ez = ay * vz - az * vy, az * vx - ax * vz, ax * vy - ay * vx
            kp = c.kp_start if self._samples < c.start_s * c.odr_hz else c.kp
            w = [w[0] + kp * ex, w[1] + kp * ey, w[2] + kp * ez]

        dq = quat_mul(self.q, (0.0, w[0], w[1], w[2]))
        q = [qi + 0.5 * dqi * dt for qi, dqi in zip(self.q, dq)]
        n = math.sqrt(sum(v * v for v in q))
        self.q = tuple(v / n for v in q)
        return self.q


def fuse_session(gyro_counts: np.ndarray, accel_counts: np.ndarray, config: FusionConfig | None = None):
    """Quaternions (n x 4) over a recording, raw counts as logged."""
    f = Mahony(config)
    out = np.empty((len(gyro_counts), 4))
    for i, (g, a) in enumerate(zip(gyro_counts / GYRO_LSB_PER_DPS, accel_counts * ACCEL_G_PER_LSB)):
        out[i] = f.process(g, a)
    return out
