import numpy as np
import pytest

import detect_hits
from detection.detector import DetectorConfig, HitDetector, VelocityModel, detect_session, stroke_rate
from detection.evaluate import score
from detection.imu_sim import INT16_MAX, simulate_session


def run(mode, realistic, seed=7, duration=40.0):
    calib = simulate_session(duration_s=20, mode=mode, seed=1, realistic=realistic)
    model = VelocityModel().fit(detect_session(calib.gyro, calib.t_s))
    test = simulate_session(duration_s=duration, mode=mode, seed=seed, bpm=110, realistic=realistic)
    detections = [model.apply(d) for d in detect_session(test.gyro, test.t_s)]
    return test, detections, score(test.hits, detections, test.fakes)


@pytest.mark.parametrize("mode", ["air", "pad"])
def test_clean_strokes_are_all_found_with_accurate_velocity(mode):
    _, _, s = run(mode, realistic=False)
    assert s.recall == 1.0
    assert s.precision == 1.0
    assert s.fake_triggers == 0
    assert s.velocity_correlation > 0.98
    # Fires while the stick is being braked, before it stops: never late.
    assert np.all(s.latency_ms < 0) and np.median(s.latency_ms) > -12


@pytest.mark.parametrize("mode", ["air", "pad"])
def test_realistic_strokes_keep_high_recall_and_precision(mode):
    _, _, s = run(mode, realistic=True)
    assert s.recall > 0.97
    assert s.precision > 0.97
    assert s.velocity_correlation > 0.97
    assert s.velocity_error_midi < 8


def test_saturated_strokes_still_get_a_velocity():
    test, detections, s = run("air", realistic=False, duration=60.0)
    saturated = [d for d in detections if d.saturated]
    assert len(saturated) > 10  # the simulation reaches 2800 dps, above the +-2000 dps scale
    # Their estimate goes above what the clipped peak alone would give.
    assert max(d.velocity for d in saturated) > 2000 / 2800 + 0.05


def test_simulated_gyro_clips_like_the_sensor():
    session = simulate_session(duration_s=10, seed=3)
    assert session.gyro.dtype == np.int16
    assert session.gyro[:, 1].max() >= INT16_MAX - 1


def test_detector_ignores_slow_moves_and_respects_refractory():
    detector = HitDetector(DetectorConfig(on_dps=260, refractory_s=0.04))
    fired = []
    t = 0.0
    # A slow move (150 dps) never fires; a fast stroke fires once even if it wobbles after.
    for rate in [150] * 50 + list(np.linspace(0, 1500, 30)) + list(np.linspace(1500, 0, 10)) + [300, 0] * 5:
        d = detector.process(t, float(rate), False)
        if d is not None:
            fired.append(d)
        t += 1 / 1666
    assert len(fired) == 1
    assert fired[0].peak_dps == pytest.approx(1500)


def test_stroke_rate_combines_the_stroke_plane():
    gyro = np.array([[0, 3000, 4000]], dtype=np.int64)  # 210 dps on y, 280 dps on z
    rate, saturated = stroke_rate(gyro)
    assert rate[0] == pytest.approx(350.0, rel=1e-6)
    assert not saturated[0]


def test_cli_runs_on_a_logger_csv(tmp_path, capsys):
    import logger
    from vdrum import protocol as p

    session = simulate_session(duration_s=8, seed=5)
    writer = logger.SessionWriter(tmp_path, "sim", 1666.0)
    parser = p.FrameParser()
    for seq, start in enumerate(range(0, len(session.t_s) - 8, 8)):
        samples = tuple(p.ImuSample(*session.accel[i], *session.gyro[i]) for i in range(start, start + 8))
        packet = p.RawPacket(p.Header(p.PKT_RAW, 0, seq & 0xFFFF), int(session.t_s[start] * 1e6), samples)
        for frame in parser.feed(p.encode_frame(p.Frame(0, -40, packet))):
            writer.write(frame)
    writer.close()

    assert detect_hits.main(["--csv", str(writer.imu_path), "--stick", "0"]) == 0
    lines = [line for line in capsys.readouterr().out.splitlines() if line and not line.startswith("time_s")]
    assert abs(len(lines) - len(session.hits)) <= 1


def test_a_gap_in_the_recording_does_not_inflate_the_slope():
    detector = HitDetector()
    detector.process(0.0, 0.0, False)
    detector.process(0.0006, 10.0, False)
    # 8 samples lost: the next sample is 5.4 ms later, much faster; slope across the gap ignored.
    detector.process(0.0060, 900.0, False)
    d = None
    t = 0.0066
    for rate in (950.0, 1000.0, 100.0):
        d = detector.process(t, rate, False) or d
        t += 0.0006
    assert d is not None
    assert d.max_accel_dps2 < 50.0 / 0.0006 * 1.01


def test_scoring_counts_each_fake_trigger_once():
    from detection.detector import Detection
    from detection.imu_sim import Hit

    hits = [Hit(1.0, 1000.0, 0.5)]
    detections = [Detection(1.001, 1000.0, 1e5, False, 0.5), Detection(2.0, 300.0, 1e4, False, 0.1)]
    s = score(hits, detections, fakes=[1.98, 2.0, 2.02])
    assert s.matched == 1 and s.false_positives == 1 and s.fake_triggers == 1
