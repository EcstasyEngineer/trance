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

Dependencies are resolved by [vcpkg](https://github.com/microsoft/vcpkg) in manifest
mode (`vcpkg.json`) — `cmake` restores them automatically on first configure. There is
no vendored dependency blob to check out.

**Common setup (both platforms):**

```sh
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"      # bootstrap-vcpkg.bat on Windows
export VCPKG_ROOT="$HOME/vcpkg"       # setx VCPKG_ROOT on Windows
git clone https://github.com/EcstasyEngineer/trance && cd trance
```

### Windows (primary platform)

**Requirements:** Windows 10/11 x64, Visual Studio 2022 or later (with the C++ and
CMake components), vcpkg. The `windows-msvc` preset pins the `Visual Studio 17 2022`
generator — on VS 2026 change that line in `CMakePresets.json` to
`Visual Studio 18 2026`.

```sh
cmake --preset windows-msvc
cmake --build --preset windows-release
```

Outputs land in `build/windows-msvc/Release/`. The Windows build links dependencies statically
against the dynamic CRT (`x64-windows-static-md`), so the executables are largely
self-contained; vcpkg auto-deploys the few remaining runtime DLLs next to them.

### Linux / WSL (experimental)

Linux support is best-effort — the realtime renderer and the `creator` editor both
build, but there is no packaged release. Useful for development on WSL.

**Requirements:** a C++17 compiler (gcc 11+), CMake 3.25+, Ninja, vcpkg, and the
system packages vcpkg builds its ports against:

```sh
sudo apt install nasm yasm autoconf automake autoconf-archive libtool libtool-bin \
  pkg-config libx11-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev \
  libgl1-mesa-dev libglu1-mesa-dev libudev-dev libgtk-3-dev

cmake --preset linux-gcc
cmake --build --preset linux-release
```

Outputs land in `build/linux-gcc/`. Under WSL, the realtime window runs through WSLg's
translated OpenGL — fine for development, slower than a native GPU path.

## Contributing

Bug reports and pull requests are welcome. See the
[open issues](../../issues) for current work items.
