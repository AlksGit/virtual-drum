"""Plot a recorded session: accelerometer and gyroscope per stick, saturated samples marked.

    python tools/plot_session.py recordings/session_<stamp>_imu.csv            # interactive window
    python tools/plot_session.py recordings/session_<stamp>_imu.csv --out a.png  # save a picture

Values are raw sensor counts (int16). A sample is saturated when an axis reaches the end of
the int16 range (the sensor clips there at its configured full scale). Conversion to g and
deg/s needs the sensitivities of the configured ranges, checked in the datasheet (TASK-004).
Also prints per-stick statistics: duration, sample rate, packets lost, saturated samples.
Packet losses also need the session_<stamp>_events.csv next to the IMU file: STATUS packets take
their seq from the same counter as RAW packets, so without it each STATUS looks like a loss.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd

from vdrum.protocol import INT16_MAX, INT16_MIN, saturated  # noqa: E402  (one definition)

RX_CLOCK_WRAP = 1 << 32  # receiver rx_time_us is a u32: loss accounting holds for 71 min
ACCEL_AXES = ("ax", "ay", "az")
GYRO_AXES = ("gx", "gy", "gz")


@dataclass
class StickStats:
    stick_id: int
    samples: int
    duration_s: float
    sample_rate_hz: float
    packets: int
    packets_lost: int
    accel_saturated: int
    gyro_saturated: int

    def describe(self) -> str:
        loss = 100.0 * self.packets_lost / max(1, self.packets + self.packets_lost)
        return (f"stick {self.stick_id}: {self.samples} samples over {self.duration_s:.1f} s "
                f"({self.sample_rate_hz:.0f} samples/s received), packets lost {self.packets_lost} ({loss:.2f} %), "
                f"saturated accel {self.accel_saturated}, gyro {self.gyro_saturated}")


def load_session(path: Path) -> pd.DataFrame:
    frame = pd.read_csv(path)
    missing = {"stick_id", "seq", "t_us", *ACCEL_AXES, *GYRO_AXES} - set(frame.columns)
    if missing:
        raise ValueError(f"{path} is not an IMU session file (missing columns: {sorted(missing)})")
    return frame


def load_events(imu_path: Path):
    """The events CSV written next to an IMU CSV by logger.py, or None if absent."""
    name = imu_path.name
    if not name.endswith("_imu.csv"):
        return None
    events_path = imu_path.with_name(name[: -len("_imu.csv")] + "_events.csv")
    return pd.read_csv(events_path) if events_path.exists() else None


def saturated_mask(frame: pd.DataFrame, axes) -> np.ndarray:
    return saturated(frame[list(axes)].to_numpy()).any(axis=1)


def unwrap_time_s(t_us: pd.Series) -> np.ndarray:
    """Stick timestamps are u32 microseconds: unwrap the 71-minute wrap, start at 0."""
    t = t_us.to_numpy(dtype=np.int64)
    wraps = np.concatenate([[0], np.cumsum(np.diff(t) < -(1 << 31))])
    t = t + wraps * (1 << 32)
    return (t - t[0]) / 1e6 if len(t) else t.astype(float)


def raw_packet_rows(data: pd.DataFrame) -> np.ndarray:
    """Index (in data) of the first sample of each RAW packet: where seq changes. Only
    consecutive repeats are merged, so the u16 seq can wrap (every ~5 min at 1.66 kHz)."""
    seq = data["seq"].to_numpy(dtype=np.int64)
    if len(seq) == 0:
        return np.array([], dtype=np.int64)
    return np.flatnonzero(np.r_[True, np.diff(seq) != 0])


def packet_losses(data: pd.DataFrame, events=None, stick_id=None) -> np.ndarray:
    """Packets lost between consecutive RAW packets of one stick. Seqs taken by other packet
    types (STATUS...) received in between, found in the events CSV by arrival time, are not losses."""
    rows = raw_packet_rows(data)
    if len(rows) < 2:
        return np.array([], dtype=np.int64)
    seq = data["seq"].to_numpy(dtype=np.int64)[rows]
    missing = (np.diff(seq) - 1) % 65536
    if events is None or stick_id is None:
        return missing

    rx = data["rx_time_us"].to_numpy(dtype=np.int64)
    origin = rx[0]
    rx_rel = (rx[rows] - origin) % RX_CLOCK_WRAP
    others = events[events["stick_id"] == stick_id]["rx_time_us"].to_numpy(dtype=np.int64)
    others_rel = np.sort((others - origin) % RX_CLOCK_WRAP)
    between = np.searchsorted(others_rel, rx_rel[1:]) - np.searchsorted(others_rel, rx_rel[:-1], side="right")
    return np.maximum(missing - between, 0)


def stick_stats(frame: pd.DataFrame, stick_id: int, events=None) -> StickStats:
    data = frame[frame["stick_id"] == stick_id]
    gaps = packet_losses(data, events, stick_id)
    t = unwrap_time_s(data["t_us"])
    duration = float(t[-1]) if len(t) else 0.0
    return StickStats(
        stick_id=stick_id,
        samples=len(data),
        duration_s=duration,
        sample_rate_hz=(len(data) - 1) / duration if duration > 0 else 0.0,
        packets=len(raw_packet_rows(data)),
        packets_lost=int(gaps.sum()),
        accel_saturated=int(saturated_mask(data, ACCEL_AXES).sum()),
        gyro_saturated=int(saturated_mask(data, GYRO_AXES).sum()),
    )


def loss_spans(data: pd.DataFrame, t: np.ndarray, events=None, stick_id=None) -> list:
    """(start, end) times around each lost packet: from the last sample before to the first after."""
    rows = raw_packet_rows(data)
    losses = packet_losses(data, events, stick_id)
    return [(t[rows[i + 1] - 1], t[rows[i + 1]]) for i in np.flatnonzero(losses > 0)]


def plot(frame: pd.DataFrame, title: str, events=None):
    import matplotlib.pyplot as plt

    sticks = sorted(frame["stick_id"].unique())
    figure, axes = plt.subplots(2 * len(sticks), 1, figsize=(14, 4.5 * len(sticks)), sharex=True, squeeze=False)
    colours = {"x": "#e15759", "y": "#59a14f", "z": "#4e79a7"}

    for row, stick_id in enumerate(sticks):
        data = frame[frame["stick_id"] == stick_id]
        t = unwrap_time_s(data["t_us"])
        for offset, (axis_names, label) in enumerate(((ACCEL_AXES, "accelerometer"), (GYRO_AXES, "gyroscope"))):
            ax = axes[2 * row + offset][0]
            for name in axis_names:
                ax.plot(t, data[name].to_numpy(), linewidth=0.7, color=colours[name[1]], label=name)
            saturated = saturated_mask(data, axis_names)
            if saturated.any():
                ax.scatter(t[saturated], np.zeros(saturated.sum()) + INT16_MAX * 1.02, marker="v", s=14,
                           color="black", label=f"saturated ({int(saturated.sum())})", zorder=3)
            for index, (start, end) in enumerate(loss_spans(data, t, events, stick_id)):
                ax.axvspan(start, end, color="#e15759", alpha=0.25, label="packets lost" if index == 0 else None)
            ax.axhline(INT16_MAX, color="grey", linewidth=0.6, linestyle="--")
            ax.axhline(INT16_MIN, color="grey", linewidth=0.6, linestyle="--")
            ax.set_ylabel(f"stick {stick_id} {label}\n(raw counts)")
            ax.legend(loc="upper right", fontsize=8, ncol=4)
            ax.grid(alpha=0.3)

    axes[-1][0].set_xlabel("time (s, stick clock)")
    figure.suptitle(title)
    figure.tight_layout()
    return figure


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", help="session_<stamp>_imu.csv written by logger.py")
    parser.add_argument("--out", help="save the figure to this file instead of opening a window")
    args = parser.parse_args(argv)

    path = Path(args.session)
    frame = load_session(path)
    if frame.empty:
        print(f"{path}: no samples")
        return 1

    events = load_events(path)
    if events is None:
        print("no events CSV next to this file: STATUS packets will be counted as losses")
    for stick_id in sorted(frame["stick_id"].unique()):
        print(stick_stats(frame, int(stick_id), events).describe())

    if args.out:
        import matplotlib
        matplotlib.use("Agg")

    figure = plot(frame, path.name, events)
    if args.out:
        figure.savefig(args.out, dpi=110)
        print(f"Wrote {args.out}")
    else:
        import matplotlib.pyplot as plt
        plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
