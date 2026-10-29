"""Hit detection prototype: evaluate on simulated sessions, or run on a recorded one.

    python tools/detect_hits.py                      # simulated sessions, scored against truth
    python tools/detect_hits.py --realistic --sweep  # harder simulation, threshold trade-off
    python tools/detect_hits.py --csv recordings/session_<stamp>_imu.csv --stick 0
                                                     # detect hits in a real recording

The detector is written sample by sample, to be ported to the stick firmware (phase 2).
See tools/detection/ for the simulator, the detector and the scoring.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from detection.detector import DetectorConfig, VelocityModel, detect_session  # noqa: E402
from detection.evaluate import score  # noqa: E402
from detection.imu_sim import ODR_HZ, simulate_session  # noqa: E402


def evaluate_simulated(args) -> int:
    thresholds = [180, 220, 260, 300, 340, 380] if args.sweep else [args.on_dps]
    for mode in ("air", "pad"):
        calib = simulate_session(duration_s=20, mode=mode, seed=1, realistic=args.realistic)
        test = simulate_session(duration_s=args.duration, mode=mode, seed=args.seed, bpm=args.bpm,
                                realistic=args.realistic)
        for on_dps in thresholds:
            config = DetectorConfig(on_dps=on_dps)
            model = VelocityModel().fit(detect_session(calib.gyro, calib.t_s, config=config))
            detections = [model.apply(d) for d in detect_session(test.gyro, test.t_s, config=config)]
            print(f"--- {mode}{' realistic' if args.realistic else ''}, threshold {on_dps} dps")
            print(score(test.hits, detections, test.fakes).report())
    return 0


def detect_recording(args) -> int:
    import pandas as pd

    frame = pd.read_csv(args.csv)
    data = frame[frame["stick_id"] == args.stick]
    if data.empty:
        print(f"no samples for stick {args.stick}")
        return 1

    gyro = data[["gx", "gy", "gz"]].to_numpy(dtype=np.int64)
    t = (data["t_us"].to_numpy(dtype=np.int64) - int(data["t_us"].iloc[0])) / 1e6
    axes = {"x": 0, "y": 1, "z": 2}
    detections = detect_session(gyro, t, axis=axes[args.axis], sign=args.sign,
                                other_axis=axes[args.other_axis] if args.other_axis else None,
                                config=DetectorConfig(on_dps=args.on_dps))
    model = VelocityModel()
    try:
        model.fit(detections)
    except ValueError:
        pass  # too few unsaturated strokes: saturated ones keep their clipped peak
    print("time_s,velocity,peak_dps,saturated")
    for d in (model.apply(d) for d in detections):
        print(f"{d.time_s:.4f},{d.velocity:.3f},{d.peak_dps:.0f},{int(d.saturated)}")
    print(f"# {len(detections)} hits in {t[-1]:.1f} s", file=sys.stderr)
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", help="session_<stamp>_imu.csv from logger.py (else: simulated sessions)")
    parser.add_argument("--stick", type=int, default=0)
    parser.add_argument("--axis", choices="xyz", default="y", help="gyro axis of the strokes (default: y)")
    parser.add_argument("--other-axis", choices=["x", "y", "z", ""], default="z",
                        help="second axis of the stroke plane (default: z, '' to disable)")
    parser.add_argument("--sign", type=float, default=1.0, help="-1 if downstrokes read negative")
    parser.add_argument("--on-dps", type=float, default=DetectorConfig().on_dps)
    parser.add_argument("--realistic", action="store_true", help="harder simulation")
    parser.add_argument("--sweep", action="store_true", help="try several thresholds")
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--bpm", type=float, default=110.0)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args(argv)
    return detect_recording(args) if args.csv else evaluate_simulated(args)


if __name__ == "__main__":
    sys.exit(main())
