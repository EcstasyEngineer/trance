# Architecture

Trance is one C++17 executable that loads a media session, runs a timed visual
program, and presents it on a desktop window with an optional OpenXR headset
output. SFML supplies the window, media and audio integration; rendering uses
OpenGL, and the in-app editor uses ImGui.

For setup, see the [README](../README.md). For the visual runtime's timing model,
see [engine-today.md](engine-today.md).

## Session to playback

1. `main()` loads JSON through `load_session()` and `load_session_json()`.
   `validate_session()` supplies defaults and repairs references. Legacy protobuf
   sessions are converted using the frozen `legacy.proto` schema. The current
   in-memory model still uses `trance_pb::Session`; JSON sidecar state preserves
   authoring information such as pattern file paths and theme scan roots.
2. `play_session()` creates `ThemeBank`, `Audio`, `ScreenRenderer`, `Director`,
   `PlaylistRunner`, and the UI/control surfaces.
3. `PlaylistRunner` selects programs and playlist transitions. The main loop
   applies their media/audio effects and reconciles runtime control requests.
4. `Director` parses built-in and custom v3 sources. A selected source becomes a
   `CompiledVisual`: a schedule tree, registers, and an ordered render block.
5. Content ticks call `Director::update()` and advance animation playback.
   Presentation calls `Director::render()`, which evaluates the render block for
   each output pass.

```text
session JSON + pattern files
            |
            v
  Session + JSON sidecar
            |
            v
   play_session / PlaylistRunner
            |
            v
         Director -----> CompiledVisual
            |             | schedule + registers + render expressions
            |             v
            |          VisualApiImpl <---- ThemeBank
            |             |               images / text / fonts / theme audio
            v             v
          Audio      ScreenRenderer
                          | desktop + optional OpenXR eye passes
```

The session being edited is live state. F2 commits autosave to its current path;
export writes another copy. The session/system JSON savers use a checked temporary
file and rename. Underscore-prefixed comment keys are not retained by a save.
Transient command overrides, such as theme/text pins, are held separately from
the session's persisted settings.

## Timing and execution ownership

There are several time domains:

- **Content ticks:** wall-clock elapsed time is converted into integer frames at
  the program's `global_fps`. Each tick advances the visual's schedule once.
- **Playlist time:** wall-clock milliseconds determine item changes. Pause/hide
  shifts the playlist's clock so hidden time does not consume an item.
- **Media time:** animation playback uses each file's frame delays, accumulated
  from content-tick durations. A pattern changes which animation plays; it does
  not prescribe the animation's encoded frame rate.
- **Presentation time:** the desktop or XR runtime paces output. Spiral rotation
  and warp accumulate playback elapsed time once per presented frame, with
  spiral rates expressed relative to 60 Hz. Multiple eye/desktop passes reuse
  that frame's state.
- **Audio time:** playback and synthesis have their own audio-stream timing.
  Grammar beat lengths are a frame-count snapshot derived from the program's
  pulsed entrainment layer, not sample-accurate audio callbacks.

These distinctions matter when investigating a stall: a held image, a stalled
schedule, a non-advancing render expression, and an animation waiting for a
decoder frame are different failures.

The frame loop owns playback/control mutations and GL operations. The theme
worker loads/unloads media; command socket and MCP readers queue requests for
the frame loop; the XR probe discovers an available runtime off the render path.
This describes intended ownership, not a claim that all concurrency is formally
verified. See [validation coverage](architecture-maturity.md).

## Renderer and controls

`ScreenRenderer` owns one window and GL context. An `XrOutput` can attach to that
context while the process runs. Active XR presentation draws left/right eye
passes and then the desktop pass. A minimized desktop pass may be skipped while
XR is pacing; a pending screenshot forces it back on. XR failure detaches the
headset output while desktop playback continues.

The event pump and XR frame handshake continue between content ticks and while
playback is paused. Pause/hide freezes content time and submits no visible XR
layers. F2 and the F1 overlay are desktop-only.

Keyboard, tray/hotkey, loopback TCP commands, and MCP over stdio converge on the
runtime state applied by `play_session()`. The transport does not own a second
playback engine. See [controls](controls.md), [command protocol](spec-mcp-ambient-daemon.md),
and [MCP setup](mcp-install.md).

## Code map

| Area | Starting points |
|---|---|
| Startup, CLI, frame loop, command dispatch | `src/trance/main.cpp` |
| Session load/save, validation, legacy conversion | `src/common/session*`, `src/common/trance.proto`, `src/common/legacy.proto` |
| Playlist stack and transitions | `src/trance/playlist_runner.{h,cpp}` |
| Pattern selection and GL draw operations | `src/trance/director.{h,cpp}` |
| Grammar, schedule, effects, render evaluation | `src/trance/visual/`; [visuals.md](visuals.md) |
| Theme selection, caching, fallback | `src/trance/theme_bank.{h,cpp}` |
| Image/animation decoding | `src/common/media/`, `src/trance/media/async_streamer.*` |
| Music, theme audio, entrainment synthesis | `src/trance/media/audio.*`, `entrainment.*`; [audio.md](audio.md) |
| Desktop and headset presentation | `src/trance/render/`; [XR design](spec-xr-unified.md) |
| F2 editor | `src/trance/ui/app_ui.*` |
| TCP and MCP transports | `src/trance/net/` |
| Overlay, global hotkey, tray, display information | `src/trance/platform/` |
| Session archive export | `src/common/session_archive.*` |
| Regression checks and live QA harness | `tests/`; [coverage and limits](architecture-maturity.md) |

The deleted wxWidgets creator, old grammar parsers, handwritten visual classes,
renderer selection, and video-export path are historical code. Current work
should follow the files above rather than old migration plans.
