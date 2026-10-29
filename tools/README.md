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

## vdrum/ — shared code
- `protocol.py` — Python view of `firmware/common/protocol.h` (parser, encoder, crc8).
  The tests check its constants against the C header.
- `ports.py` — finds the receiver by USB ids; no port is ever hard-coded.

## Tests
```
python -m pytest tools -q
```
Also run in CI on Linux, Windows and macOS (`.github/workflows/tools.yml`).
