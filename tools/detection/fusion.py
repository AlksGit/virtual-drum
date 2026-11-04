"""Streaming orientation fusion for one stick (Mahony, proportional only), written to be ported
to the firmware as is: one sample at a time, a few state variables.

Frames (docs/PROTOCOL.md): the quaternion takes the stick frame (x to the tip, y left, z up when
lying flat, LEDs up) to the world frame (x forward, y left, z up). The accelerometer measures the
specific force, +1 g along world z at rest, so gravity pulls pitch and roll back; yaw has no
reference and drifts with the residual gyro bias.

- Gyro bias: averaged while the stick is still (gyro and accelerometer quiet for STILL_S), so
  each pause recalibrates it (docs/ARCHITECTURE.md, yaw drift strategy 1).
- Accelerometer correction only when its norm is close to 1 g: during a stroke it reads
  10+ g of centripetal acceleration (docs/MEASUREMENTS.md, 2026-11-03), so the fusion runs on
  the gyro alone through strokes.
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
    still_gyro_dps: float = 6.0  # per axis, bias included (bias up to ~2.2 dps measured)
    still_accel_g: float = 0.05
    still_s: float = 0.5
    bias_alpha: float = 0.002    # bias average while still (~0.3 s time constant)


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
        self._still_samples = 0
        self._samples = 0

    def process(self, gyro_dps, accel_g) -> tuple:
        c = self.config
        dt = 1.0 / c.odr_hz
        self._samples += 1

        a_norm = math.sqrt(sum(a * a for a in accel_g))
        quiet = (all(abs(g - b) < c.still_gyro_dps for g, b in zip(gyro_dps, self.bias))
                 and abs(a_norm - 1.0) < c.still_accel_g)
        self._still_samples = self._still_samples + 1 if quiet else 0
        if self._still_samples > c.still_s * c.odr_hz:
            alpha = c.bias_alpha if self.bias_valid else 1.0 / (self._still_samples - c.still_s * c.odr_hz)
            self.bias = [b + alpha * (g - b) for g, b in zip(gyro_dps, self.bias)]
            if self._still_samples > 2 * c.still_s * c.odr_hz:
                self.bias_valid = True

        w = [math.radians(g - b) for g, b in zip(gyro_dps, self.bias)]
        if abs(a_norm - 1.0) < c.accel_gate_g:
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
