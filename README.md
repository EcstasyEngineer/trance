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
- Built-in MCP server (`--mcp`, beta) — an MCP host launches `trance --mcp` and drives
  playback through tool calls; see [docs/mcp-install.md](docs/mcp-install.md)
- Hardware-accelerated rendering via OpenGL
- Audio support with multiple independent channels, plus binaural/isochronic entrainment beds
- Programmable playlist with conditionals and subroutines
- Session bundling: `--export_archive out.trance` writes a plain zip of the session file
  plus every asset it references
- VR output: SteamVR (OpenVR) stereo, or a head-locked OpenXR quad that stays straight
  ahead and survives window occlusion — **neither backend has been verified against a
  physical headset**; see [VR setup](#vr-setup)

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

# pick the renderer for this run only (system.json is not modified):
trance.exe --renderer=openxr some.session.json
# valid names: monitor, openvr, openxr. An unknown name is a fatal error at startup.

# serve MCP on stdin/stdout so an MCP host (e.g. Claude Desktop) can drive playback --
# the binary itself is the server, no sidecar process (see docs/mcp-install.md):
trance.exe --mcp some.session.json

# bundle a session and every asset it references into a portable zip, then exit:
trance.exe --export_archive out.trance some.session.json
# `.trance` is a plain zip -- the session file is stored as session.json at the zip root
# and every asset keeps its root-relative path, so any zip tool extracts it straight back
# into a working session root. There is deliberately no importer to match.
```

## VR setup

> **Status: unverified on hardware.** Both VR backends compile and have been reviewed and
> corrected against the OpenXR/OpenVR specs, but **neither has ever run against a physical
> headset** — the development machine has none. Treat everything in this section as the
> intended behaviour rather than observed behaviour, and expect to debug. If you do run it
> on a headset, the failure banner described at the end of this section is the first thing
> to read.

**Choosing a renderer.** Three ways, in increasing precedence:

1. The `"renderer"` key in `system.json` — `"monitor"`, `"openvr"` or `"openxr"`.
   **A missing key means monitor mode**, and the saver *omits* the key whenever it is
   monitor — so a `system.json` with no `renderer` line is a complete, deliberate
   non-VR config, not an unset one. This is the usual cause of "I launched it under
   SteamVR and only got a flat floating window": that window is SteamVR mirroring the
   ordinary desktop output, not trance rendering to the headset.
2. The F2 panel's **System → Renderer** radios, which write `system.json` immediately
   but **take effect on the next launch** — the renderer is constructed once at startup.
3. `--renderer=monitor|openvr|openxr`, which overrides both for that run and is never
   written back.

**The two VR backends.** `openvr` renders a stereo projection through SteamVR.
`openxr` submits a head-locked quad one metre ahead of the view — the image stays
centred wherever you look, and nothing on the desktop can occlude it. Both paths are
stereo: the OpenXR path renders the scene once per eye and submits one eye-restricted
quad each, so the parallax lives in the rendered content while the quads themselves
share a single head-locked pose. The OpenXR path is currently Windows-only (the
`XR_KHR_opengl_enable` binding it uses is the Win32 one).

**Which OpenXR runtime is used is not ours to pick.** We create a plain OpenXR
instance, so whichever runtime is registered **active** on the machine wins — Oculus /
Quest Link if that is set, SteamVR's OpenXR runtime if that is. trance has no runtime
selection logic. Set the active runtime in the Oculus or SteamVR desktop app before
launching.

**If VR fails to start**, trance still plays the session on the desktop window, but it
says so loudly: a `*** VR UNAVAILABLE: ... ***` banner on stdout/stderr and a
persistent red line at the top of the F2 panel naming the backend that was requested
and where the request came from. The specific cause is printed by the backend itself
just above the banner.

## Data model

| Concept | Description |
|---|---|
| **Theme** | A collection of images, animations, fonts, and text lines, usually grouped around a subject |
| **Program** | A selection of themes plus display settings (speed, visual mode weights, etc.) |
| **Playlist** | A sequence of programs with timing, transitions, audio triggers, and optional branching/subroutines |
| **Session** | A complete file (`*.session.json`) containing themes, programs, and a playlist |

Sessions are plain JSON ([docs/session-json-format.md](docs/session-json-format.md) is the
normative spec) — edit them live from the in-app **F2** panel or in any text editor. Legacy
protobuf `.session` files from older versions are auto-migrated to JSON on load. (The old
`creator.exe` graphical editor has been removed; F2 and a text editor replace it.)

**The loaded session is live state, not a document.** trance opens `./default.json`
(bootstrapping one on a cold start) or the file named on the command line, and the F2
panel writes every edit straight back to it — there is no Save button and nothing to
lose by quitting. The panel's **System → Export** writes a *copy* elsewhere, leaving the
live file playing and saving; `--export_archive=<file>` writes a copy with the media
bundled in. Writes go through a temp file and a rename, so an interrupted save cannot
truncate a session.

## Supported file formats

| Type | Formats |
|---|---|
| Images | `.jpg` `.png` `.bmp` |
| Animations | `.gif` `.webm` |
| Fonts | `.ttf` |
| Text | `.txt` (for auto-generated sessions) |
| Audio | `.wav` `.flac` `.ogg` `.aiff` |

## Documentation

For players and session authors:

- [docs/controls.md](docs/controls.md) — every runtime control: keys, tray icon, global hotkeys, overlay escape routes
- [docs/sessions-and-playlists.md](docs/sessions-and-playlists.md) — authoring sessions and playlists
- [docs/session-json-format.md](docs/session-json-format.md) — the normative `*.session.json` spec
- [docs/visuals.md](docs/visuals.md) — the visual system and built-in visuals, as built
- [docs/audio.md](docs/audio.md) — music channels and the entrainment bed
- [docs/authoring-v3-patterns.md](docs/authoring-v3-patterns.md) — writing your own visual patterns
- [docs/mcp-install.md](docs/mcp-install.md) — registering `trance --mcp` with an MCP host

For developers:

- [docs/architecture.md](docs/architecture.md) — a navigable map of the codebase
- [docs/engine-today.md](docs/engine-today.md) — plain-English ground truth of the visual engine; the contract the grammar compiles down to
- [docs/spec-grammar-v3.md](docs/spec-grammar-v3.md) — the v3 pattern grammar design spec
- [docs/spec-mcp-ambient-daemon.md](docs/spec-mcp-ambient-daemon.md) — command channel / MCP verb spec

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

**Requirements:** Windows 10/11 x64, Visual Studio 2026 (with the C++ and CMake
components), vcpkg. The `windows-msvc` preset pins the `Visual Studio 18 2026`
generator — on an older Visual Studio, change that line in `CMakePresets.json` to
match (e.g. `Visual Studio 17 2022`).

```sh
cmake --preset windows-msvc
cmake --build --preset windows-release
```

If you have Visual Studio but have not set up vcpkg separately, run **`build.bat`**
instead: the preset's toolchain file requires `VCPKG_ROOT`, and MSVC requires a
vcvars64 environment, neither of which is set on a fresh VS-only box. `build.bat`
discovers both (via `vswhere`, using VS's bundled vcpkg and CMake) and then runs the
two preset commands above unchanged. It is a bootstrap wrapper, not a separate build
path — once `VCPKG_ROOT` is set, use the presets directly.

Outputs land in `build/windows-msvc/Release/`. The Windows build links dependencies statically
against the dynamic CRT (`x64-windows-static-md`), so the executables are largely
self-contained; vcpkg auto-deploys the few remaining runtime DLLs next to them.

### Linux / WSL (experimental)

Linux support is best-effort — the realtime renderer builds, but there is no packaged
release. Useful for development on WSL.

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

## License

[WTFPL](LICENSE).
