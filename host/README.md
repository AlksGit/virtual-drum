# Virtual Drum — host app

Native JUCE (C++17) app: 3D drum kit view, and later the sampler.
JUCE is fetched by CMake at configure time, pinned to tag **9.0.4** (see `docs/DECISIONS.md`).

## Prerequisites
| | Windows | macOS |
|---|---|---|
| CMake | ≥ 3.22 | ≥ 3.22 (`brew install cmake`) |
| Compiler | Visual Studio 2022 (or Build Tools) with "Desktop development with C++" | Xcode + command line tools |
| Git | required (CMake clones JUCE) | required |

The first configure needs network access to clone JUCE (~1–2 min); it is then cached in `build/`.

## Build
All commands from the `host/` directory.

Windows:
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```
Executable: `build/VirtualDrum_artefacts/Release/Virtual Drum.exe`

macOS:
```
cmake -S . -B build -G Xcode
cmake --build build --config Release --parallel
```
App bundle: `build/VirtualDrum_artefacts/Release/Virtual Drum.app`

`build/` is not portable between machines: after switching PC ↔ Mac, delete it and configure again.

### Windows: path length limit
JUCE is cloned inside `build/_deps/`, which creates deep paths. If the repo lives in a deep
directory, MSBuild fails with an error such as `exceeds the OS max path limit` (260 characters).
Either clone the repo close to the drive root (e.g. `C:\devirtual-drum`), or enable long paths
once from an admin PowerShell and reboot:
```
New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
```

## Offline / local JUCE
To build against a local JUCE checkout instead of downloading it (must be the same tag):
```
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
```

## Running the app
```
"Virtual Drum.exe"                 # Windows: receiver if detected, simulated drummer otherwise
"Virtual Drum.exe" --fake 120      # force the simulated drummer, at 120 bpm
"Virtual Drum.exe" --port COM5     # force a serial port
open "Virtual Drum.app" --args --fake 120   # macOS
```
In the 3D view: drag to orbit, mouse wheel to zoom, double-click to reset the camera.
Up / Down change the tempo of the simulated drummer, M mutes the sound. The overlay shows the
data source, the frame rate, the tempo and the audio device with its buffer size and latency.

Sound: each hit plays a drum sound synthesised at startup (no audio files in the repository),
with a gain that follows the velocity. The app asks for 48 kHz and the smallest buffer from
64 samples up, preferring ASIO, then "Windows Audio (Low Latency Mode)" on Windows. Many
built-in Windows drivers do not go below 480 samples (~10 ms) in shared mode; ASIO is needed
to reach the latency target there. CoreAudio on macOS normally accepts 64 samples.

## Tests
Unit tests (protocol, frame parser, serial port) build with the app and run with CTest:
```
ctest --test-dir build -C Release --output-on-failure
```
They also run in CI on Windows and macOS (`.github/workflows/host.yml`).

## vdrum_monitor
Command-line tool for hardware bring-up, built next to the app:
```
vdrum_monitor --list         # serial ports with their USB ids
vdrum_monitor                # auto-detect the receiver and print received frames
vdrum_monitor --port COM5    # read a given port (/dev/cu.usbmodem... on macOS)
vdrum_monitor --fake 120     # simulated drummer at 120 bpm, no hardware needed
```
By default it prints one summary per second (packet rates per stick and type, packets lost
from `seq` gaps). `--verbose` also prints every frame, which can slow the reading down.
The receiver is detected by its USB ids, defined in `firmware/common/protocol.h`.

## Layout
- `src/Main.cpp` — application and main window
- `src/MainComponent.*` — top-level content
- `src/SceneView.*` — 3D view, OpenGL 4.1 Core (renders on JUCE's GL thread)
- `src/AudioEngine.*` — audio output (JUCE device manager) playing the sampler
- `src/core/` — no JUCE dependency, unit tested:
  - `Packets.*` — C++ view of `protocol.h`, encode / decode
  - `FrameParser.*` — incremental USB frame parser with resynchronisation
  - `DataSource.h` — interface for anything that produces frames
  - `SerialDataSource.*`, `SerialPort*` — the USB receiver over a serial port
  - `FakeDrummer.*` — deterministic simulated drummer (groove, fills, stick motion)
  - `FakeDataSource.*` — plays the fake drummer in real time through the real wire format
  - `DrumKit.h` — hit zones of the kit (azimuth / elevation), `Quaternion.h` — orientation math
  - `KitLayout.h` — physical placement of the pads, `Mesh.*` — procedural meshes, `Mat4.h`
  - `SceneState.h` — thread-safe state shared by the data source and the renderer
  - `Sampler.*` — voices, velocity curve, panning; `HitQueue.h` — lock-free hits to the audio
    thread; `DrumSynth.*` — procedural drum sounds
- `src/tools/Monitor.cpp` — `vdrum_monitor`
- `tests/` — unit tests
