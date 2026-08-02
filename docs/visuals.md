# Visual system (as-built reference)

The visual engine turns a **pattern** — a timing schedule with effects — into the
flashing images, text, spirals, and animations on screen. Every visual is a
**data-driven compiled pattern**: there are no hand-coded visual classes left in
the playback path. This document is the reference for how that pipeline works.

To *write* a custom v3 pattern, see
[authoring-v3-patterns.md](authoring-v3-patterns.md). The normative grammar spec
is [spec-grammar-v3.md](spec-grammar-v3.md).

## The pipeline

```text
v3 pattern DSL text
  builtin_patterns_v3.cpp for shipped visuals
  the session's custom_visual_pattern sources for custom patterns
        |
        v  pattern_parser_v3.{h,cpp}  (the ONLY parser -- v1 and v2 are retired and deleted)
pattern::Node tree + generated RenderStmt block
        |
        v  pattern_compiler.{h,cpp}
Cycler tree
        |
        v  CompiledVisual + render_eval.{h,cpp}
VisualControl effects + data-driven rendering
```

1. **Parse** (`pattern_parser_v3.{h,cpp}`). DSL source text becomes a normalized
   `pattern::Node` IR plus a **generated** render block, with `line:col`
   diagnostics on failure. There is no fallback parser — v1
   (`pattern_parser.{h,cpp}`) is deleted. The authoritative grammar is
   `docs/spec-grammar-v3.md` plus the parser source.
2. **AST** (`pattern_ast.h`). `pattern::Node` carries the schedule (node type,
   length, action frame), an ordered `Effect` list per leaf, and the debug
   annotations (`phase` label, `image_slot` hint) the F1 overlay reads.
3. **Compile** (`pattern_compiler.{h,cpp}`). Lowers the AST to a `Cycler` tree,
   reusing the existing cycler classes. Action leaves are bound to behaviour
   through an api-free `make_action` seam (so the compiler can be unit-tested
   without SFML or protobuf).
4. **CompiledVisual** (`compiled_visual.{h,cpp}`). Each leaf's effect list becomes
   a `std::function` (`run_effect`) that calls `VisualControl` and writes a
   register block (`pattern::Registers`). The visual draws via its data-driven
   `render { }` block (or `pattern::default_render_block()` if the pattern
   generated none).

## Cyclers — the schedule primitives

A `Cycler` (`cyclers.h`) is a bounded frame counter; the tree of cyclers *is* the
schedule. Advancing the tree one frame at a time executes the pattern. All node
positions are exact integers; there is no stack at runtime.

| Cycler | Role | Length |
|---|---|---|
| `ActionCycler` | Leaf. Fires its effect on frame 0 of every N frames (N=1 = every frame). | N |
| `OneShotCycler` | Children together, **once**. The longest child keeps it active. | max(children) |
| `ParallelCycler` | Children together, **repeating**. | LCM(children) |
| `SequenceCycler` | Children in order. | sum(children) |
| `RepeatCycler` | One child repeated N times. | N · len(child) |
| `OffsetCycler` | One child, phase-shifted by K frames (pre-advances at construction). | len(child) |
| `BurstCycler` | A base loop randomly interrupted by a bounded burst, then a cooldown. The narrow, purpose-named replacement for a general state machine; lowered from the grammar's `burst` cadence. | fixed total |

Key accessors used by the overlay and the render evaluator (`cyclers.h`):

- `length()`, `position()`, `frame()` (= `position()-1 mod length()`), `progress()`.
- `active()` — set on the live path; the overlay reads this for "current section".
- `index()` — **virtual**; the Rep/Seq position (and `1` during a `BurstCycler`
  burst). A render block reads loop/segment position through this (the `index`
  attribute, e.g. `slow_repeat.index`). `0` for leaf/parallel nodes.
- `phase()` / `image_slot()` / `image_label()` — pure annotations for the overlay
  (see below). They never affect scheduling.

## Effects — the leaf vocabulary

