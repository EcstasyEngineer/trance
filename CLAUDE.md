# CLAUDE.md

Guidance for working in this repo. Read it before making changes.

## What trance is

A fullscreen visual hypnosis / media player (C++17, SFML 2). It plays **sessions** (protobuf
`.session` files) that drive a stream of timed visuals — flashing images/text, spirals,
animations — over audio with optional binaural/isochronic entrainment beds. There is also a
wxWidgets **creator** (session editor) and a video **export** path.

## Build & run

Dependencies are restored by **vcpkg manifest mode** (`vcpkg.json`) via the toolchain file;
nothing is vendored. Set `VCPKG_ROOT` to your vcpkg checkout first.

```sh
# Configure (per CMakePresets.json)
cmake --preset windows-msvc          # Windows: VS 2022, x64-windows-static-md
cmake --preset linux-gcc             # Linux/WSL: Ninja, Release

# Build (multi-config on Windows; pick Release or Debug)
cmake --build --preset windows-release        # or windows-debug
cmake --build --preset linux-release          # or linux-debug

# Run (realtime windowed; needs a real GPU — software GL won't do)
./build/windows-msvc/Release/trance.exe "C:\path\to\some.session"
# video export instead of a window:
trance.exe --export_path out.webm --export_length 60 some.session
```

There are exactly two build configurations: **Debug** and **Release**. Do not add bespoke
build scripts to the repo root — the CMake presets are the only supported path.

### Tests

Headless, no SFML/protobuf, run via ctest:

```sh
cmake --preset windows-msvc -DTRANCE_BUILD_TESTS=ON
cmake --build --preset windows-release --target v2_grammar_test
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

`tests/v2_grammar_test.cpp` is a **sanity + behavioral** harness for the v2 grammar (parses &
lowers the shipped built-ins; checks targeted behaviors). It is **not** a parity test — see
"supersede, not parity" below.

## Architecture

The visual pipeline (most active area of work):

```
v2 intent grammar  ──parse──▶  pattern::Node AST  ──compile──▶  Cycler tree  ──┐
(pattern_parser_v2)            (pattern_ast.h)     (pattern_compiler)          │
                                                                               ▼
                         render { } block of RenderStmts  ◀──generated──   per-frame eval
                         (render_eval.cpp)                                  (compiled_visual)
```

- **Cycler runtime** (`src/trance/visual/cyclers.{h,cpp}`): a tree of frame-counter nodes
  (Action / OneShot / Parallel / Sequence / Repeat / Offset / Burst) whose leaves fire effects
  (draw ops: Image/Text/Anim/Spiral/Subtext/… + scalar-register ops set/inc/toggle/roll/pulse/
  copy/when). Image registers (`current`, `prev`, named via `-> NAME`) live in a per-visual map.
- **v2 intent grammar** (`pattern_parser_v2.{h,cpp}`, spec `docs/spec-grammar-v2.md`): a
  friendlier authoring front-end that **lowers** to the same `pattern::Node` the engine already
  runs. The built-ins re-authored in it live in `builtin_patterns_v2.cpp`; `director.cpp` prefers
  the v2 source. v1 grammar (`pattern_parser.cpp`) still exists.
- **Director** (`src/trance/director.cpp`): owns the program, picks/compiles visuals, threads the
  entrainment beat period into the grammar, surfaces parse warnings.
- **ThemeBank** (`src/trance/theme_bank.{h,cpp}`): the async image/theme loader. **Bi-thematic
  by design** (see invariants).
- **main loop** (`src/trance/main.cpp`): events → update → render, per frame.

Layout: `src/trance/visual` (grammar, cyclers, render), `src/trance` (director, theme_bank,
render, media, main), `src/common` (proto `trance.proto`, session), `src/creator` (wxWidgets
editor), `docs/`, `tests/`.

## Key invariants & principles

- **Compile-down floor.** Everything the v2 grammar offers must lower to the runtime described
  in `docs/engine-today.md` — a counter tree firing the fixed draw/scalar ops feeding a render
  block. No new runtime magic sneaks in through the grammar. When a construct can't lower, it's
  flagged a REQUIRED RUNTIME EXTENSION, never faked.
- **Bi-thematic engine.** ThemeBank holds exactly **two live themes** (primary + alternate);
  every accessor is a `bool alternate`, not an index. The grammar exposes only `concept`
  (theme 0) and `reward` (theme 1); `theme N≥2` is a hard error. 3+ simultaneous themes is a
  **decided non-goal** (`docs/spec-grammar-v2.md` §7) — not deferred work.
- **Supersede, not parity.** The v2 grammar is meant to *improve on* the original 8 built-ins,
  not be byte-locked to them. Do not add tests that freeze the originals' compiled-tree shape;
  parity-locking is what kept dragging the design back to super_fast's hand-rolled FSM.
- **Modding-language north star (aspirational, with known debt).** The grammar should read like
  a modding language — "oh, so that's how they define this; I can make my own." Today several
  constructs (`crossfade`, `every <curve>` ramps, `anim every Nth`, `chance`) are **baked C++
  compiler macros**, not composable from exposed primitives. That's tracked tech debt to
  refactor; prefer adding general primitives over new `if (kw == "...")` special cases.

## Conventions

- MSVC builds with **`/W3 /WX`** (warnings are errors). Linux is `-Wall -Wextra` without
  `-Werror` (legacy 2014-era code). Keep new code warning-clean on MSVC.
- Commit messages end with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Branch for changes; `master` is the single long-lived branch. Don't leave stray branches.
- `system.cfg` is a runtime-written config (gitignored), not source.
