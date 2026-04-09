# Convert Stereo to 5.1 
<div class="project-badges">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-blue" alt="Platform: Windows, macOS, Linux" loading="lazy">
  <img src="https://img.shields.io/badge/language-C%2B%2B17-orange" alt="Language: C++17" loading="lazy">
  <img src="https://img.shields.io/badge/framework-JUCE%208-cyan" alt="Framework: JUCE 8" loading="lazy">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License: MIT" loading="lazy">
</div>

# New Update: 
<img width="700" height="700" alt="image" src="https://github.com/user-attachments/assets/f808ebc9-56be-4a42-b1c5-04929429515d" />

Real-time desktop application that upmixes stereo music into discrete 5.1 output using JUCE.

This project is built in C++17 with CMake and targets low-latency multi-channel playback on real hardware.

## What this app does

- Plays stereo audio files (`.wav`, `.mp3`, `.flac`, `.aiff`, `.ogg`, `.aac`)
- Switches between:
  - `Stereo` (original L/R passthrough)
  - `5.1 Surround` (psychoacoustic upmix)
- Outputs six independent channels:
  - FL, FR, FC, LFE, SL, SR
- Provides live controls for center, LFE, surround field, and decorrelation
- Supports offline export to a 5.1 WAV file with current settings
- Supports batch export of multiple source files to 5.1 WAV
- Follows system output-device changes in real time
- Saves and loads custom presets (`.json`)
- Restores the last session state (mode, parameters, file, transport position)
- Includes an output safety limiter with live gain-reduction feedback
- Includes A/B snapshot compare (`Store/Recall A/B`)
- Includes per-channel `Solo` / `Mute` controls
- Includes pro metering stats (Peak dBFS, LUFS approx, clip count)
- Includes speaker calibration controls (per-channel trim/delay/polarity)

## Channel order (5.1)

| Index | Channel | Speaker |
|------:|---------|---------|
| 0 | FL | Front Left |
| 1 | FR | Front Right |
| 2 | FC | Front Center |
| 3 | LFE | Subwoofer |
| 4 | SL | Surround Left |
| 5 | SR | Surround Right |

<img width="300" height="300" alt="image" src="https://github.com/user-attachments/assets/773322b3-f163-40fe-8e19-6223fe5898c9" />

## DSP summary

- Mid/Side extraction from stereo input
- Front preservation for FL/FR
- Center extraction with HPF and gain
- LFE generation with LR4-style low-pass, shelf boost, and harmonic exciter
- Surround generation with side blend + center spill, HPF, Haas delay, and velvet-noise decorrelation
- Real-time meters and per-channel visual feedback

## Requirements

- CMake 3.22+
- C++17 compiler
- JUCE folder at `./JUCE`
- Playback device:
  - stereo output (minimum), or
  - 5.1 output (recommended)

### Windows

- Windows 10/11
- Visual Studio 2026 with `Desktop development with C++`

### macOS

- Xcode Command Line Tools
- Ninja (`brew install ninja`)

### Linux (Ubuntu/Debian)

- `build-essential`, `ninja-build`, `pkg-config`
- JUCE GUI/audio deps:
  - `libasound2-dev`, `libjack-jackd2-dev`, `ladspa-sdk`
  - `libfreetype6-dev`, `libx11-dev`, `libxcomposite-dev`, `libxcursor-dev`
  - `libxext-dev`, `libxinerama-dev`, `libxrandr-dev`, `libxrender-dev`
  - `libglu1-mesa-dev`, `mesa-common-dev`
  
## Build instructions

1. Clone the repository.
2. Ensure `JUCE/` exists next to `CMakeLists.txt`.

### Windows (Visual Studio)

```bat
mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON
cmake --build . --config Release
ctest --test-dir . -C Release --output-on-failure
```

Executable path:

```text
build\Surround51Upmixer_artefacts\Release\5.1 Surround Upmixer.exe
```

### macOS (Ninja)

```bash
mkdir -p build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
```

### Linux (Ninja)

```bash
mkdir -p build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
```

## Test Hardware
Testing was conducted on a Jazz Speakers J-9941A 5.1-channel home theater amplifier speaker system, which supports Dolby Digital and DTS audio formats.

<img width="600" height="600" alt="image" src="https://github.com/user-attachments/assets/38aafb89-7fbc-4703-b565-38d3d9abfe90" />

## First run

1. Launch the app.
2. Click `Open Audio File`.
3. Press `Play`.
4. Toggle between `STEREO` and `5.1 SURROUND`.
5. Tune the parameter panels while listening.
6. Use `Audio Settings` to select output device/channel config.
   
<img width="500" height="500" alt="image" src="https://github.com/user-attachments/assets/3ce3b5ad-08b4-439d-a530-871dc4775db5" />

## Ensure your audio output is set to 5.1 surround sound. <br/>
Before proceeding, verify that your system is configured for 5.1 speaker setup. This is essential for proper channel mapping and surround sound functionality

<img width="500" height="500" alt="image" src="https://github.com/user-attachments/assets/ae954c75-00f9-425f-b310-c0ec84e93fdd"/>

## Realtek audio console
<img width="500" height="500" alt="image" src="https://github.com/user-attachments/assets/c13efeed-0c82-448b-9bb0-ee5efbed0596" />

## Export to 5.1 WAV

1. Load a track.
2. Set desired controls.
3. Click `Export 5.1 WAV`.
4. Choose destination filename.

Notes:
- Export uses your current parameter snapshot.
- Export format is 5.1 WAV (24-bit).
- Export path also applies the same output safety limiter.

## Batch export

1. Click `Batch Export`.
2. Select multiple source files.
3. Select destination folder.
4. The app renders all files using your current upmix settings.

## Troubleshooting

- No sound:
  - Confirm file is loaded and transport is playing.
  - Check selected output device in `Audio Settings` or `Your System Output`.
- Only 2 channels active:
  - Your current device may be stereo.
  - Switch to a 5.1-capable device and configure it in `Windows Sound settings` or `Realtek audio console`.
- Wrong output after device switch:
  - The app polls and follows default output changes, but manually reselecting device in `Audio Settings` can force immediate sync.

## Project structure

```text
.
├─ CMakeLists.txt
├─ Source/
│  ├─ Main.cpp
│  ├─ MainComponent.h
│  ├─ MainComponent.cpp
|-- tests/
|   `-- UpmixEngineSmokeTest.cpp
|-- .github/workflows/
|   `-- windows-build.yml
└─ JUCE/
```
## Author
Mohamed Moslem Allouch

