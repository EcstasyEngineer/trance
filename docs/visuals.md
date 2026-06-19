# Visual system (as-built reference)

The visual engine turns a **pattern** — a timing schedule with effects — into the
flashing images, text, spirals, and animations on screen. As of the Framing-B
refactor, every visual is a **data-driven compiled pattern**: there are no
hand-coded visual classes left in the playback path. This document is the
reference for how that pipeline works. The earlier design narrative (the
"Framing A vs Framing B" debate and the plan to delete the hardcoded visuals,
which is now done) lives in [visual-grammar.md](visual-grammar.md) as history.

To *write* a custom pattern, see
[authoring-visual-patterns.md](authoring-visual-patterns.md).

## The pipeline

```
pattern DSL text                              pattern_parser.{h,cpp}
  (builtin_patterns.cpp for the 8 built-ins,    │  → line:col diagnostics
   or .session VisualPatternSource for custom)  ▼
                                              pattern::Node tree   (pattern_ast.h)
                                                │  pattern_compiler.{h,cpp}
                                                ▼
                                              Cycler tree          (cyclers.{h,cpp})
                                                │  CompiledVisual   (compiled_visual.{h,cpp})
                                                ▼
                                              VisualControl effects + a named render preset
                                                                   (render_preset.{h,cpp})
```

1. **Parse** (`pattern_parser.{h,cpp}`). DSL source text → a normalized
   `pattern::Node` IR, with `line:col` diagnostics on failure. The authoritative
   grammar is the header comment in `pattern_parser.h`.
2. **AST** (`pattern_ast.h`). `pattern::Node` carries the schedule (node type,
   length, action frame), an ordered `Effect` list per leaf, and the debug
   annotations (`phase` label, `image_slot` hint) the F1 overlay reads.
3. **Compile** (`pattern_compiler.{h,cpp}`). Lowers the AST to a `Cycler` tree,
   reusing the existing cycler classes. Action leaves are bound to behaviour
   through an api-free `make_action` seam (so the compiler can be unit-tested
   without SFML or protobuf).
4. **CompiledVisual** (`compiled_visual.{h,cpp}`). Each leaf's effect list becomes
   a `std::function` (`run_effect`) that calls `VisualControl` and writes a
   register block (`pattern::Registers`). The visual draws via a named render
   preset.

## Cyclers — the schedule primitives

A `Cycler` (`cyclers.h`) is a bounded frame counter; the tree of cyclers *is* the
schedule. Advancing the tree one frame at a time executes the pattern. All node
positions are exact integers; there is no stack at runtime.

| Cycler | Role | Length |
|---|---|---|
| `ActionCycler` | Leaf. Fires its effect every N frames (or on frame K of every N, or every frame). | N |
| `OneShotCycler` | Children together, **once**. The longest child keeps it active. | max(children) |
| `ParallelCycler` | Children together, **repeating**. | LCM(children) |
| `SequenceCycler` | Children in order. | sum(children) |
| `RepeatCycler` | One child repeated N times. | N · len(child) |
| `OffsetCycler` | One child, phase-shifted by K frames (pre-advances at construction). | len(child) |
| `BurstCycler` | A base loop randomly interrupted by a bounded burst, then a cooldown. The narrow, purpose-named replacement for a general state machine. | fixed total |

Key accessors used by the overlay and render presets (`cyclers.h`):

- `length()`, `position()`, `frame()` (= `position()-1 mod length()`), `progress()`.
- `active()` — set on the live path; the overlay reads this for "current section".
- `index()` — **virtual**; the Rep/Seq position (and `1` during a `BurstCycler`
  burst). A render preset reads loop/segment position through this. `0` for
  leaf/parallel nodes.
- `phase()` / `image_slot()` / `image_label()` — pure annotations for the overlay
  (see below). They never affect scheduling.

## Effects — the leaf vocabulary

The compiler turns each leaf's ordered `Effect` list into one action lambda
(`run_effect`, `compiled_visual.cpp`). An effect either drives a `VisualControl`
draw call or mutates the pattern's scalar register block. The full enum is
`pattern::Effect::Kind` (`pattern_ast.h`).

**Draw effects** (call `VisualControl` / write an image register):
`image`, `text`, `anim`, `subtext`, `small_text`, `themes`, `font`, `spiral_new`,
`spiral` (rotate), `upload`. Image effects (and `image reg NAME`, `anim reg NAME`)
write a named image register; the render preset reads it.

**Scalar-state ops** — the *only* mutable state the language has. There are no
general variables; these exist solely to express the handful of stateful built-in
visuals (toggles, counters, captured randoms) as data:

- `set NAME N` — `scalars[NAME] = N`.
- `inc NAME [by N]` — `scalars[NAME] += N`.
- `toggle NAME` — `scalars[NAME] ^= 1`.
- `roll NAME : a b c` — `scalars[NAME] = choices[random(n)]` (captured random,
  e.g. the `{2,4,8}` animation modulus).
