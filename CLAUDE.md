# Working in this repository

Trance is a C++17/SFML 3 visual media player with JSON sessions, V4 text/AST
patterns, browser and ImGui editors, audio/entrainment, desktop overlays, TCP commands,
MCP over stdio, and an optional OpenXR output. There is one product executable
and one renderer/window. Read the [architecture](docs/architecture.md) for the
code map and the [README](README.md) for user-facing setup.

## Working rules

- Priorities are fast, small, and easy to use. Prefer a small general mechanism
  to a special case for one built-in pattern.
- Follow the supported CMake presets. Do not add bespoke build scripts or new
  build configurations; the configurations are Debug and Release.
- This single-developer repository normally works on `main`. Do not invent a
  branch/review workflow unless the user requests one.
- Keep new MSVC code warning-clean: the project uses `/W3 /WX`. Linux uses
  `-Wall -Wextra` without global `-Werror`.
- Use file and function names in docs instead of drifting source line numbers.
  Separate implemented behavior from ideas and historical design notes.
- Treat comments and old specs as evidence to check against code. Update the
  relevant current reference when behavior changes; do not append another
  contradictory status paragraph to an obsolete plan.

## Build and checks

Set `VCPKG_ROOT` to a vcpkg checkout. The manifest/toolchain restores dependencies.

```powershell
cmake --preset windows-msvc -DTRANCE_BUILD_TESTS=ON
cmake --build --preset windows-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
.\build\windows-msvc\Release\trance.exe --lint
```

Windows uses the Visual Studio 18 2026 generator and
`x64-windows-static-md`. Linux uses `linux-gcc` / `linux-release`; its Debug
configure preset is `linux-gcc-debug` with build preset `linux-debug`.

Build the full preset before CTest. Building only the player can leave stale
test binaries. `theme_bank_test` requires GL and is labelled `gpu`; use
`-LE gpu` for a runner without a context, and report the exclusion. A successful
skip is not media coverage.

`--lint` parses/compiles built-ins and optional custom patterns, then samples
render expressions at three schedule positions with no-op effects. It is not a
formal proof or complete runtime test. Native V4 tests check parsing, execution,
clocks, text round-trips and recorded rendering; other CTests cover playlists,
JSON and ThemeBank. See [validation coverage](docs/architecture-maturity.md).
The live socket QA harness is `tests/qa_command_channel.py`, outside CTest.

## Visual architecture and invariants

The pipeline is:

```text
V4 text -> JSON AST -> pattern::Node + RenderStmt -> Cycler tree + captures -> render evaluation
```

- `pattern_text_v4.cpp` provides the deliberately small text frontend;
  `pattern_parser_v4.cpp` validates the shared AST. Built-ins in
  `builtin_patterns_v4.cpp` use that native path. Text only exposes features used
  by those eight recipes plus the requested subpattern timing controls.
  V3 source is rejected. Unsupported JSON must never be formatted lossily.
- `pattern_compiler.cpp` maps schedule nodes to runtime cyclers.
  `CompiledVisual` executes ordered effects; `render_eval` reads clocks and
  registers to draw. Keep scheduling and rendering separate.
- Local occurrence ownership and modulation clocks are separate. A curve can
  read an ancestor without changing its schedule. Independent sequences may end
  early while retaining their authored clock span. `loop_subpatterns` is one
  boolean, default false. Explicit `duration: "inherit"` derives a finite sum/max
  from timed subpatterns at every level; retain the sentinel in authored source
  so child edits recalculate it. Omitted duration still uses the enclosing window
  or derives a root's finite sum/max. Do not invent
  unknown totals. There is no Wait policy or sampled-duration feature. Cadence bodies
  restart with cadence, and parent bounds clip descendants. Report inactive and
  clipped spans without equating child inactivity with blank pixels.
- Re-entry does not mean all scalar/image state is erased. State lifetime must
  be specified and tested independently from clock reset.
- Grammar features must lower to concrete schedule, effect and render data.
  If they require runtime behavior, make that change explicit rather than
  hiding it behind parser terminology.
- ThemeBank exposes two live theme sides: `primary` and `secondary`.
  `alternate` is a selector that toggles between them. More simultaneous theme
  sides require a runtime/data-model change and are outside the current design.
- Theme content selection stays within that theme and its explicit inheritance.
  Still/animation requests can fall back to the other kind, then to the side's
  last good frame. Preserve both isolation and fallback behavior.
- Each captured image keeps its image, requested slot, generation and readiness
  together. Failed refresh retains the drawable image and retries. Flash Text
  keeps an opaque base under the phase-shifted overlay; opposing source-over
  fades alone do not guarantee full image coverage.
- Multiple output passes must not advance schedules or accumulating render
  state multiple times. Content ticks, playback elapsed time, animation frame
  delays, and audio timing are distinct.
- Built-ins preserve useful visual intent without requiring old frame-by-frame
  parity. Test the desired observable behavior, not a frozen compiled-tree shape.

Syntax: [spec-grammar-v4.md](docs/spec-grammar-v4.md).
Text: [v4-text-grammar.md](docs/v4-text-grammar.md).
Runtime model: [engine-today.md](docs/engine-today.md).

## Session and UI boundaries

JSON is the on-disk session format. The current in-memory model uses protobuf;
legacy files migrate through the frozen legacy schema. Preserve sidecar state
when loading/saving authored pattern paths and theme scan information.

F2 committed edits autosave to the current session. Session/system JSON writes
use a checked temporary file followed by rename. Export writes another copy.
Underscore-prefixed comment keys are not preserved on save. Runtime overrides
such as command-issued theme/text pins must stay separate from persisted session
settings.

`system.json` is runtime-written and gitignored. Do not treat it as source.
The old creator editor, renderer selection, and video-export pipeline have been
removed; do not reintroduce their assumptions into current documentation.
