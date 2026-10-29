"""Python view of firmware/common/protocol.h (see docs/PROTOCOL.md).

Keep in sync with the C header: tests/test_protocol.py checks the constants against it.
All fields are little-endian; packets are decoded field by field with `struct`.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import Enum
from typing import Optional, Union

PROTO_VERSION = 4

PKT_RAW = 0x01
PKT_HIT = 0x02
PKT_ORIENT = 0x03
PKT_STATUS = 0x04

RAW_MAX_SAMPLES = 8
QUAT_Q14_ONE = 16384
HIT_FLAG_GYRO_SAT = 1 << 0
HIT_FLAG_ACCEL_SAT = 1 << 1

USB_SYNC0 = 0xA5
USB_SYNC1 = 0x5A
USB_FRAME_HEADER_SIZE = 8  # sync(2) + len(1) + rx_time_us(4) + rssi(1)
USB_FRAME_OVERHEAD = 9     # header + crc8

USB_RECEIVER_VID = 0x2FE3
USB_RECEIVER_PID = 0x0100

CRC8_POLY = 0x07
CRC8_INIT = 0x00

_HEADER = struct.Struct("<BBH")
_RAW_FIXED = struct.Struct("<BBHIB")
_SAMPLE = struct.Struct("<6h")
_HIT = struct.Struct("<BBHIB4hB")
_ORIENT = struct.Struct("<BBHI4h")
_STATUS = struct.Struct("<BBHH3hB4H")
_FRAME_HEADER = struct.Struct("<BBBIb")

RAW_COUNT_OFFSET = 8  # header(4) + t0_us(4)


def raw_packet_size(count: int) -> int:
    return _RAW_FIXED.size + count * _SAMPLE.size


def crc8_bitwise(data: bytes, crc: int = CRC8_INIT) -> int:
    """CRC-8/SMBUS: poly 0x07, init 0x00, no reflection, no final xor (reference algorithm)."""
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ CRC8_POLY) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


_CRC8_TABLE = bytes(crc8_bitwise(bytes([i])) for i in range(256))


def crc8(data: bytes, crc: int = CRC8_INIT) -> int:
    """Same CRC, one table lookup per byte (about 20x faster, the logger's main cost)."""
    table = _CRC8_TABLE
    for byte in data:
        crc = table[crc ^ byte]
    return crc


@dataclass(frozen=True)
class Header:
    type: int
    stick_id: int
    seq: int


@dataclass(frozen=True)
class ImuSample:
    ax: int
    ay: int
    az: int
    gx: int
    gy: int
    gz: int


@dataclass(frozen=True)
class RawPacket:
    header: Header
    t0_us: int
    samples: tuple


@dataclass(frozen=True)
class HitPacket:
    header: Header
    t_us: int
    velocity: int
    quat: tuple  # w, x, y, z in Q14
    flags: int


@dataclass(frozen=True)
class OrientPacket:
    header: Header
    t_us: int
    quat: tuple  # w, x, y, z in Q14


@dataclass(frozen=True)
class StatusPacket:
    header: Header
    vbat_mv: int
    gyro_bias: tuple
    fw_version: int
    # Cumulative counters since boot, wrapping at 2**16 (protocol v4).
    tx_retries: int = 0
    tx_failed: int = 0
    tx_queue_full: int = 0
    imu_overruns: int = 0


Packet = Union[RawPacket, HitPacket, OrientPacket, StatusPacket]


@dataclass(frozen=True)
class Frame:
    rx_time_us: int
    rssi: int
    packet: Packet


class LengthCheck(Enum):
    NEED_MORE_DATA = 0
    CONSISTENT = 1
    INCONSISTENT = 2


def check_payload_length(payload: bytes, declared_length: int) -> LengthCheck:
    """Checks a declared payload length against the packet type, using the bytes available."""
    if len(payload) < 1:
        return LengthCheck.NEED_MORE_DATA

    expected = {PKT_HIT: _HIT.size, PKT_ORIENT: _ORIENT.size, PKT_STATUS: _STATUS.size}
    packet_type = payload[0]
    if packet_type in expected:
        return LengthCheck.CONSISTENT if declared_length == expected[packet_type] else LengthCheck.INCONSISTENT

    if packet_type == PKT_RAW:
        if declared_length <= RAW_COUNT_OFFSET:
            return LengthCheck.INCONSISTENT
        if len(payload) <= RAW_COUNT_OFFSET:
            return LengthCheck.NEED_MORE_DATA
        count = payload[RAW_COUNT_OFFSET]
        if not 1 <= count <= RAW_MAX_SAMPLES:
            return LengthCheck.INCONSISTENT
        return LengthCheck.CONSISTENT if declared_length == raw_packet_size(count) else LengthCheck.INCONSISTENT

    return LengthCheck.INCONSISTENT


def decode_packet(data: bytes) -> Optional[Packet]:
    """Decodes one radio payload; returns None for an unknown type or a size mismatch."""
    data = bytes(data)
    if check_payload_length(data, len(data)) is not LengthCheck.CONSISTENT:
        return None

    packet_type = data[0]
    if packet_type == PKT_RAW:
        ptype, stick, seq, t0_us, count = _RAW_FIXED.unpack_from(data)
        samples = tuple(ImuSample(*_SAMPLE.unpack_from(data, _RAW_FIXED.size + i * _SAMPLE.size)) for i in range(count))
        return RawPacket(Header(ptype, stick, seq), t0_us, samples)

    if packet_type == PKT_HIT:
        ptype, stick, seq, t_us, velocity, qw, qx, qy, qz, flags = _HIT.unpack(data)
        return HitPacket(Header(ptype, stick, seq), t_us, velocity, (qw, qx, qy, qz), flags)

    if packet_type == PKT_ORIENT:
        ptype, stick, seq, t_us, qw, qx, qy, qz = _ORIENT.unpack(data)
        return OrientPacket(Header(ptype, stick, seq), t_us, (qw, qx, qy, qz))

    ptype, stick, seq, vbat, bx, by, bz, fw, retries, failed, full, overruns = _STATUS.unpack(data)
    return StatusPacket(Header(ptype, stick, seq), vbat, (bx, by, bz), fw, retries, failed, full, overruns)


def encode_packet(packet: Packet) -> bytes:
    """Encodes a packet; the type byte always follows the packet class."""
    h = packet.header
    if isinstance(packet, RawPacket):
        count = len(packet.samples)
        if not 1 <= count <= RAW_MAX_SAMPLES:
            raise ValueError(f"RAW packets carry 1..{RAW_MAX_SAMPLES} samples, got {count}")
        out = bytearray(_RAW_FIXED.pack(PKT_RAW, h.stick_id, h.seq, packet.t0_us, count))
        for s in packet.samples:
            out += _SAMPLE.pack(s.ax, s.ay, s.az, s.gx, s.gy, s.gz)
        return bytes(out)
    if isinstance(packet, HitPacket):
        return _HIT.pack(PKT_HIT, h.stick_id, h.seq, packet.t_us, packet.velocity, *packet.quat, packet.flags)
    if isinstance(packet, OrientPacket):
        return _ORIENT.pack(PKT_ORIENT, h.stick_id, h.seq, packet.t_us, *packet.quat)
    if isinstance(packet, StatusPacket):
        return _STATUS.pack(PKT_STATUS, h.stick_id, h.seq, packet.vbat_mv, *packet.gyro_bias, packet.fw_version,
                            packet.tx_retries, packet.tx_failed, packet.tx_queue_full, packet.imu_overruns)
    raise TypeError(f"not a packet: {packet!r}")


def encode_frame(frame: Frame) -> bytes:
    payload = encode_packet(frame.packet)
    body = struct.pack("<BIb", len(payload), frame.rx_time_us, frame.rssi) + payload
    return bytes([USB_SYNC0, USB_SYNC1]) + body + bytes([crc8(body)])


@dataclass
class ParserStats:
    frames: int = 0
    crc_errors: int = 0
    length_errors: int = 0
    discarded_bytes: int = 0


class FrameParser:
    """Incremental parser for the receiver's USB byte stream, same rules as the host C++ one:
    on a bad crc, an inconsistent length or an unknown type, drop one byte and resynchronise."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.stats = ParserStats()

    def feed(self, data: bytes) -> list:
        self._buffer += data
        frames = []
        head = 0
        buf = self._buffer

        while True:
            sync = buf.find(bytes([USB_SYNC0, USB_SYNC1]), head)
            if sync < 0:
                # Keep a trailing 0xA5: it may start the next sync sequence.
                keep_from = len(buf) - 1 if buf and buf[-1] == USB_SYNC0 else len(buf)
                self.stats.discarded_bytes += max(0, keep_from - head)
                head = max(head, keep_from)
                break

            self.stats.discarded_bytes += sync - head
            head = sync
            available = len(buf) - head
            if available < USB_FRAME_HEADER_SIZE:
                break

            length = buf[head + 2]
            payload_start = head + USB_FRAME_HEADER_SIZE
            payload = bytes(buf[payload_start:payload_start + length])
            check = LengthCheck.INCONSISTENT if length == 0 else check_payload_length(payload, length)

            if check is LengthCheck.INCONSISTENT:
                self.stats.length_errors += 1
                self.stats.discarded_bytes += 1
                head += 1
                continue
            if check is LengthCheck.NEED_MORE_DATA:
                break

            frame_size = USB_FRAME_OVERHEAD + length
            if available < frame_size:
                break

            if buf[head + frame_size - 1] != crc8(buf[head + 2:head + frame_size - 1]):
                self.stats.crc_errors += 1
                self.stats.discarded_bytes += 1
                head += 1
                continue

            packet = decode_packet(payload)
            if packet is None:
                self.stats.length_errors += 1
                self.stats.discarded_bytes += 1
                head += 1
                continue

            _, _, _, rx_time_us, rssi = _FRAME_HEADER.unpack_from(buf, head)
            frames.append(Frame(rx_time_us, rssi, packet))
            self.stats.frames += 1
            head += frame_size

        del buf[:head]
        return frames