The compiler turns each leaf's ordered `Effect` list into one action lambda
(`run_effect`, `compiled_visual.cpp`). An effect either drives a `VisualControl`
draw call or mutates the pattern's scalar register block. The full enum is
`pattern::Effect::Kind` (`pattern_ast.h`).

**This is the IR vocabulary, not the authoring surface.** The v3 grammar's
statements (`image`, `word`, `caption`, `draw`, `spiral`, `warp`/`drunk`, `copy`,
`audio`, the `every`/`loop`/`ramp`/`burst` cadences — see
[spec-grammar-v3.md](spec-grammar-v3.md) §4) *lower to* these ops; most scalar
ops are emitted by the parser rather than typed by an author (`set`/`inc`/`roll`
as user-facing statements never shipped — spec §4.7).

**Draw effects** (call `VisualControl` / write an image register):
`Image`, `Text`, `Anim`, `Subtext`, `SmallSub`, `Themes`, `Font`, `SpiralNew`,
`SpiralSet` (deterministic spiral type + width). Spiral ROTATION is not an effect:
it is a per-frame render param (`RenderStmt{Op::Spiral, speed=...}`).
Image effects write a named image register; the render block reads it.

**Theme audio** (issue #23): `Audio` starts a precanned track from the theme's
audio pool on the engine's single dedicated theme-audio channel (slot semantics
identical to `Image`; optional loop flag); `AudioStop` stops it.

**Scalar-state ops** — the *only* mutable state the language has. There are no
general variables; these exist to express stateful visuals (toggles, counters,
captured randoms) as data:

