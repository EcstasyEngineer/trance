# Architecture

A navigable map of the `trance` codebase: what the two executables are, how a
`*.session.json` becomes pixels on screen, and where to start reading for each
subsystem. For the feature list and build instructions, see the
[README](../README.md).

## Two executables, one session model

`trance` ships as two binaries that share the in-memory session model (the
`trance_pb::Session` protobuf, `src/common/trance.proto`) and the `common`
support code, but otherwise have no runtime dependency on each other:

- **`trance`** — the realtime player. Loads a session, runs the frame loop,
  and renders to a window (or VR headset). Entry point: `src/trance/main.cpp`.
- **`trance_convert`** — a one-shot legacy-proto → JSON converter.

The **on-disk format is JSON**: `*.session.json`, spec in
[session-json-format.md](session-json-format.md), loader
`src/common/session_json.cpp`. The proto is the frozen *in-memory* model only —
legacy protobuf `.session` files are no longer read directly and convert via
`trance_convert`.

Editing happens in the in-app F2 (ImGui) panel or directly in the JSON. A third
binary, a wxWidgets `creator` editor, used to ship and was deleted; the handful of
things it did that F2 still does not are tracked in
[architecture-maturity.md](architecture-maturity.md).

## Runtime data flow (the player)

The player's lifecycle lives in `play_session()` (`src/trance/main.cpp`):

1. **Load.** `main()` calls `load_session()` (`src/common/session.cpp`), which
   requires a `*.session.json` path and parses it via `load_session_json()`
   (`src/common/session_json.cpp`) into the in-memory `trance_pb::Session`; a
   legacy `.session`/`.cfg` proto path is rejected with a fatal hint to run
   `trance_convert`. `validate_session()` then fills in defaults and repairs
   dangling references. If the file is missing it falls back to
   `get_default_session()` and `search_resources()` (auto-generates a session
   from media found next to the binary).
2. **Theme bank.** A `ThemeBank` (`src/trance/theme_bank.{h,cpp}`) is constructed
   for the active program. It keeps two themes active in video memory and
   asynchronously loads a third on a background thread (`run_async_thread` in
   `main.cpp`) so themes can swap without a load stall.
3. **Renderer.** One of three `Renderer` subclasses (`src/trance/render/`) is
   chosen: `ScreenRenderer` (window), `OpenVrRenderer` (SteamVR), or
   `OpenXrRenderer` (OpenXR quad layer). See the renderer section below.
4. **Director.** The `Director` (`src/trance/director.{h,cpp}`) owns the visual
   engine and the GL programs. It holds a `Visual` (the current pattern), the
   `VisualApiImpl` bridge to the theme bank and renderer, and the compiled
   built-in / custom pattern tables.
5. **Audio.** An `Audio` object (`src/trance/media/audio.{h,cpp}`) plays per-channel
   music and synthesises the entrainment bed. Always constructed — `Director` holds
   it by reference, so there is no audio-less configuration.
6. **Frame loop.** `play_session()` converts wall-clock time into frame ticks at
   the program's `global_fps`, advances the playlist state machine, then per
   frame calls `director.update()` (advance the visual's cycler tree, pull
   images) and `director.render()`. The playlist "VM" (standard items,
   subroutines, weighted/conditional branching) lives in `PlaylistRunner`
   (`src/trance/playlist_runner.{h,cpp}`), which owns the stack and the switch
   clock while `play_session()` supplies the wall clock and the per-item side
   effects — see [sessions-and-playlists.md](sessions-and-playlists.md).

```
*.session.json
   │  load_session (-> load_session_json) + validate_session   (common/session*.cpp)
   ▼
trance_pb::Session ──► ThemeBank      (image/animation/text/font supply)
   │                       ▲
   │                       │ get_image / get_text / ...  via VisualApiImpl
   ▼                       │
play_session frame loop ──► Director ──► Visual (cycler tree + effects)
   │  (playlist state machine)   │            │
   │                             │            ▼
   ├──► Audio (channels + entrainment bed)  VisualApiImpl (VisualControl + VisualRender)
   │                                          │
   └──────────────────────────────────────► Renderer ──► screen / VR headset
```

## `src/` directory tour

