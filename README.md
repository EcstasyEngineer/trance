# trance

**trance** is a self-hypnosis tool that displays images, animations, and text in
randomly-generated patterns designed to aid induction and deepening.

## Features

- Randomly-generated visuals — no two sessions are the same
- Eight distinct visual modes (slow flash, subliminal text, parallel images, animation bursts, and more)
- Authorable visual patterns via the v3 pattern grammar ([docs/spec-grammar-v3.md](docs/spec-grammar-v3.md))
- In-app control panel (**F2**, ImGui) for live session editing and control
- Click-through always-on-top overlay mode (`--overlay`)
- System tray icon (Windows) + global **Shift+F11** hide-everything hotkey — see [docs/controls.md](docs/controls.md)
- Line-protocol control channel (`--command_port`) for external automation
- Hardware-accelerated rendering via OpenGL
- Audio support with multiple independent channels, plus binaural/isochronic entrainment beds
- Programmable playlist with conditionals and subroutines
- Video export (`.webm`, `.h264`, frame-by-frame `.jpg`/`.png`/`.bmp`)
- SteamVR (OpenVR) support

## Quick start

1. Download the [latest release](../../releases/latest) and extract it anywhere.
2. Drop a folder of images/GIFs next to `trance.exe` and run it directly — it will
   auto-generate a default session from whatever media it finds — or point it at a
   `*.session.json` file ([docs/session-json-format.md](docs/session-json-format.md)).
3. Press **F2** for the in-app control panel; quit with **Escape**, its
   **Quit trance** button, the tray icon, or closing the window. Full list of
   controls: [docs/controls.md](docs/controls.md).

## Usage

```sh
# play a session normally
trance.exe some.session.json

# force every visual selection to one built-in, by its v3 name -- for testing a single
# visual without hand-editing weights in the session:
trance.exe --visual=slow_flash some.session.json
# valid names: accelerate, slow_flash, sub_text, flash_text, simple, super_parallel,
# animation, super_fast. An unknown name is a fatal error at startup listing the valid
# ones -- it never falls through to "picked something else".

# force every visual selection to a single custom v3 pattern source file -- themes,
# entrainment, and playlist still come from the session/program as normal, only the
# visual schedule is overridden. A parse error prints the parser's line:col diagnostic
# and exits instead of falling back to a built-in.
trance.exe --pattern=my_pattern.v3 some.session.json

# --visual and --pattern are mutually exclusive.

# click-through always-on-top overlay over the desktop (see docs/controls.md for the
# escape routes -- the window intentionally ignores clicks and keys once engaged):
trance.exe --overlay some.session.json

# video export instead of a window:
trance.exe --export_path out.webm --export_length 60 some.session.json
```

## Data model

| Concept | Description |
|---|---|
| **Theme** | A collection of images, animations, fonts, and text lines, usually grouped around a subject |
| **Program** | A selection of themes plus display settings (speed, visual mode weights, etc.) |
| **Playlist** | A sequence of programs with timing, transitions, audio triggers, and optional branching/subroutines |
| **Session** | A complete file (`*.session.json`) containing themes, programs, and a playlist |

Sessions are plain JSON ([docs/session-json-format.md](docs/session-json-format.md) is the
normative spec) — edit them live from the in-app **F2** panel or in any text editor. Legacy
protobuf `.session` files from older versions convert with `trance_convert`. (The old
`creator.exe` graphical editor is deprecated.)

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
