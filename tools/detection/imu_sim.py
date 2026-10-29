"""Simulated drumstick IMU recordings with ground truth, to prototype hit detection offline.

The stick pivots at the wrist. A stroke is: an upstroke (tip goes up), a pause at the top, a
fast downstroke, then either
  - "air": the wrist brakes the stick sharply (virtual drum, no surface), or
  - "pad": the stick hits a practice pad (impact shock, rebound).
Between strokes there may be "fake" gestures: fast moves that are braked without being hits.

The sensor sits near the grip. Outputs are what the logger records: raw int16 counts at the
IMU rate, in the stick frame (X along the stick towards the tip, Y to the left, Z up when the
stick is level), with noise and the +-16 g / +-2000 dps full scales (values clip at int16).
Downstrokes rotate the tip downwards, i.e. a positive rotation rate around +Y.

Ground truth per hit: the time at which the tip stops (end of the stroke, when a real drum
would sound) and the peak angular speed of the downstroke.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

ODR_HZ = 1666.0
GYRO_LSB_PER_DPS = 1.0 / 0.070     # +-2000 dps: 70 mdps/LSB (docs/HARDWARE.md)
ACCEL_LSB_PER_G = 1.0 / 0.000488   # +-16 g: 0.488 mg/LSB
INT16_MAX = 32767
SENSOR_RADIUS_M = 0.04             # sensor distance from the wrist pivot
G = 9.81


@dataclass
class Hit:
    time_s: float             # tip stops: when the sound should start
    peak_speed_dps: float     # true peak angular speed of the downstroke (before clipping)
    velocity: float           # 0..1, the expressiveness target (peak speed / max speed)


@dataclass
class Session:
    t_s: np.ndarray
    accel: np.ndarray         # (n, 3) int16 counts
    gyro: np.ndarray          # (n, 3) int16 counts
    hits: list = field(default_factory=list)
    fakes: list = field(default_factory=list)   # times of braked non-hit gestures
    mode: str = "air"


def _smooth_pulse(t: np.ndarray, start: float, duration: float) -> np.ndarray:
    """Raised-cosine bump of unit area over [start, start + duration]."""
    x = (t - start) / duration
    bump = np.where((x >= 0) & (x <= 1), 1.0 - np.cos(2 * np.pi * x), 0.0)
    return bump / duration


def simulate_session(
    duration_s: float = 30.0,
    bpm: float = 100.0,
    mode: str = "air",
    seed: int = 1,
    max_speed_dps: float = 2800.0,
    fake_rate: float = 0.1,
    realistic: bool = False,
) -> Session:
    """A session of strokes on eighth notes with random velocities; max_speed above 2000 dps
    means the loudest strokes saturate the gyroscope.

    realistic=True makes it harder: each stroke rotates around an axis tilted up to 30 degrees
    from +Y (part of the motion shows up on the Z axis), a third of the beats are fast doubles
    (sixteenths), ghost notes go down to 0.08 velocity, braking is softer and noise larger."""
    rng = np.random.default_rng(seed)
    n = int(duration_s * ODR_HZ)
    t = np.arange(n) / ODR_HZ
    rate = np.zeros(n)          # pitch rate around +Y, deg/s (positive = tip goes down)
    yaw_rate = np.zeros(n)      # small sideways motion, deg/s
    impact_shock = np.zeros(n)  # extra acceleration along -Z at pad impacts, in g

    base_period = 60.0 / bpm / 2.0
    hits, fakes = [], []
    start = 0.5
    while start + base_period < duration_s - 0.5:
        period = base_period / 2.0 if realistic and rng.random() < 0.33 else base_period
        velocity = float(rng.uniform(0.08, 1.0) if realistic else rng.uniform(0.15, 1.0))
        tilt = np.radians(rng.uniform(-30, 30)) if realistic else 0.0
        stroke = np.zeros(n)
        peak = velocity * max_speed_dps
        # Faster strokes are also shorter; the downstroke takes up the second half of the beat.
        down = period * rng.uniform(0.40, 0.50) * (1.0 - 0.3 * velocity)
        up = period * 0.35
        brake = rng.uniform(0.010, 0.030 if realistic else 0.018)  # wrist braking time
        down_start = start + period - down - brake
        stop = down_start + down + brake

        # Upstroke: integral equals the angle lost on the way down (rotation back up).
        angle_deg = peak * down * 0.6
        stroke -= angle_deg * _smooth_pulse(t, start, up)

        # Downstroke: speed rises to the peak (sin^2 shape), then the brake brings it to zero.
        in_down = (t >= down_start) & (t < down_start + down)
        x = (t[in_down] - down_start) / down
        stroke[in_down] += peak * np.sin(0.5 * np.pi * x) ** 2
        in_brake = (t >= down_start + down) & (t < stop)
        y = (t[in_brake] - down_start - down) / brake
        stroke[in_brake] += peak * np.cos(0.5 * np.pi * y) ** 2

        if mode == "pad":
            # Rebound after the impact, and a short ringing shock seen by the accelerometer.
            rebound = (t >= stop) & (t < stop + 0.04)
            r = (t[rebound] - stop) / 0.04
            stroke[rebound] -= 0.35 * peak * np.sin(np.pi * r)
            shock = (t >= stop) & (t < stop + 0.006)
            s = t[shock] - stop
            impact_shock[shock] += (6 + 24 * velocity) * np.exp(-s / 0.0015) * np.cos(2 * np.pi * 900 * s)

        # The stroke's rotation axis, tilted around X: part of it appears on the Z axis.
        rate += np.cos(tilt) * stroke
        yaw_rate += np.sin(tilt) * stroke
        yaw_rate += rng.normal(0, 40) * _smooth_pulse(t, start, period) * period

        hits.append(Hit(time_s=stop, peak_speed_dps=peak, velocity=velocity))

        # Occasional fake: a small downward twitch braked in the air (100-300 dps, below the
        # softest stroke at 0.15 x max speed), which must not trigger a hit.
        if rng.random() < fake_rate:
            f_start = start + 0.15 * period
            f_peak = rng.uniform(100, 300)
            rate += f_peak * 0.5 * 0.06 * _smooth_pulse(t, f_start, 0.06)  # peak value = f_peak
            fakes.append(f_start + 0.03)

        start += period

    # Orientation (pitch) from the rate, starting level-ish, for the gravity direction.
    pitch_deg = np.cumsum(rate) / ODR_HZ - 10.0
    pitch = np.radians(pitch_deg)
    omega = np.radians(rate)
    alpha = np.gradient(omega, 1.0 / ODR_HZ)

    # Specific force in the stick frame: gravity (+1 g up) rotated by the pitch, plus the
    # tangential and centripetal accelerations of a sensor at SENSOR_RADIUS from the pivot.
    ax = -np.sin(pitch) * 1.0 - omega**2 * SENSOR_RADIUS_M / G
    az = np.cos(pitch) * 1.0 - alpha * SENSOR_RADIUS_M / G - impact_shock
    ay = np.zeros(n)

    accel = np.stack([ax, ay, az], axis=1) * ACCEL_LSB_PER_G
    gyro = np.stack([np.zeros(n), rate, yaw_rate], axis=1) * GYRO_LSB_PER_DPS

    noise_scale = 3.0 if realistic else 1.0
    accel += rng.normal(0, 4.0 * noise_scale, accel.shape)  # ~2 mg of noise (x3 if realistic)
    gyro += rng.normal(0, 3.0 * noise_scale, gyro.shape)    # ~0.2 dps of noise (x3 if realistic)

    clip = lambda a: np.clip(np.round(a), -INT16_MAX - 1, INT16_MAX).astype(np.int16)  # noqa: E731
    return Session(t_s=t, accel=clip(accel), gyro=clip(gyro), hits=hits, fakes=fakes, mode=mode)