- `Set` — `scalars[NAME] = N`.
- `Toggle` — `scalars[NAME] ^= 1`.
- `Roll` — `scalars[NAME] = choices[random(n)]` (captured random; this is what
  the grammar's `chance` lowers to).
- `Pulse` — a bounded counter that raises a one-frame flag every MOD-th fire
  (the "every-Nth animation" trick made visible). MOD may be a register.
- `Copy` — image-register hand-off (`copy cur -> prev`; the crossfade handoff,
  and the one state effect that is author-visible in the v3 surface).
- slot-from-register (`slot_reg` on an image/anim effect) — a toggle/flag used
  as a primary/alternate selector.

**Guard.** An effect can carry a `when` guard (`Guard::Truthy | Eq | Ge`): run
only when the register condition holds. This is the language's *only*
conditional.

**Other leaf attributes.** `divide N` (`pattern::Node::divide`) runs a leaf's
effects only every Nth time it fires — bounded state owned by the compiled
action, not a user variable.

## Registers and the render block

`pattern::Registers` (`pattern_runtime.h`) is the visual's mutable state:

```cpp
struct Registers {
  std::unordered_map<std::string, Image>   images;      // named image slots
  std::unordered_map<std::string, Slot>    image_slots; // concrete source theme per image reg
  std::unordered_map<std::string, int32_t> scalars;     // named bool/int registers
};
```

Effects write the registers; the pattern's **render block** describes *what is
drawn* as data, and `eval_render` (`render_eval.{h,cpp}`) reads the registers plus
the live cycler state each frame to emit draw calls. The render block is a
`std::vector<pattern::RenderStmt>` (`pattern_ast.h`) — the data-driven replacement
for the old per-pattern C++ render presets (`render_preset.{h,cpp}`, since
deleted). In v3 the render block is **generated by the parser** from the
pattern's draw statements — it is not authored directly, and the legacy authored
`render { }` / `render NAME` surfaces are retired.

Each `RenderStmt` is one draw op (`Image`, `Text`, `Subtext`, `SmallText`,
`Spiral`, `Warp`, `AudioVolume`) with an optional `when [cond]` and `[expr]`
params (`alpha`, `origin`, `zoom`, `shadow_origin`, `shadow_zoom`, and `speed` —
the per-frame spiral-rotation / theme-audio-volume axis). The `[expr]`s are
evaluated per frame against register values and live cycler state, which the
block addresses by node `id` through a `NodeMap` (`pattern_compiler.h`) — e.g.
`slow_loop.active`, `slow_main.progress`, `slow_repeat.index`.

A pattern that generates no render block falls back to
`pattern::default_render_block()` (current image + spiral + text), so playback
never shows a blank frame.

## The eight built-in patterns

`Program::VisualType` still has its original eight values
(`src/common/trance.proto`); the enum is unchanged, so existing sessions keep
working. Each value now maps to a v3 DSL source string in
`builtin_patterns_v3.cpp`, each generating its own render block:

| Enum (value) | Pattern source | Note |
|---|---|---|
| `ACCELERATE` (1) | `kAccelerate` | Accelerating image cadence via the sampled `every ramp` construct. |
| `SLOW_FLASH` (2) | `kSlowFlash` | Slow then fast flash phases (the canonical example). |
| `SUB_TEXT` (3) | `kSubText` | Image + scrolling subtext. |
| `FLASH_TEXT` (4) | `kFlashText` | 2-image crossfade + text. |
| `PARALLEL` (5) | `kSimple` | Single image (the enum name and behaviour don't match — this is one image). |
| `SUPER_PARALLEL` (6) | `kSuperParallel` | 3-image staggered interleave. |
| `ANIMATION` (7) | `kAnimation` | Animation + crossfade image with a fade window. |
| `SUPER_FAST` (8) | `kSuperFast` | Rapid runtime cuts with a `burst` interrupt plus chance-based word/animation accents. |

`Director::build_builtin_patterns()` (`director.cpp`) parses all eight at
startup and throws on a parse failure: built-in sources are compile-time
constants, so a typo is a build bug — fail fast rather than risk a null visual
at selection time. `trance --lint` proves the same property headlessly (parses,
lowers, compiles and expr-evaluates all eight) without opening a window.

All eight formerly-hardcoded `*Visual` classes are deleted. `Visual`
(`src/trance/visual/visual.h`) survives only as the small base interface
(cycler + render function) that `CompiledVisual` implements.

## Selection

`Director::change_visual()` (`director.cpp`) builds one weighted shuffle over
**both** the program's enabled `visual_type` entries and its enabled
`custom_visual_pattern` entries, then picks one. The current visual "sticks" with
a length- and speed-scaled probability (roughly 1/2 for a 2048-frame cycle) so
patterns don't change every cycle. (The `--visual` / `--pattern` command-line
flags bypass the shuffle entirely and force every selection.) Custom patterns are
parsed once per program change in `Director::rebuild_custom_patterns()` — v3 is
the only parser; a custom pattern that fails to parse is skipped with a surfaced
warning, never the whole session.

## The F1 debug overlay

The cycler tree carries `phase` / `image_slot` annotations for the live-section and tree
view (`Director::draw_debug_overlay`, `director.cpp`). Theme stars come from render-time
layer metadata instead: `Image` effects store the concrete source slot next to each image
register, `Copy` preserves it, and each rendered image layer records alpha + slot. The F1
theme block shows all four ThemeBank queue slots vertically (`unloaded`, `primary`,
`secondary`, `loading`) and marks `*` only when a visible image layer from that slot was drawn
this frame. Visuals with no labelled section honestly show `section --`.

## Tests

Ctest entries (gated behind `-DTRANCE_BUILD_TESTS=ON`, run via `ctest`; CI runs
them on every pull request):

- `grammar_lint` — `trance --lint`: the product binary parses, lowers, compiles
  and expr-evaluates all eight built-ins. Deliberately **not** a parity test
  (see CLAUDE.md, "supersede, not parity").
- `session_json_test` — the `*.session.json` loader round-trip + the frozen
  legacy-import contract (#47).
- `playlist_runner_test` — the playlist stack machine (`playlist_runner.{h,cpp}`).

The `--command_port` channel is QA'd end-to-end from Python against a live exe
(`tests/qa_command_channel.py`, #29).
