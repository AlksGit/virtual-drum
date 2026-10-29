"""Streaming hit detector for one stick, written to be ported to the firmware as is:
one sample at a time, a few state variables, no buffers (docs/ARCHITECTURE.md, detection).

Input per sample: the pitch rate (deg/s, positive = tip going down) and whether the gyro axis
was saturated. A hit fires when, after a downstroke faster than `on_dps`, the rate falls below
`release_fraction` x its peak: the stick is being braked, a few milliseconds before it stops,
which hides part of the system latency. A refractory period prevents double triggers.

Velocity: the peak rate when the gyro did not saturate. When it did, the peak is unknown, so
the strongest angular acceleration of the downstroke (measured while still below saturation)
predicts it, through a calibration fitted on unsaturated strokes (`VelocityModel`).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from vdrum.protocol import saturated as is_saturated

from .imu_sim import GYRO_LSB_PER_DPS, ODR_HZ


@dataclass
class DetectorConfig:
    odr_hz: float = ODR_HZ
    on_dps: float = 260.0           # a downstroke starts above this rate (ghost notes vs twitches)
    release_fraction: float = 0.35  # fire when the rate drops below this fraction of the peak
    refractory_s: float = 0.04
    max_speed_dps: float = 2800.0   # rate mapped to velocity 1.0


@dataclass
class Detection:
    time_s: float
    peak_dps: float          # measured peak (clipped when saturated)
    max_accel_dps2: float    # strongest angular acceleration during the rise
    saturated: bool
    velocity: float = 0.0    # 0..1, filled by VelocityModel


class HitDetector:
    def __init__(self, config: DetectorConfig | None = None) -> None:
        self.config = config or DetectorConfig()
        self._in_stroke = False
        self._peak = 0.0
        self._max_accel = 0.0
        self._saturated = False
        self._previous = 0.0
        self._previous_time = None
        self._refractory_until = -1.0

    def process(self, time_s: float, rate_dps: float, saturated: bool) -> Detection | None:
        """Feeds one sample; returns a Detection when a hit fires."""
        c = self.config
        # Angular acceleration over the real time step: recordings have gaps (lost packets),
        # where a fixed 1/odr step would overestimate the slope.
        dt = 1.0 / c.odr_hz if self._previous_time is None else time_s - self._previous_time
        accel = (rate_dps - self._previous) / dt if dt > 0 else 0.0
        if dt > 2.5 / c.odr_hz:
            accel = 0.0  # gap: the slope across it is unknown
        self._previous = rate_dps
        self._previous_time = time_s

        if time_s < self._refractory_until:
            return None

        if not self._in_stroke:
            if rate_dps > c.on_dps:
                self._in_stroke = True
                self._peak = rate_dps
                self._max_accel = max(accel, 0.0)
                self._saturated = saturated
            return None

        if rate_dps >= self._peak:
            self._peak = rate_dps
            if not saturated:  # once clipped, the slope is meaningless
                self._max_accel = max(self._max_accel, accel)
        self._saturated = self._saturated or saturated

        if rate_dps < c.release_fraction * self._peak:
            self._in_stroke = False
            self._refractory_until = time_s + c.refractory_s
            return Detection(time_s, self._peak, self._max_accel, self._saturated)

        return None


class VelocityModel:
    """Peak rate to velocity; for saturated strokes the peak is predicted from the rise
    acceleration with a power law peak = k * accel^p fitted on unsaturated strokes."""

    def __init__(self, max_speed_dps: float = 2800.0) -> None:
        self.max_speed_dps = max_speed_dps
        self.k = None
        self.p = None

    def fit(self, detections) -> "VelocityModel":
        clean = [d for d in detections if not d.saturated and d.max_accel_dps2 > 0]
        if len(clean) < 5:
            raise ValueError("need at least 5 unsaturated strokes to calibrate")
        x = np.log([d.max_accel_dps2 for d in clean])
        y = np.log([d.peak_dps for d in clean])
        self.p, log_k = np.polyfit(x, y, 1)
        self.k = float(np.exp(log_k))
        return self

    def apply(self, detection: Detection) -> Detection:
        peak = detection.peak_dps
        if detection.saturated and self.k is not None and detection.max_accel_dps2 > 0:
            peak = max(peak, self.k * detection.max_accel_dps2 ** self.p)
        detection.velocity = float(np.clip(peak / self.max_speed_dps, 0.0, 1.0))
        return detection


def stroke_rate(gyro_counts: np.ndarray, axis: int = 1, other_axis: int | None = 2, sign: float = 1.0):
    """Downstroke rate in deg/s and per-sample saturation flags.

    Strokes rotate around an axis close to `axis` (pitch); a wrist that is not straight tilts it
    towards `other_axis`, so the rate is the norm in that plane, signed by the main axis.
    """
    main = gyro_counts[:, axis].astype(float) / GYRO_LSB_PER_DPS
    saturated = is_saturated(gyro_counts[:, axis].astype(int))
    if other_axis is None:
        return sign * main, saturated
    other = gyro_counts[:, other_axis].astype(float) / GYRO_LSB_PER_DPS
    saturated |= is_saturated(gyro_counts[:, other_axis].astype(int))
    return sign * np.sign(main) * np.hypot(main, other), saturated


def detect_session(gyro_counts: np.ndarray, t_s: np.ndarray, axis: int = 1, sign: float = 1.0,
                   config: DetectorConfig | None = None, other_axis: int | None = 2) -> list:
    """Runs the detector over a recording (gyro in raw counts, as logged)."""
    detector = HitDetector(config)
    rate, saturated = stroke_rate(gyro_counts, axis, other_axis, sign)
    found = []
    for time_s, r, s in zip(t_s, rate, saturated):
        d = detector.process(float(time_s), float(r), bool(s))
        if d is not None:
            found.append(d)
    return found
