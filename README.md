# Stereo to 5.1 
<div class="project-badges">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-blue" alt="Platform: Windows, macOS, Linux" loading="lazy">
  <img src="https://img.shields.io/badge/language-C%2B%2B17-orange" alt="Language: C++17" loading="lazy">
  <img src="https://img.shields.io/badge/framework-JUCE%208-cyan" alt="Framework: JUCE 8" loading="lazy">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License: MIT" loading="lazy">
</div>
<br/>
<img width="968" height="1022" alt="image" src="https://github.com/user-attachments/assets/26f339b0-3a17-4906-88f1-ca6fd4946d6a" />

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
- Supports offline export to a 5.1 WAV file with your current settings
- Follows system output-device changes in real time

## Channel order (5.1)

| Index | Channel | Speaker |
|------:|---------|---------|
| 0 | FL | Front Left |
| 1 | FR | Front Right |
| 2 | FC | Front Center |
| 3 | LFE | Subwoofer |
| 4 | SL | Surround Left |
| 5 | SR | Surround Right |

<img width="409" height="360" alt="image" src="https://github.com/user-attachments/assets/773322b3-f163-40fe-8e19-6223fe5898c9" />

## DSP summary

- Mid/Side extraction from stereo input
- Front preservation for FL/FR
- Center extraction with HPF and gain
- LFE generation with LR4-style low-pass, shelf boost, and harmonic exciter
- Surround generation with side blend + center spill, HPF, Haas delay, and velvet-noise decorrelation
- Real-time meters and per-channel visual feedback

## Requirements

- Windows 10/11 (primary tested target)
- Visual Studio 2026 with `Desktop development with C++`
- CMake 3.22+
- A playback device with:
  - stereo output (minimum), or
  - 5.1 output (recommended for full experience)

## Build instructions (Windows)

1. Clone the repository.
2. Make sure a `JUCE/` folder exists next to `CMakeLists.txt`.
3. Build:

```bat
mkdir build
cd build
cmake .. -G "Visual Studio 18 2026" -A x64
cmake --build . --config Release
```

Executable path:

```text
build\Surround51Upmixer_artefacts\Release\5.1 Surround Upmixer.exe
```

## First run

1. Launch the app.
2. Click `Open Audio File`.
3. Press `Play`.
4. Toggle between `STEREO` and `5.1 SURROUND`.
5. Tune the parameter panels while listening.
6. Use `Audio Settings` to select output device/channel config.
   <img width="647" height="470" alt="image" src="https://github.com/user-attachments/assets/3ce3b5ad-08b4-439d-a530-871dc4775db5" />

"Ensure your audio output is set to 5.1 surround sound." <br/>
Before proceeding, verify that your system is configured for 5.1 speaker setup. This is essential for proper channel mapping and surround sound functionality
<img width="1059" height="897" alt="image" src="https://github.com/user-attachments/assets/c13efeed-0c82-448b-9bb0-ee5efbed0596" />

## Export to 5.1 WAV

1. Load a track.
2. Set your desired controls.
3. Click `Export 5.1 WAV`.
4. Choose destination filename.

Notes:
- Export uses your current parameter values snapshot.
- Export format is 5.1 WAV (24-bit writer path in current code).

## Troubleshooting

- No sound:
  - Confirm file is loaded and transport is playing.
  - Check selected output device in `Audio Settings` or `Your System Output`.
- Only 2 channels active:
  - Your current device may be stereo.
  - Switch to a 5.1-capable device and configure it in Windows Sound settings.
  - <img width="1059" height="897" alt="image" src="https://github.com/user-attachments/assets/c13efeed-0c82-448b-9bb0-ee5efbed0596" />

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
└─ JUCE/
```
## Author

Mohamed Moslem Allouch

