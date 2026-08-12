# trance

**trance** is a self-hypnosis tool that displays images, animations, and text in
randomly-generated patterns designed to aid induction and deepening.

Three priorities govern every design decision: **fast** (native-rate rendering, no
wasted work), **small** (one exe, few dependencies), and **easy to use** (zero
configuration — things just work, and say so loudly on the console when they can't).

## Features

- Randomly-generated visuals — no two sessions are the same
- Eight distinct visual modes (slow flash, subliminal text, parallel images, animation bursts, and more)
- Authorable visual patterns via the v3 pattern grammar ([docs/spec-grammar-v3.md](docs/spec-grammar-v3.md))
- In-app control panel (**F2**, ImGui) for live session editing and control
- Click-through always-on-top overlay mode (`--overlay`)
- System tray icon (Windows) + global **Shift+F11** hide-everything hotkey — see [docs/controls.md](docs/controls.md)
- Line-protocol control channel (`--command_port`) for external automation
- Built-in MCP server (`--mcp`, beta) — an MCP host launches `trance --mcp` and drives
  playback through tool calls; register it with
  `claude mcp add trance -- "C:\path\to\trance.exe" "C:\path\to\session.json" --mcp --hidden`
  and see [docs/mcp-install.md](docs/mcp-install.md)
- Unattended start (`--hidden`, `--muted`) — come up in silent running and wait to be
  summoned, instead of appearing the instant a control host connects
- Hardware-accelerated rendering via OpenGL
- Audio support with multiple independent channels, plus binaural/isochronic entrainment beds
- Programmable playlist with conditionals and subroutines
- Session bundling: `--export_archive out.trance` writes a plain zip of the session file
  plus every asset it references
- VR output: a head-locked OpenXR quad in the headset *alongside* the desktop window —
  no mode to pick and nothing to configure, a headset attaches whenever it appears and
  detaches without ever interrupting the session — **not yet verified against a physical
  headset**; see [VR setup](#vr-setup)

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

# serve MCP on stdin/stdout so an MCP host (e.g. Claude Desktop) can drive playback --
# the binary itself is the server, no sidecar process (see docs/mcp-install.md):
trance.exe --mcp some.session.json
# ...but running that by hand is the one thing you never do: the HOST spawns the process
# when it connects. Register it once instead, and it is launched for you --
#   claude mcp add trance -- "C:\path\to\trance.exe" "C:\path\to\session.json" --mcp --hidden

# start in silent running: the window is never mapped, playback is paused and audio muted,
# and the process sits alive waiting for `show`. This is what --hidden is FOR: an MCP host
# owns the process lifecycle, so without it connecting a host puts the fullscreen player on
# screen before any tool call could hide it. --muted is the same idea for audio alone, and
# unlike --hidden's mute it survives a `show`:
trance.exe --hidden --muted --mcp some.session.json

# bundle a session and every asset it references into a portable zip, then exit:
trance.exe --export_archive out.trance some.session.json
# `.trance` is a plain zip -- the session file is stored as session.json at the zip root
# and every asset keeps its root-relative path, so any zip tool extracts it straight back
# into a working session root. There is deliberately no importer to match.
```

## VR setup

> **Status: unverified on hardware.** The OpenXR output compiles, has been reviewed and
> corrected against the OpenXR spec, and its failure paths have been exercised on a
> machine with no VR software at all — but it **has never run against a physical
> headset**, because the development machine has none. Treat everything in this section as
> the intended behaviour rather than observed behaviour, and expect to debug. If you do
> run it on a headset, the console lines described at the end of this section are the
> first thing to read. The hardware acceptance matrix that would change this status is
> §5 of [docs/spec-xr-unified.md](docs/spec-xr-unified.md).

**There is nothing to configure.** VR is not a mode you select and not a renderer you
pick: there is one renderer, it owns the window, and a headset is an extra *output* of
it. There is no `renderer` setting, no F2 radios and no `--renderer` flag — they were
deleted along with the SteamVR-specific OpenVR backend (SteamVR is itself an OpenXR
runtime, so nothing is lost by going through OpenXR). The full plan and its rationale:
[docs/spec-xr-unified.md](docs/spec-xr-unified.md).

**Attaching is automatic and continuous.** For the whole run, trance looks for a headset
every 5 seconds in the background, and one that appears — Link started, headset donned,
runtime switched — simply joins as a second output, without a restart and without
interrupting what is already playing. The reverse is just as undramatic: any XR failure
(Link killed, runtime quit, session lost, headset doffed) detaches the headset, the
desktop plays straight through it without so much as a dropped audio sample, and probing
resumes for the next one. The look-ahead costs nothing on a machine with no VR software:
it is one registry read, and nothing is loaded into the process at all.

**What the headset shows.** A head-locked quad one metre ahead of the view — the image
stays centred wherever you look, and nothing on the desktop can occlude it. It is
stereo: the scene renders once per eye and each eye gets its own eye-restricted quad,
so the parallax lives in the rendered content while the quads share a single head-locked
pose. Windows-only (the `XR_KHR_opengl_enable` binding it uses is the Win32 one).

**The desktop keeps playing the same session at the same time.** The window is not a
blit of an eye texture — it is a third render pass of the same frame, so it shows the
scene without the per-eye shear and at its own aspect ratio. That also means the **F2
panel works normally while the headset plays**: the panel and the F1 debug overlay are
drawn in the desktop pass only, never in the headset, and every edit applies live to both
outputs.

**A minimized window does not slow the headset down** (the VRChat property). While the
headset is attached it paces the loop itself, and the desktop present is taken out of
the way entirely while the window is minimized — so you can minimize trance, use the
machine, and the headset keeps running at its native rate. Restoring the window brings it
back to the current frame. Content advances on playback time, not on presentation rate,
so a visual runs at the same speed whether a 90 Hz headset is attached or not.

**Which OpenXR runtime is used is not ours to pick.** We create a plain OpenXR
instance, so whichever runtime is registered **active** on the machine wins — Oculus /
Quest Link if that is set, SteamVR's OpenXR runtime if that is. trance has no runtime
selection logic. Set the active runtime in the Oculus or SteamVR desktop app before
launching. Switching it while nothing is attached is enough; the next probe lands on the
new one.

**If a headset can't be attached**, trance says so once — a single line on stderr, plus
a persistent red line at the top of the F2 panel — and repeats it only when the answer
changes, so a console left running overnight stays readable. It distinguishes the cases
that matter: no OpenXR runtime registered on the machine, a runtime registered but
unreachable, a runtime that is up but has no HMD attached, and a session that could not
be created. An attach prints its own line, naming the runtime and the per-eye resolution;
so does a detach, naming what failed. `status` over the command channel reports the same
thing as `xr=off|unattached|attached|attached-idle`.

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
