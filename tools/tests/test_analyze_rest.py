import numpy as np
import pytest

import analyze_rest
import logger
from vdrum import protocol as p

ODR_HZ = 1666.0
PERIOD_US = 600


def write_rest_session(tmp_path, gyro_raw=(10, -20, 30), receiver_ppm=0.0, lose_seq=None, packets=4000):
    """One stick lying flat (1 g on z), constant gyro counts, receiver clock off by receiver_ppm."""
    writer = logger.SessionWriter(tmp_path, "rest", ODR_HZ)
    parser = p.FrameParser()
    for seq in range(packets):
        if seq == lose_seq:
            continue
        t0 = seq * 8 * PERIOD_US
        rx = int(t0 * (1.0 + receiver_ppm * 1e-6)) + 1000
        samples = tuple(p.ImuSample(0, 0, 2049, *gyro_raw) for _ in range(8))
        packet = p.RawPacket(p.Header(p.PKT_RAW, 0, seq), t0, samples)
        for frame in parser.feed(p.encode_frame(p.Frame(rx, -50, packet))):
            writer.write(frame)
    writer.close()
    return writer.imu_path


def test_counter_delta_handles_the_u16_wrap():
    assert analyze_rest.counter_delta(np.array([65530, 65535, 4])) == 10
    assert analyze_rest.counter_delta(np.array([7])) == 0


def test_clock_rate_counts_lost_samples_and_receiver_drift(tmp_path):
    path = write_rest_session(tmp_path, receiver_ppm=50.0, lose_seq=100)
    clock = analyze_rest.analyse(path, calibration_s=2.0)[0]["clock"]

    assert clock["rate_stick_clock_hz"] == pytest.approx(1e6 / PERIOD_US, rel=1e-4)
    assert clock["stick_vs_receiver_ppm"] == pytest.approx(50.0, abs=1.0)


def test_sensors_bias_noise_and_no_yaw_drift_after_calibration(tmp_path):
    path = write_rest_session(tmp_path, gyro_raw=(10, -20, 30))
    result = analyze_rest.analyse(path, calibration_s=2.0)[0]
    sensors = result["sensors"]

    assert sensors["accel_norm_g"] == pytest.approx(2049 * analyze_rest.ACCEL_G_PER_LSB, rel=1e-6)
    assert sensors["gyro_bias_dps"] == pytest.approx([0.7, -1.4, 2.1], abs=1e-6)
    assert sensors["gyro_noise_dps"] == pytest.approx([0.0, 0.0, 0.0], abs=1e-9)
    assert sensors["yaw_drift_deg"] == pytest.approx(0.0, abs=1e-9)  # constant bias is calibrated out
    assert result["link"]["lost"] == 0


def test_stale_packets_at_the_start_are_dropped(tmp_path):
    writer = logger.SessionWriter(tmp_path, "stale", ODR_HZ)
    parser = p.FrameParser()
    samples = tuple(p.ImuSample(0, 0, 2049, 0, 0, 0) for _ in range(8))
    # 5 packets held by the receiver from two minutes earlier, then the live stream.
    stream = [(seq, seq * 4800, 1000 + seq * 4800) for seq in range(5)]
    stream += [(25000 + seq, 120_000_000 + seq * 4800, 120_000_000 + seq * 4800) for seq in range(2000)]
    for seq, t0, rx in stream:
        packet = p.RawPacket(p.Header(p.PKT_RAW, 0, seq), t0, samples)
        for frame in parser.feed(p.encode_frame(p.Frame(rx, -50, packet))):
            writer.write(frame)
    writer.close()

    link = analyze_rest.analyse(writer.imu_path, calibration_s=2.0)[0]["link"]
    assert link["packets"] == 2000
    assert link["lost"] == 0
