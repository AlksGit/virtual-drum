import re
from pathlib import Path

import pytest

from vdrum import protocol as p

PROTOCOL_H = Path(__file__).resolve().parents[2] / "firmware" / "common" / "protocol.h"

# Reference frame from docs/PROTOCOL.md (shared with the C++ tests).
REFERENCE_FRAME = bytes.fromhex("A5 5A 12 DD CC BB AA D8 02 01 34 12 04 03 02 01 64 00 40 FF FF 00 00 02 01 01 32")


def header_defines():
    text = PROTOCOL_H.read_text()
    defines = {}
    for name, value in re.findall(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b", text):
        defines[name] = int(value, 0)
    for name, value in re.findall(r"(PKT_\w+)\s*=\s*(0x[0-9A-Fa-f]+)", text):
        defines[name] = int(value, 0)
    return defines


@pytest.mark.parametrize("name", [
    "PROTO_VERSION", "PKT_RAW", "PKT_HIT", "PKT_ORIENT", "PKT_STATUS", "RAW_MAX_SAMPLES",
    "QUAT_Q14_ONE", "USB_SYNC0", "USB_SYNC1", "USB_FRAME_HEADER_SIZE", "USB_FRAME_OVERHEAD",
    "USB_RECEIVER_VID", "USB_RECEIVER_PID", "CRC8_POLY", "CRC8_INIT",
])
def test_constants_match_protocol_h(name):
    assert getattr(p, name) == header_defines()[name]


def test_crc8_check_value():
    assert p.crc8(b"123456789") == 0xF4


def test_reference_frame_decodes_to_documented_values():
    parser = p.FrameParser()
    frames = parser.feed(REFERENCE_FRAME)
    assert len(frames) == 1
    frame = frames[0]
    assert frame.rx_time_us == 0xAABBCCDD
    assert frame.rssi == -40
    hit = frame.packet
    assert isinstance(hit, p.HitPacket)
    assert (hit.header.stick_id, hit.header.seq) == (1, 0x1234)
    assert hit.t_us == 0x01020304
    assert hit.velocity == 100
    assert hit.quat == (16384, -1, 0, 258)
    assert hit.flags == p.HIT_FLAG_GYRO_SAT


def test_encoding_the_reference_packet_gives_the_reference_frame():
    hit = p.HitPacket(p.Header(p.PKT_HIT, 1, 0x1234), 0x01020304, 100, (16384, -1, 0, 258), p.HIT_FLAG_GYRO_SAT)
    assert p.encode_frame(p.Frame(0xAABBCCDD, -40, hit)) == REFERENCE_FRAME


def make_raw(stick, seq, count, t0=1000):
    samples = tuple(p.ImuSample(i, -i, 2 * i, 100 * i, -32768 + i, 32767 - i) for i in range(count))
    return p.RawPacket(p.Header(p.PKT_RAW, stick, seq), t0, samples)


@pytest.mark.parametrize("packet", [
    make_raw(0, 1, 1),
    make_raw(1, 2, p.RAW_MAX_SAMPLES),
    p.HitPacket(p.Header(p.PKT_HIT, 0, 3), 99, 127, (1, -2, 3, -4), 3),
    p.OrientPacket(p.Header(p.PKT_ORIENT, 1, 4), 5, (16384, 0, 0, 0)),
    p.StatusPacket(p.Header(p.PKT_STATUS, 0, 5), 3712, (-3, 0, 12), 4, 65535, 2, 3, 4),
])
def test_packets_round_trip(packet):
    assert p.decode_packet(p.encode_packet(packet)) == packet


def test_sizes_match_the_c_structs():
    assert len(p.encode_packet(make_raw(0, 0, 1))) == 21
    assert len(p.encode_packet(make_raw(0, 0, 8))) == 105
    assert len(p.encode_packet(p.HitPacket(p.Header(2, 0, 0), 0, 1, (0, 0, 0, 0), 0))) == 18
    assert len(p.encode_packet(p.OrientPacket(p.Header(3, 0, 0), 0, (0, 0, 0, 0)))) == 16
    assert len(p.encode_packet(p.StatusPacket(p.Header(4, 0, 0), 0, (0, 0, 0), 0))) == 21


def test_raw_sample_count_is_validated():
    with pytest.raises(ValueError):
        p.encode_packet(p.RawPacket(p.Header(1, 0, 0), 0, ()))
    data = bytearray(p.encode_packet(make_raw(0, 0, 1)))
    data[p.RAW_COUNT_OFFSET] = 0
    assert p.decode_packet(bytes(data)) is None


def orient_frame(seq):
    return p.encode_frame(p.Frame(seq, -50, p.OrientPacket(p.Header(p.PKT_ORIENT, 0, seq), seq, (16384, 0, 0, 0))))


def test_frames_split_byte_by_byte_are_reassembled():
    stream = b"".join(orient_frame(seq) for seq in range(5))
    parser = p.FrameParser()
    frames = []
    for i in range(len(stream)):
        frames += parser.feed(stream[i:i + 1])
    assert [f.packet.header.seq for f in frames] == list(range(5))
    assert parser.stats.discarded_bytes == 0


def test_leading_garbage_is_skipped():
    parser = p.FrameParser()
    frames = parser.feed(bytes([0x00, 0xA5, 0x13, 0x5A, 0xA5, 0xFF]) + REFERENCE_FRAME)
    assert len(frames) == 1
    assert parser.stats.discarded_bytes == 6


def test_corrupted_frame_is_dropped_and_the_next_one_kept():
    bad = bytearray(orient_frame(1))
    bad[12] ^= 0x01
    parser = p.FrameParser()
    frames = parser.feed(bytes(bad) + orient_frame(2))
    assert [f.packet.header.seq for f in frames] == [2]
    assert parser.stats.crc_errors == 1


def test_inconsistent_length_is_rejected_without_waiting():
    parser = p.FrameParser()
    frames = parser.feed(bytes([0xA5, 0x5A, 0xF0, 0, 0, 0, 0, 0, p.PKT_HIT]) + REFERENCE_FRAME)
    assert len(frames) == 1
    assert parser.stats.length_errors >= 1


def test_sync_bytes_inside_payload_do_not_break_parsing():
    frame = p.encode_frame(p.Frame(0, 0, p.OrientPacket(p.Header(3, 0, 0x5AA5), 0xA55AA55A, (0x5AA5, 0, 0, 0))))
    parser = p.FrameParser()
    assert len(parser.feed(frame * 3)) == 3
    assert parser.stats.discarded_bytes == 0


def test_matches_the_cpp_parser_on_a_trailing_sync_byte():
    parser = p.FrameParser()
    assert parser.feed(REFERENCE_FRAME[:1]) == []
    assert len(parser.feed(REFERENCE_FRAME[1:])) == 1
    assert parser.stats.discarded_bytes == 0


def test_table_crc_matches_the_bitwise_reference():
    import random
    rng = random.Random(4)
    for length in (0, 1, 7, 105, 114):
        data = bytes(rng.randrange(256) for _ in range(length))
        assert p.crc8(data) == p.crc8_bitwise(data)
    assert p.crc8(b"123456789") == 0xF4
