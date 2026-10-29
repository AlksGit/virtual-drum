import csv

import logger
from vdrum import protocol as p


def raw_frame(stick, seq, count, t0):
    samples = tuple(p.ImuSample(seq % 1000, stick, i, 0, 0, i * 10) for i in range(count))
    packet = p.RawPacket(p.Header(p.PKT_RAW, stick, seq), t0, samples)
    return p.encode_frame(p.Frame(t0 + 1500, -45, packet))


def read_csv(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def test_replay_writes_one_row_per_sample_and_counts_losses(tmp_path):
    stream = bytearray()
    for seq in range(10):
        if seq == 6:
            continue  # one packet lost on stick 0
        stream += raw_frame(0, seq, 8, 10_000 + seq * 4800)
    for seq in range(4):
        stream += raw_frame(1, seq, 8, 20_000 + seq * 4800)
    hit = p.HitPacket(p.Header(p.PKT_HIT, 1, 4), 30_000, 90, (16384, 0, 0, 0), 0)
    stream += b"\x00\x13garbage" + p.encode_frame(p.Frame(31_000, -40, hit))

    capture = tmp_path / "capture.bin"
    capture.write_bytes(bytes(stream))
    out = tmp_path / "out"

    assert logger.main(["--replay", str(capture), "--out", str(out), "--odr", "1600"]) == 0

    imu_files = list(out.glob("session_*_imu.csv"))
    events_files = list(out.glob("session_*_events.csv"))
    assert len(imu_files) == 1 and len(events_files) == 1

    rows = read_csv(imu_files[0])
    assert list(rows[0].keys()) == logger.IMU_COLUMNS
    assert len(rows) == (9 + 4) * 8

    stick0 = [r for r in rows if r["stick_id"] == "0"]
    assert sorted({int(r["seq"]) for r in stick0}) == [0, 1, 2, 3, 4, 5, 7, 8, 9]
    # Per-sample timestamps: t0 + index / ODR (1600 Hz -> 625 us).
    first = [r for r in stick0 if r["seq"] == "0"]
    assert [int(r["t_us"]) for r in first] == [10_000 + i * 625 for i in range(8)]
    assert [int(r["gz"]) for r in first] == [i * 10 for i in range(8)]

    events = read_csv(events_files[0])
    assert len(events) == 1
    assert events[0]["type"] == "HIT"
    assert events[0]["velocity"] == "90"


def test_session_writer_counts_seq_wrap_without_loss(tmp_path):
    writer = logger.SessionWriter(tmp_path, "wrap", 1666.0)
    for seq in (65534, 65535, 0, 1):
        frame = p.FrameParser().feed(raw_frame(0, seq, 1, 0))[0]
        writer.write(frame)
    writer.close()
    assert writer.lost.get(0, 0) == 0


def test_missing_receiver_is_reported_without_creating_files(tmp_path, monkeypatch):
    monkeypatch.setattr(logger, "find_receiver_port", lambda: None)
    assert logger.main(["--out", str(tmp_path / "out"), "--duration", "0.1"]) == 1
    assert not (tmp_path / "out").exists()


def test_unopenable_port_is_reported_without_creating_files(tmp_path):
    assert logger.main(["--port", "vdrum-missing-port", "--out", str(tmp_path / "out")]) == 1
    assert not (tmp_path / "out").exists()
