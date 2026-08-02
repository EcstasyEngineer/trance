# CLAUDE.md

Guidance for working in this repo. Read it before making changes.

## What trance is

A fullscreen visual hypnosis / media player (C++17, SFML 3). It plays **sessions**
(`*.session.json`, spec in `docs/session-json-format.md`; legacy protobuf `.session` files
convert via `trance_convert`) that drive a stream of timed visuals — flashing images/text,
spirals, animations — over audio with optional binaural/isochronic entrainment beds and
theme audio. Also: a click-through `--overlay` mode (X11 and Win32), an
ImGui F2 in-app UI, a system tray icon (Windows) + global Shift+F11 hide-everything hotkey, and a
`--command_port` line-protocol control channel. (The legacy wxWidgets **creator** editor has
been deleted; the F2 panel plus hand-edited JSON is the editing story.)

## Build & run

Dependencies are restored by **vcpkg manifest mode** (`vcpkg.json`) via the toolchain file;
nothing is vendored. Set `VCPKG_ROOT` to your vcpkg checkout first.

```sh
# Configure (per CMakePresets.json)
cmake --preset windows-msvc          # Windows: VS 2026, x64-windows-static-md
cmake --preset linux-gcc             # Linux/WSL: Ninja, Release

# Build (multi-config on Windows; pick Release or Debug)
cmake --build --preset windows-release        # or windows-debug
cmake --build --preset linux-release          # or linux-debug

# Run (realtime windowed; needs a real GPU — software GL won't do)
./build/windows-msvc/Release/trance.exe "C:\path\to\some.session"

# Bundle a clean distributable (trance.exe + trance_convert.exe + openvr_api.dll, nothing else)
cmake --install build/windows-msvc --config Release --prefix dist
```

There are exactly two build configurations: **Debug** and **Release**. Do not add bespoke
build scripts to the repo root — the CMake presets are the only supported path.

### Tests

Headless, no SFML/protobuf, run via ctest:

```sh
cmake --preset windows-msvc -DTRANCE_BUILD_TESTS=ON
cmake --build --preset windows-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

Five tests: `v3_grammar_test`, `session_json_test`, `playlist_runner_test`,
`command_protocol_test`, `theme_bank_test`. CI runs ctest on every pull request. Test
targets live in `tests/CMakeLists.txt`; their binaries and intermediates land under
`build/<preset>/tests/`, keeping `Release/` to the shippable binaries only.

`theme_bank_test` is the one that is **not** headless: it drives ThemeBank's tiered image
selection against a real synthetic media tree, and `get_image` uploads textures, so it needs
a current OpenGL context (`sf::Context` — no window). It is labelled `gpu`, so
`ctest -LE gpu` skips it; run without a GPU it prints a loud SKIP banner and exits 0 rather
than reddening CI.

**Build the test targets, not just `trance`.** Building only the `trance` target leaves
stale test binaries in place and `ctest` then reports false passes — that is exactly how
`session_json_test` sat red on master unnoticed.

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
  ONLY grammar -- v1 (`pattern_parser.{h,cpp}`) and the intermediate v2 have both been retired
  and deleted; there is no fallback parser. Two nouns (pattern, effect) + one rule (every
  numeric is a modulator riding the enclosing pattern's clock, redirectable with `over NAME`).
  Patterns nest; crossfade emerges from `copy` + cur/prev + source-over fade-in (no keyword);
  zoom/fade/spiral speed/warp are one curve-drivable class. Built-ins live in
  `builtin_patterns_v3.cpp`. `director.cpp` parses every built-in and custom pattern source
  with v3; a custom pattern that fails to parse is skipped with a surfaced warning (not a
  crash, not a silent black screen) -- the rest of the program's visuals still play.
  (The `super_fast` FSM has also been retired.)
- **Director** (`src/trance/director.cpp`): owns the program, picks/compiles visuals, threads the
  entrainment beat period into the grammar, surfaces parse warnings.
- **ThemeBank** (`src/trance/theme_bank.{h,cpp}`): the async image/theme loader. **Bi-thematic
  by design** (see invariants).
- **main loop** (`src/trance/main.cpp`): events → update → render, per frame.

Layout: `src/trance/visual` (grammar, cyclers, render), `src/trance` (director, theme_bank,
render, media, main), `src/common` (proto `trance.proto`, session), `docs/`, `tests/`.

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
  (`docs/spec-grammar-v3.md` §9, "hard non-goals") — not deferred work.
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
- Branch for changes; `master` is the single long-lived branch. Don't leave stray branches.
- `system.json` is a runtime-written config (gitignored), not source. (`system.cfg` is its
  retired protobuf ancestor — convert with `trance_convert`.)
