# Architecture

A navigable map of the `trance` codebase: what the two executables are, how a
`.session` becomes pixels on screen, and where to start reading for each
subsystem. For the feature list and build instructions, see the
[README](../README.md).

## Two executables, one session model

`trance` ships as two separate binaries that share the protobuf session schema
(`src/common/trance.proto`) and the `common` support code, but otherwise have no
runtime dependency on each other:

- **`trance`** — the realtime player. Loads a `.session`, runs the frame loop,
  and renders to a window (or VR headset, or a video file). Entry point:
  `src/trance/main.cpp`.
- **`creator`** — a wxWidgets GUI editor for building and saving `.session`
  files. Separate executable, separate `main`. Entry point:
  `src/creator/main.cpp`.

The interchange format is the `trance_pb::Session` message. `creator` writes it,
`trance` reads it. Neither knows anything about the other's internals.

### The `creator` editor

`creator` is a wxWidgets desktop app (`src/creator/`, ~17 files). `CreatorFrame`
(`src/creator/main.cpp`) hosts a `wxNotebook` with one page per part of the data
model — themes (`theme.{h,cpp}`), programs (`program.{h,cpp}`), the playlist
(`playlist.{h,cpp}`), and session variables (`variables.{h,cpp}`) — plus a system
settings dialog (`settings.{h,cpp}`). It edits an in-memory `trance_pb::Session`
and serialises it with `save_session` (`src/common/session.cpp`). It can also
launch the player (`launch.{h,cpp}`) and drive a video export (`export.{h,cpp}`).
The editor has no rendering or visual-engine code of its own — it only produces
the proto that the player consumes. (No deeper editor-internals doc exists yet;
this paragraph is the whole map.)

## Runtime data flow (the player)

The player's lifecycle lives in `play_session()` (`src/trance/main.cpp:102`):

1. **Load.** `main()` calls `load_session()` (`src/common/session.cpp:465`) to
   parse the `.session` proto, then `validate_session()` fills in defaults and
   repairs dangling references. If the file is missing it falls back to
   `get_default_session()` and `search_resources()` (auto-generates a session
   from media found next to the binary).
2. **Theme bank.** A `ThemeBank` (`src/trance/theme_bank.{h,cpp}`) is constructed
   for the active program. It keeps two themes active in video memory and
   asynchronously loads a third on a background thread (`run_async_thread` in
   `main.cpp`) so themes can swap without a load stall.
3. **Renderer.** One of three `Renderer` subclasses (`src/trance/render/`) is
   chosen: `ScreenRenderer` (window), `OpenVrRenderer` (SteamVR), or
   `VideoExportRenderer` (offline encode). See the renderer section below.
4. **Director.** The `Director` (`src/trance/director.{h,cpp}`) owns the visual
   engine and the GL programs. It holds a `Visual` (the current pattern), the
   `VisualApiImpl` bridge to the theme bank and renderer, and the compiled
   built-in / custom pattern tables.
5. **Audio.** In realtime mode an `Audio` object (`src/trance/media/audio.{h,cpp}`)
   plays per-channel music and synthesises the entrainment bed. Not created for
   video export.
6. **Frame loop.** `play_session()` converts wall-clock time into frame ticks at
   the program's `global_fps`, advances the playlist state machine, then per
   frame calls `director.update()` (advance the visual's cycler tree, pull
   images) and `director.render()`. The playlist "VM" (standard items,
   subroutines, weighted/conditional branching) is driven by the `stack` in
   `play_session()` — see [sessions-and-playlists.md](sessions-and-playlists.md).

```
.session (protobuf)
   │  load_session + validate_session   (common/session.cpp)
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
   └──────────────────────────────────────► Renderer ──► screen / VR / video file
```

## `src/` directory tour

| Path | What lives here |
|---|---|
| `src/common/` | Shared, executable-agnostic code: the `trance.proto` schema, session load/save/validate (`session.{h,cpp}`), small utilities (`util.h`, `common.h`). |
| `src/common/media/` | Decoders shared by both binaries: `Image`, the `Streamer` animation interface (`streamer.{h,cpp}`). |
| `src/trance/` | The realtime player: `main.cpp`, `director.{h,cpp}`, `theme_bank.{h,cpp}`, GLSL `shaders.h`. |
| `src/trance/media/` | Player-side media: `audio.{h,cpp}`, `entrainment.{h,cpp}` (the synthesised bed), `font.{h,cpp}`, `async_streamer.{h,cpp}`, video `export.{h,cpp}`. |
| `src/trance/render/` | The `Renderer` interface and its subclasses: `render.{h,cpp}` (screen), `openvr.{h,cpp}` (SteamVR), `video_export.{h,cpp}` (offline encode). |
| `src/trance/visual/` | The visual engine: the cycler/pattern system (~23 files). The pattern DSL parser/compiler, the `Cycler` tree, compiled visuals, the data-driven render blocks (`render_eval`), and the headless tests. |
| `src/creator/` | The wxWidgets `.session` editor (separate executable). |
| `src/jpgd/` | Vendored JPEG decoder (third-party). |

## Where to start reading, per subsystem

- **Player lifecycle / frame loop** → `src/trance/main.cpp`, function
  `play_session()` (`:102`). The playlist state machine is the `while (true)`
  block at `:223`.
- **Visual engine** → [visuals.md](visuals.md). Start at `Director::change_visual`
  (`src/trance/director.cpp:384`) for selection, then `compiled_visual.{h,cpp}`
  and `cyclers.{h,cpp}`.
- **Authoring a custom pattern** → [authoring-visual-patterns.md](authoring-visual-patterns.md);
  grammar in `src/trance/visual/pattern_parser.h`.
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
- **Editor** → `src/creator/main.cpp`.

## Controls (realtime)

Handled in `handle_events()` (`src/trance/main.cpp:68`): **Escape**/window-close
quits, **F1** toggles the debug overlay (`Director::draw_debug_overlay`,
`director.cpp:743`), **M** toggles audio mute (`Audio::ToggleMute`).
