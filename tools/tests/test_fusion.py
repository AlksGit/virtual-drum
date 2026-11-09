"""Gyro bias calibration of the orientation fusion (tools/detection/fusion.py)."""

import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from detection.fusion import Mahony  # noqa: E402

ODR = 1666


def run(f, seconds, gyro, noise_dps=0.0, line_dps=0.0, seed=0):
    rng = np.random.default_rng(seed)
    for i in range(int(seconds * ODR)):
        line = line_dps * math.sin(2 * math.pi * 769 * i / ODR)
        g = [v + rng.normal(0, noise_dps) for v in gyro]
        g[2] += line
        f.process(g, (0.0, 0.0, 1.0))


def test_bias_found_at_rest_despite_the_769_hz_line():
    f = Mahony()
    run(f, 2.0, (-0.07, -1.4, 0.49), noise_dps=0.5, line_dps=2.5)
    assert np.allclose(f.bias, (-0.07, -1.4, 0.49), atol=0.1)


def test_wrong_bias_comes_back_at_rest():
    # 2026-11-06, left stick: a slow hand rotation taken for the bias locked it at 3.4 dps.
    f = Mahony()
    run(f, 1.0, (-0.6, -0.6, 3.4))
    assert f.bias[2] > 3.0
    run(f, 10.0, (-0.07, -1.4, 0.49), noise_dps=0.5, line_dps=2.5)
    assert np.allclose(f.bias, (-0.07, -1.4, 0.49), atol=0.1)


def test_no_calibration_while_turning():
    f = Mahony()
    turning = [(0.0, 0.0, 40.0 * math.sin(2 * math.pi * 2 * i / ODR)) for i in range(2 * ODR)]
    for g in turning:
        f.process(g, (0.0, 0.0, 1.0))
    assert not f.bias_valid
