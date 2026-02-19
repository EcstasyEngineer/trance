# trance

**trance** is a self-hypnosis tool that displays images, animations, and text in
randomly-generated patterns designed to aid induction and deepening.

## Features

- Randomly-generated visuals — no two sessions are the same
- Eight distinct visual modes (slow flash, subliminal text, parallel images, animation bursts, and more)
- Graphical session editor (`creator.exe`) with full control over all settings
- Hardware-accelerated rendering via OpenGL
- Audio support with multiple independent channels
- Programmable playlist with conditionals and subroutines
- Video export (`.webm`, `.h264`, frame-by-frame `.jpg`/`.png`/`.bmp`)
- SteamVR (OpenVR) support

## Quick start

1. Download the [latest release](../../releases/latest) and extract it anywhere.
2. Run `creator.exe` to build a session, or drop a folder of images/GIFs into the same
   directory and run `trance.exe` directly — it will auto-generate a default session
   from whatever media it finds.
3. Press **Escape** to exit fullscreen.

## Data model

| Concept | Description |
|---|---|
| **Theme** | A collection of images, animations, fonts, and text lines, usually grouped around a subject |
| **Program** | A selection of themes plus display settings (speed, visual mode weights, etc.) |
| **Playlist** | A sequence of programs with timing, transitions, audio triggers, and optional branching/subroutines |
| **Session** | A complete file (`.session`) containing themes, programs, and a playlist |

All parts of a session are edited with `creator.exe`.

## Supported file formats

| Type | Formats |
|---|---|
| Images | `.jpg` `.png` `.bmp` |
| Animations | `.gif` `.webm` |
| Fonts | `.ttf` |
| Text | `.txt` (for auto-generated sessions) |
| Audio | `.wav` `.flac` `.ogg` `.aiff` |
| Video export | `.webm` `.h264` `.jpg` `.png` `.bmp` |

## Building from source

**Requirements:** Windows 10/11 x64, Visual Studio 2022 or later

```
git clone https://github.com/EcstasyEngineer/trance
cd trance
# Open build/trance.sln in Visual Studio, select Release|x64, build
# Or from the command line:
cmake --build build --config Release --parallel
```

Outputs land in `build/Release/`. All dependencies are vendored under `dependencies/` —
no separate install step needed.

## Contributing

Bug reports and pull requests are welcome. See the
[open issues](../../issues) for current work items.
