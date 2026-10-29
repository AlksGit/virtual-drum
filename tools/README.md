# tools — Python

Recording and analysis of sessions (phase 0–1). Same code on Windows and macOS.

## Setup (once per machine, never committed)
```
python -m venv .venv
.venv/Scripts/python -m pip install -r tools/requirements.txt   # Windows
.venv/bin/python -m pip install -r tools/requirements.txt       # macOS
```

## logger.py — record a session
```
python tools/logger.py                       # auto-detect the receiver (USB ids in protocol.h)
python tools/logger.py --port COM5           # or /dev/cu.usbmodem... on macOS
python tools/logger.py --duration 60 --save-raw
python tools/logger.py --replay recordings/session_..._.bin   # re-decode a capture
```
Writes `session_<stamp>_imu.csv` (one row per IMU sample: rx time, rssi, stick, seq, t0,
index, t_us, ax..gz) and `session_<stamp>_events.csv` (HIT, ORIENT, STATUS) in `recordings/`
(ignored by git). Prints rates, packet losses (seq gaps) and crc errors every second.

## plot_session.py — look at a session
```
python tools/plot_session.py recordings/session_<stamp>_imu.csv            # window
python tools/plot_session.py recordings/session_<stamp>_imu.csv --out a.png
```
Accelerometer and gyroscope per stick in raw counts, with the int16 full-scale lines,
saturated samples marked and lost packets shaded. Prints, per stick: received sample rate,
packets lost and saturated samples. Keep `session_<stamp>_events.csv` next to the IMU file:
STATUS packets share the seq counter with RAW packets, and the events file tells them apart
from real losses. Losses are counted correctly across the seq wrap (~5 min). (Conversion to g and deg/s waits for the sensitivities to
be checked in the datasheet, TASK-004.)

## fetch_kits.py — free sampled kits for the app
```
python tools/fetch_kits.py              # Swirly + Big Rusty (Karoryfer, CC0), 6 layers per pad
python tools/fetch_kits.py --kit swirly --layers 8
```
Downloads files one by one from the sfzinstruments repositories (pinned commits) into
`Documents/Virtual Drum/Kits` (or `$VDRUM_KITS_DIR`, `--dest`), named `<pad>_v<N>` for the app.
The softest quarter of the recorded layers is skipped (30-40 dB down, silent on a virtual kit).

## detect_hits.py — hit detection prototype (phase 1 groundwork)
```
python tools/detect_hits.py                        # simulated sessions, scored against truth
python tools/detect_hits.py --realistic --sweep    # harder simulation, threshold trade-off
python tools/detect_hits.py --csv recordings/session_<stamp>_imu.csv --stick 0   # real data
```
- `detection/imu_sim.py` — simulated stick IMU (wrist pivot, braked "air" strokes or "pad"
  impacts, gyro saturation at +-2000 dps, noise, fake twitches) with ground truth.
- `detection/detector.py` — streaming detector, one sample at a time (to port to the firmware):
  fires when a fast downstroke is braked below 35 % of its peak, a few ms before the stick stops;
  velocity from the peak rate, or from the rise acceleration when the gyro saturates.
- `detection/evaluate.py` — precision, recall, latency, velocity correlation.

Results on simulated data (60 s, 110 bpm, threshold 260 dps):

| Simulation | Recall | Precision | Velocity correlation | Fires before the stop |
|---|---|---|---|---|
| clean | 100 % | 100 % | 0.99 | ~5 ms |
| realistic (tilted axis, doubles, ghost notes, more noise) | ~99 % | ~98.5 % | 0.995 | ~8 ms |

The threshold trades ghost notes against twitches (180 dps: no miss, more false triggers;
380 dps: no false trigger, the softest ghost notes are missed). Real recordings decide.

## vdrum/ — shared code
- `protocol.py` — Python view of `firmware/common/protocol.h` (parser, encoder, crc8).
  The tests check its constants against the C header.
- `ports.py` — finds the receiver by USB ids; no port is ever hard-coded.

## Tests
```
python -m pytest tools -q
```
Also run in CI on Linux, Windows and macOS (`.github/workflows/tools.yml`).