- `pulse COUNTER every MOD -> FLAG` — a bounded counter that raises a one-frame
  flag every MOD-th fire (the "every-Nth animation" trick made visible). MOD may
  be a register.
- `copy SRC -> DST` — image-register hand-off (FLASH_TEXT's `_start = _end`).
- slot-from-register (`image reg NAME`, `anim reg NAME`, …) — a toggle used as a
  primary/alternate selector.

**Guard.** `when REG [== N | >= N]` runs a single effect only when the register
condition holds. This is the language's *only* conditional.

**Native FSM.** `super_fast_tick` is SUPER_FAST's 4-state FSM, deliberately
isolated as one native effect (`compiled_visual.cpp`) rather than leaking a
state-machine language into the DSL.

**Other leaf modifiers.** `divide N` runs a leaf's effects only every Nth time it
fires; `generate VAR from A to B { … }` is a compile-time bounded expansion; an
`[expr]` arithmetic evaluator (`+ - * / ^` and the active `generate` var) may
appear anywhere an int is expected.

## Registers and render presets

`pattern::Registers` (`render_preset.h`) is the visual's mutable state:

```cpp
struct Registers {
  std::unordered_map<std::string, Image>   images;   // named image slots
  std::unordered_map<std::string, int32_t> scalars;  // named bool/int registers
};
```

Effects write the registers; a **render preset** (`render_preset.{h,cpp}`) reads
them and the live cycler state to emit draw calls. A preset is the analogue of a
hardcoded visual's render lambda, but it addresses cycler nodes by their DSL `id`
through a `NodeMap` instead of captured C++ locals. `render_preset(name)` returns
the named preset, or a single-image default if the name is unknown — so an
authored pattern always renders something rather than failing playback.

## The eight built-in patterns

`Program::VisualType` still has its original eight values (`trance.proto:128`);
the enum is unchanged, so old `.session` files keep working. Each value now maps
to a DSL source string in `builtin_patterns.cpp`:

| Enum (value) | Pattern source | Render preset | Note |
|---|---|---|---|
| `ACCELERATE` (1) | `kAccelerate` | `accelerate` | Ramp of image segments, length 56→12; built with `generate` + `[expr]`. |
| `SLOW_FLASH` (2) | `kSlowFlash` | `slow_flash` | Slow then fast flash phases (the canonical example). |
| `SUB_TEXT` (3) | `kSubText` | `sub_text` | Image + scrolling subtext; `sub_speed`-gated cadence. |
| `FLASH_TEXT` (4) | `kFlashText` | `flash_text` | 2-image crossfade + text. |
| `PARALLEL` (5) | `kSimple` | `simple` | Single image (the enum name and behaviour don't match — this is one image). |
| `SUPER_PARALLEL` (6) | `kSuperParallel` | `super_parallel` | 3-image staggered interleave. |
| `ANIMATION` (7) | `kAnimation` | `animation` | Animation + crossfade image with a fade window. |
| `SUPER_FAST` (8) | `kSuperFast` | `super_fast` | Rapid current/next cuts, driven by the isolated `super_fast_tick` FSM. |

`Director::build_builtin_patterns()` (`director.cpp:84`) parses all eight at
startup and throws on a parse failure (built-in sources are compile-time
constants with no fallback, so a typo is a build bug — `builtin_patterns_test`
guards this).

### The one remaining hardcoded class

`AnimationVisual` (`visual.cpp`) is the **only** surviving `*Visual` class. It is
not played by the engine — it is the live reference that `pattern_render_test`
advances next to the compiled `animation` built-in to prove render equivalence.
The engine itself plays the compiled `animation` pattern. The other seven
formerly-hardcoded classes have been deleted.

## Selection

`Director::change_visual()` (`director.cpp:384`) builds one weighted shuffle over
**both** the program's enabled `visual_type` entries and its enabled
`custom_visual_pattern` entries, then picks one. The current visual "sticks" with
a length- and speed-scaled probability (roughly 1/2 for a 2048-frame cycle) so
patterns don't change every cycle. Custom patterns are parsed once per program
change in `rebuild_custom_patterns()` (`director.cpp:104`); a custom pattern that
fails to parse is skipped with a warning, never the whole session.

## The F1 debug overlay

Because a compiled pattern's cycler tree carries the same `phase` / `image_slot`
annotations a hardcoded visual would, it drives the F1 overlay for free
(`Director::draw_debug_overlay`, `director.cpp:743`). The overlay reports the
deepest active labelled cycler as the current "section", which theme slots the
active image lanes source, the composited image layers, the spiral, the
entrainment bed, and a minimised live view of the cycler tree. Visuals with no
labelled section (e.g. SUPER_FAST, whose phases live in its FSM rather than the
tree) honestly show `section --`.

## Tests

Six headless ctests (gated behind `-DTRANCE_BUILD_TESTS=ON`) cover the pipeline —
parse, compile, effect routing, the authoring primitives, the built-in sources,
and frame-for-frame render equivalence. See [../CONTRIBUTING.md](../CONTRIBUTING.md).