| Path | What lives here |
|---|---|
| `src/common/` | Shared, executable-agnostic code: the `trance.proto` in-memory schema, the JSON loader/saver (`session_json.{h,cpp}`), session load/save/validate (`session.{h,cpp}`), the legacy-proto reader for `trance_convert` (`session_legacy.{h,cpp}`), small utilities (`util.h`, `common.h`). |
| `src/common/media/` | Decoders shared by both binaries: `Image`, the `Streamer` animation interface (`streamer.{h,cpp}`). |
| `src/trance/` | The realtime player: `main.cpp`, `director.{h,cpp}`, `theme_bank.{h,cpp}`, `playlist_runner.{h,cpp}` (the playlist stack machine), GLSL `shaders.h`. |
| `src/trance/media/` | Player-side media: `audio.{h,cpp}`, `entrainment.{h,cpp}` (the synthesised bed), `font.{h,cpp}`, `async_streamer.{h,cpp}`. |
| `src/trance/render/` | The `Renderer` interface and its subclasses: `render.{h,cpp}` (screen — also home of the click-through overlay window hints, `apply_overlay_hints` / `clear_overlay_hints`), `openvr.{h,cpp}` (SteamVR), `openxr.{h,cpp}` (OpenXR head-locked quad-layer backend — Quest Link, any conformant runtime). |
| `src/trance/visual/` | The visual engine: the cycler/pattern system. The v3 pattern DSL parser/compiler, the `Cycler` tree, compiled visuals, and the data-driven render blocks (`render_eval`). |
| `src/trance/ui/` | The ImGui in-app control panel (`app_ui.{h,cpp}`), toggled with F2. |
| `src/trance/net/` | The `--command_port` control channel: line→verb protocol (`command_protocol.{h,cpp}`) and the socket/mailbox (`command_channel.{h,cpp}`). |
| `src/trance/platform/` | Out-of-window controls (`system_control.{h,cpp}`): the system tray icon (Windows) and the global Shift+F11 hide-everything hotkey (Win32/X11) — the control surface that keeps working while the overlay is click-through. |

## Where to start reading, per subsystem

- **Player lifecycle / frame loop** → `src/trance/main.cpp`, function
  `play_session()`. The playlist state machine it drives is
  `src/trance/playlist_runner.h`.
- **Visual engine** → [visuals.md](visuals.md). Start at `Director::change_visual`
  (`src/trance/director.cpp`) for selection, then `compiled_visual.{h,cpp}`
  and `cyclers.{h,cpp}`.
- **Authoring a custom pattern** → [authoring-v3-patterns.md](authoring-v3-patterns.md);
  grammar in `src/trance/visual/pattern_parser_v3.cpp` (the only parser — v1 retired).
- **Sessions / playlists / proto schema** →
  [sessions-and-playlists.md](sessions-and-playlists.md); schema in
  `src/common/trance.proto`.
- **Audio + entrainment** → [audio.md](audio.md);
  `src/trance/media/audio.{h,cpp}` and `entrainment.{h,cpp}`.
- **Rendering** → `src/trance/render/render.h` (the `Renderer` interface and
  `Renderer::State` per-eye enum). `Director::render_image` / `render_spiral` /
  `render_text` (`src/trance/director.cpp`) hold the actual GL draw calls.
- **Theme supply** → `src/trance/theme_bank.h` (the class comment explains the
  two-active-plus-one-loading scheme).
- **Overlay mode / out-of-window controls** → `apply_overlay_hints`
  (`src/trance/render/render.h`) for the click-through window itself;
  `src/trance/platform/system_control.h` for the tray icon + global Shift+F11
  hide-everything hotkey that control it.
- **Command channel** → `src/trance/net/command_protocol.h`; verb reference in
  [spec-mcp-ambient-daemon.md](spec-mcp-ambient-daemon.md).
- **Editor** → `src/trance/ui/app_ui.h` (the F2 panel).
- **Session bundling** → `src/common/session_archive.h` (`--export_archive`; the
  header comment explains why there is no matching importer).

## Controls (realtime)

Full user-facing reference: [controls.md](controls.md).

In-window keys are handled in `handle_events()` (`src/trance/main.cpp`):
window-close and **Escape** quit, **F2** toggles the ImGui control panel
(`src/trance/ui/app_ui.{h,cpp}`, which carries the "Quit trance" button), **F1**
toggles the debug overlay (`Director::draw_debug_overlay`,
`src/trance/director.cpp`), **M** toggles audio mute (`Audio::ToggleMute`).

Outside the window (works even when the overlay makes the window click-through):
the global **Shift+F11** hide-everything toggle (first press: window hidden +
paused + muted; next press restores the pre-hide state) and the Windows **system
tray icon** menu (hide/show, overlay + opacity nudge, pause, panel, quit) — both
in `src/trance/platform/system_control.{h,cpp}` — plus the `--command_port`
channel (including `hide`/`show`) and SIGINT/SIGTERM.
