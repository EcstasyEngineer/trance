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
cmake --build --preset windows-release --target v3_grammar_test
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

`tests/v3_grammar_test.cpp` is a **sanity + behavioral** harness for the v3 grammar (parses &
lowers the shipped built-ins; checks crossfade-from-primitives + register-scope isolation). It
is **not** a parity test — see "supersede, not parity" below.

## Architecture

The visual pipeline (most active area of work):

```
v3 intent grammar  ──parse──▶  pattern::Node AST  ──compile──▶  Cycler tree  ──┐
(pattern_parser_v3)            (pattern_ast.h)     (pattern_compiler)          │
                                                                               ▼
                         render { } block of RenderStmts  ◀──generated──   per-frame eval
                         (render_eval.cpp)                                  (compiled_visual)
```

- **Cycler runtime** (`src/trance/visual/cyclers.{h,cpp}`): a tree of frame-counter nodes
  (Action / OneShot / Parallel / Sequence / Repeat / Offset / Burst) whose leaves fire effects
  (draw ops: Image/Text/Anim/Spiral/Subtext/Warp + scalar-register ops set/inc/toggle/roll/pulse/
  copy/spiral-set/when). Image registers live in a per-visual map, lexically pattern-scoped.
- **v3 intent grammar** (`pattern_parser_v3.{h,cpp}`, spec `docs/spec-grammar-v3.md`): the
  shipped authoring front-end. Two nouns (pattern, effect) + one rule (every numeric is a
  modulator riding the enclosing pattern's clock, redirectable with `over NAME`). Patterns nest;
  crossfade emerges from `copy` + cur/prev + source-over fade-in (no keyword); zoom/fade/spiral
  speed/warp are one curve-drivable class. Built-ins live in `builtin_patterns_v3.cpp`;
  `director.cpp` prefers v3 for built-ins and custom pattern sources, falling back to the v1
  grammar (`pattern_parser.cpp`) for legacy custom patterns.
  (The intermediate v2 grammar and the `super_fast` FSM have been retired.)
- **Director** (`src/trance/director.cpp`): owns the program, picks/compiles visuals, threads the
  entrainment beat period into the grammar, surfaces parse warnings.
- **ThemeBank** (`src/trance/theme_bank.{h,cpp}`): the async image/theme loader. **Bi-thematic
  by design** (see invariants).
- **main loop** (`src/trance/main.cpp`): events → update → render, per frame.

Layout: `src/trance/visual` (grammar, cyclers, render), `src/trance` (director, theme_bank,
render, media, main), `src/common` (proto `trance.proto`, session), `src/creator` (wxWidgets
editor), `docs/`, `tests/`.

## Key invariants & principles

- **Compile-down floor.** Everything the v3 grammar offers must lower to the runtime described
  in `docs/engine-today.md` — a counter tree firing the fixed draw/scalar ops feeding a render
  block. The v3 runtime extensions (curve spiral speed, the `SpiralSet` selector, the wave
  warp, the new render params) are explicit and named in `docs/spec-grammar-v3.md` §9/§0; nothing
  else sneaks new runtime magic in through the grammar. A construct that can't lower is flagged a
  REQUIRED RUNTIME EXTENSION, never faked.
- **Bi-thematic engine.** ThemeBank holds exactly **two live themes** (primary + alternate);
  every accessor is a `bool alternate`, not an index. The grammar exposes only `concept`
  (theme 0) and `reward` (theme 1). 3+ simultaneous themes is a **decided non-goal**
  (`docs/spec-grammar-v3.md` §8 / `spec-grammar-v2.md` §7) — not deferred work.
- **Supersede, not parity.** The v3 grammar *improves on* the original 8 built-ins rather than
  matching them byte-for-byte ("same effect, not the same frames"). Do not add tests that freeze
  the originals' compiled-tree shape; parity-locking is what kept dragging the design back to
  super_fast's hand-rolled FSM (now deleted).
- **Modding-language north star.** The grammar reads like a modding language — "oh, so that's how
  they define this; I can make my own." This was the whole point of v3: crossfade, spiral, zoom,
  fade, and warp are composed from exposed primitives, not baked `if (kw == "...")` C++ macros.
  Keep it that way — add general primitives, not special cases. (The one deferred piece is a
  text-content register so text can crossfade like images; `docs/spec-grammar-v3.md` Ext#4.)

## Conventions

- MSVC builds with **`/W3 /WX`** (warnings are errors). Linux is `-Wall -Wextra` without
  `-Werror` (legacy 2014-era code). Keep new code warning-clean on MSVC.
- Commit messages end with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Branch for changes; `master` is the single long-lived branch. Don't leave stray branches.
- `system.cfg` is a runtime-written config (gitignored), not source.
