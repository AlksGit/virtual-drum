"""Record a session from the Virtual Drum receiver to CSV files.

    python tools/logger.py                      # auto-detect the receiver by USB ids
    python tools/logger.py --port COM5          # or /dev/cu.usbmodem... on macOS
    python tools/logger.py --replay capture.bin # re-decode a raw byte capture (no hardware)

Each session writes, in --out (default: recordings/ at the repo root, ignored by git):
    session_<stamp>_imu.csv     one row per IMU sample of RAW packets
    session_<stamp>_events.csv  HIT, ORIENT and STATUS packets
    session_<stamp>.bin         the raw byte stream (with --save-raw), replayable later
A summary is printed every second: sample rate per stick, packets lost (seq gaps), crc errors.
Stop with Ctrl+C or --duration.
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from datetime import datetime
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))

import serial  # noqa: E402

from vdrum.ports import describe_ports, find_receiver_port  # noqa: E402
from vdrum.protocol import (  # noqa: E402
    FrameParser,
    HitPacket,
    OrientPacket,
    RawPacket,
    StatusPacket,
)

DEFAULT_OUT = Path(__file__).resolve().parent.parent / "recordings"
DEFAULT_ODR_HZ = 1666.0

IMU_COLUMNS = ["rx_time_us", "rssi", "stick_id", "seq", "t0_us", "index", "t_us",
               "ax", "ay", "az", "gx", "gy", "gz"]
EVENT_COLUMNS = ["rx_time_us", "rssi", "stick_id", "seq", "type", "t_us",
                 "velocity", "flags", "qw", "qx", "qy", "qz", "vbat_mv",
                 "bias_x", "bias_y", "bias_z", "fw_version"]


class SessionWriter:
    """Writes decoded frames to the session CSV files and tracks losses per stick."""

    def __init__(self, out_dir: Path, stamp: str, odr_hz: float) -> None:
        out_dir.mkdir(parents=True, exist_ok=True)
        self.imu_path = out_dir / f"session_{stamp}_imu.csv"
        self.events_path = out_dir / f"session_{stamp}_events.csv"
        self._imu_file = self.imu_path.open("w", newline="")
        self._events_file = self.events_path.open("w", newline="")
        self._imu = csv.writer(self._imu_file)
        self._events = csv.writer(self._events_file)
        self._imu.writerow(IMU_COLUMNS)
        self._events.writerow(EVENT_COLUMNS)
        self._sample_period_us = 1e6 / odr_hz
        self._last_seq: dict = {}
        self.lost: dict = {}
        self.samples: dict = {}

    def write(self, frame) -> None:
        packet = frame.packet
        header = packet.header
        stick = header.stick_id

        if stick in self._last_seq:
            gap = (header.seq - self._last_seq[stick] - 1) & 0xFFFF
            self.lost[stick] = self.lost.get(stick, 0) + gap
        self._last_seq[stick] = header.seq

        if isinstance(packet, RawPacket):
            for index, s in enumerate(packet.samples):
                t_us = packet.t0_us + round(index * self._sample_period_us)
                self._imu.writerow([frame.rx_time_us, frame.rssi, stick, header.seq, packet.t0_us, index, t_us,
                                    s.ax, s.ay, s.az, s.gx, s.gy, s.gz])
            self.samples[stick] = self.samples.get(stick, 0) + len(packet.samples)
            return

        row = dict.fromkeys(EVENT_COLUMNS, "")
        row.update(rx_time_us=frame.rx_time_us, rssi=frame.rssi, stick_id=stick, seq=header.seq)
        if isinstance(packet, HitPacket):
            row.update(type="HIT", t_us=packet.t_us, velocity=packet.velocity, flags=packet.flags,
                       qw=packet.quat[0], qx=packet.quat[1], qy=packet.quat[2], qz=packet.quat[3])
        elif isinstance(packet, OrientPacket):
            row.update(type="ORIENT", t_us=packet.t_us,
                       qw=packet.quat[0], qx=packet.quat[1], qy=packet.quat[2], qz=packet.quat[3])
        elif isinstance(packet, StatusPacket):
            row.update(type="STATUS", vbat_mv=packet.vbat_mv, bias_x=packet.gyro_bias[0],
                       bias_y=packet.gyro_bias[1], bias_z=packet.gyro_bias[2], fw_version=packet.fw_version)
        self._events.writerow([row[c] for c in EVENT_COLUMNS])

    def close(self) -> None:
        self._imu_file.close()
        self._events_file.close()


def summary_line(writer: SessionWriter, parser: FrameParser, previous: dict, seconds: float) -> str:
    parts = []
    for stick in sorted(set(writer.samples) | set(writer.lost)):
        rate = (writer.samples.get(stick, 0) - previous.get(stick, 0)) / seconds
        parts.append(f"stick {stick}: {rate:6.0f} samples/s, lost {writer.lost.get(stick, 0)}")
    parts.append(f"crc errors {parser.stats.crc_errors}")
    return " | ".join(parts)


def run(args: argparse.Namespace) -> int:
    # Open the source first, so that no empty session files are left behind on failure.
    if args.replay:
        source = Path(args.replay).open("rb")
        read = lambda: source.read(4096)  # noqa: E731
        close = source.close
        print(f"Replaying {args.replay}")
    else:
        port = args.port or find_receiver_port()
        if port is None:
            print("Virtual Drum receiver not found (USB ids from protocol.h). Ports:\n" + describe_ports())
            return 1
        try:
            # timeout bounds the latency of Ctrl+C / --duration; CDC ignores the baud rate.
            link = serial.Serial(port, 115200, timeout=0.05)
        except serial.SerialException as error:
            print(f"Cannot open {port}: {error}")
            return 1
        read = lambda: link.read(max(1, link.in_waiting))  # noqa: E731
        close = link.close
        print(f"Recording from {port} (Ctrl+C to stop)")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    parser = FrameParser()
    writer = SessionWriter(Path(args.out), stamp, args.odr)
    raw_file = (Path(args.out) / f"session_{stamp}.bin").open("wb") if args.save_raw else None

    start = time.monotonic()
    last_summary = start
    previous_samples: dict = {}
    try:
        while args.duration is None or time.monotonic() - start < args.duration:
            data = read()
            if not data:
                if args.replay:
                    break
                continue
            if raw_file is not None:
                raw_file.write(data)
            for frame in parser.feed(data):
                writer.write(frame)

            now = time.monotonic()
            if not args.replay and now - last_summary >= 1.0:
                print(summary_line(writer, parser, previous_samples, now - last_summary), flush=True)
                previous_samples = dict(writer.samples)
                last_summary = now
    except KeyboardInterrupt:
        pass
    finally:
        close()
        writer.close()
        if raw_file is not None:
            raw_file.close()

    stats = parser.stats
    print(f"frames={stats.frames} crc_errors={stats.crc_errors} length_errors={stats.length_errors} "
          f"discarded_bytes={stats.discarded_bytes} lost={dict(sorted(writer.lost.items()))}")
    print(f"Wrote {writer.imu_path}\n      {writer.events_path}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--port", help="serial port (default: auto-detect the receiver by USB ids)")
    source.add_argument("--replay", help="decode a raw byte capture instead of a serial port")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="output directory (default: %(default)s)")
    parser.add_argument("--odr", type=float, default=DEFAULT_ODR_HZ,
                        help="IMU sample rate in Hz, for the per-sample timestamps (default: %(default)s)")
    parser.add_argument("--duration", type=float, help="stop after this many seconds")
    parser.add_argument("--save-raw", action="store_true", help="also save the raw byte stream (.bin)")
    return run(parser.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
