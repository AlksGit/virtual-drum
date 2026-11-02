"""Analyse a session recorded with the sticks lying still: link quality, clocks, sensor noise, drift.

    python tools/analyze_rest.py recordings/session_<stamp>_imu.csv
    python tools/analyze_rest.py recordings/session_<stamp>_imu.csv --calibration 10

Per stick:
- link: packets lost (seq gaps, STATUS seqs excluded), loss bursts, RSSI, STATUS counter deltas
  (ESB retries, failed payloads, queue full, IMU FIFO overruns);
- clocks: sample rate against the stick clock (t_us) and against the receiver clock
  (rx_time_us), and the drift between the two clocks in ppm;
- sensors: accelerometer norm (g), gyroscope bias (deg/s) and noise, bias stability per minute;
- yaw drift: the gyroscope is calibrated on the first --calibration seconds (bias = mean), then
  the rate around the vertical (gravity direction from the accelerometer) is integrated over the
  rest of the session. This is the drift left after a start-up bias calibration, at rest.

Sensitivities (accelerometer +-16 g, gyroscope +-2000 dps) are the ones verified in
docs/HARDWARE.md (ST driver lsm6ds3tr-c_reg.c, NCS v3.4.1).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

from plot_session import (
    ACCEL_AXES,
    GYRO_AXES,
    RX_CLOCK_WRAP,
    load_events,
    load_session,
    packet_losses,
    raw_packet_rows,
    unwrap_time_s,
)

ACCEL_G_PER_LSB = 0.488e-3  # lsm6ds3tr_c_from_fs16g_to_mg: 0.488 mg/LSB
GYRO_DPS_PER_LSB = 70e-3    # lsm6ds3tr_c_from_fs2000dps_to_mdps: 70 mdps/LSB
NOMINAL_ODR_HZ = 1666.0     # LSM6DS3TR_C_XL_ODR_1k66Hz (firmware/stick/src/imu.c)
STATUS_COUNTERS = ("tx_retries", "tx_failed", "tx_queue_full", "imu_overruns")
SAMPLES_PER_PACKET = 8      # RAW_MAX_SAMPLES: a lost packet is counted as a full one
STALE_GAP_S = 1.0           # a receiver-time jump this long right after the start: stale packets


def unwrap_rx_s(rx_us: np.ndarray) -> np.ndarray:
    """Receiver times (u32 us) relative to the first one, in seconds."""
    rel = (rx_us.astype(np.int64) - int(rx_us[0])) % RX_CLOCK_WRAP
    return rel / 1e6


def counter_delta(values: np.ndarray) -> int:
    """Increase of a u16 counter that wraps at 2^16, summed over consecutive readings."""
    if len(values) < 2:
        return 0
    return int(((np.diff(values.astype(np.int64))) % 65536).sum())


def drop_stale_start(data: pd.DataFrame) -> pd.DataFrame:
    """Packets the receiver held from before the session (old rx_time_us, then a jump of
    minutes) arrive first in recordings made before the receiver fix: drop them."""
    rows = raw_packet_rows(data)
    if len(rows) < 2:
        return data
    rx = unwrap_rx_s(data["rx_time_us"].to_numpy()[rows])
    jumps = np.flatnonzero(np.diff(rx) > STALE_GAP_S)
    if len(jumps) and rx[jumps[0]] < STALE_GAP_S:
        return data.iloc[rows[jumps[0] + 1]:].reset_index(drop=True)
    return data


def link_report(data: pd.DataFrame, events, stick_id: int) -> dict:
    losses = packet_losses(data, events, stick_id)
    rows = raw_packet_rows(data)
    report = {
        "packets": len(rows),
        "lost": int(losses.sum()),
        "loss_rate": float(losses.sum()) / max(len(rows) + int(losses.sum()), 1),
        "loss_events": int(np.count_nonzero(losses)),
        "largest_burst": int(losses.max()) if len(losses) else 0,
        "rssi_mean": float(data["rssi"].mean()),
        "rssi_min": int(data["rssi"].min()),
    }
    if events is not None:
        status = events[(events["stick_id"] == stick_id) & (events["type"] == "STATUS")]
        for name in STATUS_COUNTERS:
            report[name] = counter_delta(status[name].to_numpy()) if name in status else None
        report["status_packets"] = len(status)
    return report


def clock_report(data: pd.DataFrame, lost_packets: int) -> dict:
    t_stick = unwrap_time_s(data["t_us"])
    rows = raw_packet_rows(data)
    rx = unwrap_rx_s(data["rx_time_us"].to_numpy()[rows])
    t_first = t_stick[rows]
    stick_span = float(t_stick[-1] - t_stick[0])
    # Receiver arrival jitters by one packet period: fit a line through arrival vs stick time.
    slope = float(np.polyfit(t_first, rx, 1)[0]) if len(rows) > 2 else float("nan")
    produced = len(data) + lost_packets * SAMPLES_PER_PACKET  # lost samples were produced too
    rate_stick = (produced - 1) / stick_span if stick_span > 0 else float("nan")
    return {
        "duration_s": stick_span,
        "rate_stick_clock_hz": rate_stick,
        "rate_receiver_clock_hz": rate_stick / slope if slope else float("nan"),
        "odr_error_pct": (rate_stick / NOMINAL_ODR_HZ - 1.0) * 100.0,
        "stick_vs_receiver_ppm": (slope - 1.0) * 1e6,
        "timestamp_steps_us": sorted(set(np.diff(data["t_us"].to_numpy(dtype=np.int64))[:2000].tolist()))[:6],
    }


def sensor_report(data: pd.DataFrame, calibration_s: float) -> dict:
    t = unwrap_time_s(data["t_us"])
    accel = data[list(ACCEL_AXES)].to_numpy(dtype=float) * ACCEL_G_PER_LSB
    gyro = data[list(GYRO_AXES)].to_numpy(dtype=float) * GYRO_DPS_PER_LSB

    norm = np.linalg.norm(accel, axis=1)
    bias = gyro.mean(axis=0)
    up = accel.mean(axis=0)
    up /= np.linalg.norm(up)

    # Bias stability: mean of each full minute.
    minutes = int(t[-1] // 60)
    per_minute = np.array([gyro[(t >= 60 * m) & (t < 60 * (m + 1))].mean(axis=0) for m in range(minutes)]).reshape(-1, 3)

    # Yaw drift after a start-up calibration.
    calib = t < calibration_s
    rest = ~calib
    if calib.any() and rest.any():
        yaw_rate = (gyro[rest] - gyro[calib].mean(axis=0)) @ up
        yaw = np.cumsum(yaw_rate * np.diff(t[rest], prepend=t[rest][0]))
        rest_span = float(t[rest][-1] - t[rest][0])
    else:  # session shorter than the calibration
        yaw = np.array([])
        rest_span = 0.0

    return {
        "accel_norm_g": float(norm.mean()),
        "accel_noise_mg": (accel.std(axis=0) * 1e3).round(2).tolist(),
        "gyro_bias_dps": bias.round(3).tolist(),
        "gyro_noise_dps": gyro.std(axis=0).round(3).tolist(),
        "bias_range_per_minute_dps": (np.ptp(per_minute, axis=0).round(4).tolist() if minutes >= 2 else None),
        "bias_vertical_per_minute_dps": (per_minute @ up).round(4).tolist() if minutes >= 1 else None,
        "yaw_drift_deg": float(yaw[-1]) if len(yaw) else float("nan"),
        "yaw_drift_deg_per_min": float(yaw[-1]) / rest_span * 60.0 if rest_span > 0 else float("nan"),
        "yaw_drift_max_abs_deg": float(np.abs(yaw).max()) if len(yaw) else float("nan"),
        "repeated_samples": int((np.diff(data[list(ACCEL_AXES + GYRO_AXES)].to_numpy(), axis=0) == 0).all(axis=1).sum()),
    }


def analyse(path: Path, calibration_s: float) -> dict:
    frame = load_session(path)
    events = load_events(path)
    results = {}
    for stick_id in sorted(frame["stick_id"].unique()):
        data = drop_stale_start(frame[frame["stick_id"] == stick_id].reset_index(drop=True))
        link = link_report(data, events, int(stick_id))
        results[int(stick_id)] = {
            "link": link,
            "clock": clock_report(data, link["lost"]),
            "sensors": sensor_report(data, calibration_s),
        }
    return results


def print_report(results: dict) -> None:
    for stick_id, sections in results.items():
        print(f"stick {stick_id}")
        for section, values in sections.items():
            print(f"  {section}")
            for key, value in values.items():
                if isinstance(value, float):
                    value = f"{value:.4g}"
                print(f"    {key:28s} {value}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("imu_csv", type=Path, help="session_<stamp>_imu.csv written by logger.py")
    parser.add_argument("--calibration", type=float, default=10.0,
                        help="seconds at the start used as gyroscope bias calibration (default 10)")
    args = parser.parse_args(argv)
    print_report(analyse(args.imu_csv, args.calibration))
    return 0


if __name__ == "__main__":
    sys.exit(main())
