import math

import numpy as np
import pandas as pd
import pytest

import logger
import plot_session
from vdrum import protocol as p


def write_session(tmp_path, lose_seq=None, saturate_gyro_at=None, t0_start=0):
    """Two sticks, 1600 Hz, 8 samples per packet, a swing on gz that may hit full scale."""
    writer = logger.SessionWriter(tmp_path, "test", 1600.0)
    parser = p.FrameParser()
    for stick in (0, 1):
        for seq in range(200):
            if stick == 0 and seq == lose_seq:
                continue
            t0 = (t0_start + seq * 5000) & 0xFFFFFFFF
            samples = []
            for i in range(8):
                n = seq * 8 + i
                gz = int(20000 * math.sin(n / 50.0))
                if saturate_gyro_at is not None and stick == 1 and n == saturate_gyro_at:
                    gz = 32767
                samples.append(p.ImuSample(100, -200, 2048, 0, 0, gz))
            packet = p.RawPacket(p.Header(p.PKT_RAW, stick, seq), t0, tuple(samples))
            for frame in parser.feed(p.encode_frame(p.Frame(t0, -40, packet))):
                writer.write(frame)
    writer.close()
    return writer.imu_path


def test_stats_report_rate_losses_and_saturation(tmp_path):
    path = write_session(tmp_path, lose_seq=50, saturate_gyro_at=333)
    frame = plot_session.load_session(path)

    left = plot_session.stick_stats(frame, 0)
    assert left.packets_lost == 1
    assert left.samples == 199 * 8
    assert left.gyro_saturated == 0
    assert left.sample_rate_hz == pytest.approx(1600, rel=0.01)

    right = plot_session.stick_stats(frame, 1)
    assert right.packets_lost == 0
    assert right.gyro_saturated == 1
    assert right.accel_saturated == 0


def test_time_unwraps_across_the_u32_wrap():
    t = pd.Series([0xFFFFFF00, 0xFFFFFFF0, 0x00000010, 0x00000100])
    assert np.allclose(plot_session.unwrap_time_s(t) * 1e6, [0, 0xF0, 0x110, 0x200])


def test_plot_is_written_to_a_file(tmp_path, capsys):
    path = write_session(tmp_path, saturate_gyro_at=100, t0_start=0xFFFF0000)
    out = tmp_path / "session.png"
    assert plot_session.main([str(path), "--out", str(out)]) == 0
    assert out.exists() and out.stat().st_size > 10_000
    printed = capsys.readouterr().out
    assert "stick 0:" in printed and "stick 1:" in printed


def test_wrong_file_is_rejected(tmp_path):
    bad = tmp_path / "events.csv"
    bad.write_text("rx_time_us,type\n1,HIT\n")
    with pytest.raises(ValueError):
        plot_session.load_session(bad)


def test_loss_spans_cover_the_missing_packet(tmp_path):
    frame = plot_session.load_session(write_session(tmp_path, lose_seq=50))
    data = frame[frame["stick_id"] == 0]
    spans = plot_session.loss_spans(data, plot_session.unwrap_time_s(data["t_us"]))
    assert len(spans) == 1
    start, end = spans[0]
    assert start == pytest.approx(49 * 0.005 + 7 / 1600, abs=1e-4)
    assert end == pytest.approx(51 * 0.005, abs=1e-4)


def write_long_session(tmp_path, lost_in_second_lap=(), status_every=208):
    """One stick over two laps of the u16 seq, with STATUS packets taking seqs (as on the stick)."""
    writer = logger.SessionWriter(tmp_path, "long", 1666.0)
    lost = set(lost_in_second_lap)
    sample = p.ImuSample(0, 0, 2048, 0, 0, 0)
    t = 0
    for seq_index in range(2 * 65536 + 100):
        seq = seq_index & 0xFFFF
        t += 4800
        rx = t & 0xFFFFFFFF
        if seq_index % status_every == 0:
            packet = p.StatusPacket(p.Header(p.PKT_STATUS, 0, seq), 3700, (0, 0, 0), 2)
        elif seq_index >= 65536 and seq_index in lost:
            continue
        else:
            packet = p.RawPacket(p.Header(p.PKT_RAW, 0, seq), rx, (sample,))
        writer.write(p.Frame(rx, -40, packet))
    writer.close()
    return writer.imu_path


def test_losses_survive_the_seq_wrap_and_ignore_status_packets(tmp_path):
    lost = [70000, 70001, 90000, 100000, 120000, 121000, 125000, 130003, 131000, 131001]  # none on a STATUS (multiple of 208)
    path = write_long_session(tmp_path, lost_in_second_lap=lost)
    frame = plot_session.load_session(path)
    stats = plot_session.stick_stats(frame, 0, plot_session.load_events(path))
    assert stats.packets_lost == len(lost)
    expected_raw = 2 * 65536 + 100 - len(range(0, 2 * 65536 + 100, 208)) - len(lost)
    assert stats.packets == expected_raw


def test_without_events_status_packets_look_like_losses(tmp_path):
    path = write_long_session(tmp_path, status_every=1000)
    frame = plot_session.load_session(path)
    assert plot_session.stick_stats(frame, 0).packets_lost == len(range(1000, 2 * 65536 + 100, 1000))
