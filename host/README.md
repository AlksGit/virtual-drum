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

## Offline / local JUCE
To build against a local JUCE checkout instead of downloading it (must be the same tag):
```
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
```

## Layout
- `src/Main.cpp` — application and main window
- `src/MainComponent.*` — top-level content
- `src/SceneView.*` — OpenGL 4.1 Core context for the 3D view (renders on JUCE's GL thread)
