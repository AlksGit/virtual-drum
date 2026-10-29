"""Scores detections against the ground truth of a simulated (or annotated) session."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class Score:
    true_hits: int
    detections: int
    matched: int
    false_positives: int
    missed: int
    fake_triggers: int          # detections near a fake gesture
    latency_ms: np.ndarray      # detection time - truth time, matched hits (negative = early)
    velocity_true: np.ndarray
    velocity_estimated: np.ndarray

    @property
    def precision(self) -> float:
        return self.matched / self.detections if self.detections else 0.0

    @property
    def recall(self) -> float:
        return self.matched / self.true_hits if self.true_hits else 0.0

    @property
    def velocity_correlation(self) -> float:
        if len(self.velocity_true) < 3:
            return 0.0
        return float(np.corrcoef(self.velocity_true, self.velocity_estimated)[0, 1])

    @property
    def velocity_error_midi(self) -> float:
        """Mean absolute velocity error in MIDI steps (1..127)."""
        return float(np.mean(np.abs(self.velocity_true - self.velocity_estimated)) * 127) if len(self.velocity_true) else 0.0

    def report(self) -> str:
        lat = self.latency_ms
        lat_text = (f"latency median {np.median(lat):+.1f} ms, p5 {np.percentile(lat, 5):+.1f}, "
                    f"p95 {np.percentile(lat, 95):+.1f}") if len(lat) else "latency n/a"
        return (f"hits {self.true_hits}, detections {self.detections}, precision {self.precision:.3f}, "
                f"recall {self.recall:.3f}, false positives {self.false_positives}, missed {self.missed}, "
                f"fake triggers {self.fake_triggers}\n{lat_text}\n"
                f"velocity correlation {self.velocity_correlation:.3f}, mean error {self.velocity_error_midi:.1f} MIDI steps")


def score(hits, detections, fakes=(), window_s: float = 0.03) -> Score:
    """Greedy one-to-one matching of detections to hits within +-window."""
    used = set()
    latencies, v_true, v_est = [], [], []
    for hit in hits:
        best, best_dt = None, window_s
        for i, d in enumerate(detections):
            dt = abs(d.time_s - hit.time_s)
            if i not in used and dt <= best_dt:
                best, best_dt = i, dt
        if best is not None:
            used.add(best)
            latencies.append((detections[best].time_s - hit.time_s) * 1000.0)
            v_true.append(hit.velocity)
            v_est.append(detections[best].velocity)

    unmatched = [d for i, d in enumerate(detections) if i not in used]
    fake_triggers = sum(1 for d in unmatched for f in fakes if abs(d.time_s - f) < 0.08)
    return Score(
        true_hits=len(hits), detections=len(detections), matched=len(used),
        false_positives=len(unmatched), missed=len(hits) - len(used), fake_triggers=fake_triggers,
        latency_ms=np.array(latencies), velocity_true=np.array(v_true), velocity_estimated=np.array(v_est),
    )
