# Trance Visual Grammar v3 — Design Specification

> Status: **SHIPPED** (Phases 1–4 implemented; `pattern_parser_v3.{h,cpp}` +
> `builtin_patterns_v3.cpp`, the director's preferred built-in path). This document defines the
> v3 surface and its exact lowering to the cycler + effect + render-block runtime. The runtime
> extensions it needed are built: curve-driven spiral speed, the `SpiralSet` selector, and the
> wave warp shader. The **one deferred** piece is a text-content register so text can crossfade
> like images (Ext#4) — the text path is a single live slot, not a register. The v2 grammar and
> the `super_fast` FSM have been retired. Where this spec and any older prose disagree, **the
> parser and runtime enums are the source of truth** (`pattern_ast.h`, `compiled_visual.cpp`,
> `render_eval.cpp`, `api.cpp`). §0 (locked decisions) governs the rest.

---

## 0. Decisions locked (supersede conflicting draft text below)

These were decided with the user after the workflow, and are grounded in the actual render
code (`shaders.h`, `api.cpp`, `render_eval.cpp`). Where §1–§12 conflict, **this section wins.**

**0.1 Multi-parameter effects — already handled, no new concept.** An effect is
`verb (param modulator)*` — a verb followed by any number of `param modulator` pairs, each
independently curve-drivable by the one rule. This is exactly how `image` already takes
`zoom M fade in origin M`. A bare leading modulator is the effect's "main" knob; the rest
default. So a multi-param effect like the warp below needs no new grammar machinery.

**0.2 The `drunk` effect is a WAVE WARP (shader), not an origin/zoom wobble.** Supersedes
§4.5. The intent is an "under-water" sinusoidal displacement of the image, not a positional
jitter (a plain jitter is just `origin`/`zoom` with a random modulator — not worth a new
primitive). Grammar:
```
warp amplitude (curve 0 -> 0.3)  wavelength 0.2  speed (curve 1 -> 3)
```
**Feasibility (grounded):** the image fragment shader (`shaders.h:55-67`) is
`out_colour * texture2D(texture, out_texture_coord)`. The warp adds `warp_amp / warp_wavelength
/ warp_speed / warp_time` uniforms and displaces the sampled coord, e.g.
`coord += warp_amp * sin(coord.yx / warp_wavelength + warp_time * warp_speed)`. **Required
runtime extension (bounded):** the shader lines + a `RenderStmt` warp-param trio (live `[expr]`
like zoom) + uniform plumbing in `api.cpp`/`render.cpp` + a per-frame `warp_time`. This
REPLACES §4.5's `Effect::Kind::Walk` register-walk design (and moots its fps-normalization /
dead-zone gaps in A.1/§12, since time comes from the frame clock, though `warp_speed` still
scales per-second). `drunk <intensity>` becomes sugar for `warp amplitude <intensity>` with a
sensible default wavelength/speed. If this proves too costly it can be deferred without
changing any other primitive.

**0.3 Text cannot crossfade yet — Extension #4 remains deferred.** The current text path has
one live text slot (`VisualApiImpl::_current_text`) and `render_text` has no alpha/content
register parameter, so `copy`/`draw` crossfade is image-only. A future Extension #4 would add a
text-content register plus an alpha param on `render_text`; until then `flash_text` is authored
as an image crossfade with word/caption accents.

**0.4 Spiral: `look{}` grammar selector; SPEED is a curve; COLOR/DIRECTION stay settings.**
The spiral fragment shader (`shaders.h:82-99`) has **7 types** (`spiral_type` 1–7, correct the
draft's "5"), `width` = arm count, `acolour`/`bcolour`, and a `time` uniform that spins it.
- `look { spiral type=N width=W }` → a deterministic `SpiralSet` effect (Extension #3) pinning
  type/width (replacing `change_spiral`'s random roll). Hard-errors until Ext#3 lands (no
  silent fallback — corrects the A.1 note).
- `spiral speed <curve>` → drives the `time` uniform's per-frame rate (Extension #1).
- `acolour`/`bcolour`/direction remain Program-proto settings, walled off from the grammar.

**0.5 `super_fast`: commit to randomness primitives; delete `SuperFastTick`.** No `raw {}`
escape hatch. Accept "same effect, not the same frames." Closes that §12 open question.

**0.6 Register scoping — lexical, pattern-scoped, compile-time qualified (resolves the A.1
collision).** A register name is **local to its nearest enclosing `pattern`**; cadence blocks
(`every`/`loop`) do **not** open a new register scope (so a `copy cur -> prev` inside an
`every` and the `draw cur` / `draw prev` statements in the same pattern hit the same registers — which
is exactly what crossfade needs).
- **Qualification.** The compiler prefixes each bare register name with its pattern's id-path
  before emitting to the flat runtime maps (`regs.images` / `regs.scalars`). Two sibling
  crossfade sub-patterns each writing `cur`/`prev` become `A.cur`/`A.prev` and `B.cur`/`B.prev`
  — **collision impossible**, at any nesting depth. A loop's iterations reuse the same pattern
  scope, so `cur`/`prev` correctly **persist** across beats (the handoff still works).
- **Resolution (lexical).** A bare reference binds to the nearest enclosing pattern that uses
  that name. To deliberately read another pattern's register (descriptive power), write a
  **qualified `OtherPattern.reg`** — the same `pattern.name` form the compiler uses internally.
- **AST cost (minimal — the explicit design constraint).** No new AST node types;
  `Effect.target`, `Effect.src`, and `RenderStmt.image_reg` stay `std::string`. The parser
  already walks the pattern nesting; it adds a scope stack and qualifies register names at parse
  time. **Runtime is unchanged** — still a flat string→value map, the keys are just qualified.
  Pure compile-time string transformation.
- **Safety.** The §7.4 resolution check rejects a bare name that resolves to nothing and a
  qualified name whose target pattern/register does not exist — so an unresolved register is a
  **parse error**, never the silent-zero dark-screen footgun.
- **Rejected:** explicit `reg NAME` declarations (ceremony hurts common-case legibility);
  collision-only suffixing `cur#2` (unpredictable for cross-references); first-class register
  handles (a new binding concept = AST over-engineering). Lexical auto-qualification is the best
  compromise of legibility (bare local names), power (qualified cross-refs), and minimal AST.

**Still open after these decisions:** future text-register support and any further tuning of
the `warp` parameters.

---

## 1. Design principles

1. **Two nouns, one rule.** The entire language is:
   - **PATTERN** — a named span that owns a length and therefore a normalized `0..1`
     clock (`.progress`). A pattern's body is a list of effects and nested patterns.
   - **EFFECT** — a verb placed inside a pattern: a *draw* (`image`/`word`/`spiral`),
     a *driver* (`zoom`/`fade`/`spiral speed`/`drunk`), or a *state* op (`copy`/`set`/`roll`).
   - **THE RULE** — every numeric an effect takes is a **MODULATOR**: a literal, a
     `curve`, a `drunk` wander, a `beat`, or a raw `[expr]`. Every modulator implicitly
     reads `this.progress` — the clock of the enclosing pattern — unless redirected with
     `over NAME` to an ancestor pattern's clock.

2. **The grammar conveys intent.** You read a pattern top-down; every line is
   `<effect> <param> <modulator>`. The clock a curve rides is "the pattern I am written
   inside," which the indentation already shows. A simple pattern is narratable without a
   manual (`image concept zoom (curve 0 -> 0.5)` = "draw concept, zoom from 0 to 0.5 over
   its life").

3. **Everything compiles down (HARD INVARIANT).** Every construct reduces to today's
   runtime: a `pattern::Node` tree of `{Action, Seq, Par, One, Rep, Off, Burst}` cyclers
   whose leaves fire `pattern::Effect`s, plus a flat `std::vector<RenderStmt>` whose `[expr]`
   params are evaluated each frame by `render_eval.cpp`. Anything that genuinely cannot
   lower is named as a required runtime extension (§9), never smuggled.

4. **Name what cannot lower.** Two things require a runtime extension: a *continuous*
   curve-driven spiral speed, and the new `drunk` wander. Both degrade gracefully to an
   existing lowering until the extension ships, and the grammar surface is identical either
   way.

5. **Grammar owns time-varying scalars; settings own static identity/palette.** A value a
   curve can move per frame belongs in the grammar; a look chosen once and constant for the
   run belongs in settings. This single rule resolves the spiral tension (§8).

6. **No silent-zero footguns.** `render_eval` resolves unknown identifiers to `0.0` with no
   diagnostic. v3 mandates a compile-time check (§7.4): every `over NAME` and every wired
   modulator name must resolve to a declared pattern id / register, or it is a parse error.

---

## 2. Why this spine (synthesis rationale)

The adversarial scoring put **signalflow** alone at compile-down 8 on two independent
lenses with **no fatal flaws**, because it (a) names its runtime extensions instead of
hiding them and (b) solves the crossfade double-zoom trap with a *visible* wide anchor
rather than the hidden `+0.5` offset. **mod** scored equally on goal-coverage and supplied
the most teachable framing ("two nouns + one rule", `over NAME` generalizing the fixed
clock trio), but a critic verified two real bugs in its examples: its crossfade
two-beat-zoom "fix" was mis-diagnosed, and its `seq`-default body cannot co-run a per-frame
driver leaf with persistent draws. **timeline-layers** and **streamfx** contributed the
cleanest grammar-vs-settings articulation and the `echo`/decorator ergonomics, but
streamfx's crossfade example anchored alpha to the wrong (whole-stream) clock.

v3 therefore = **signalflow's modulator-as-wire spine + visible-wide-anchor crossfade**,
re-skinned with **mod's "two nouns one rule" legibility and `over NAME` anchoring**, with
the three verified bugs fixed:

- Crossfade rides the **per-beat leaf clock**, never the whole-pattern Repeat (§6 EX4).
- A pattern body is **parallel by default** so drivers and draws co-run (§3, §4.1).
- `over NAME` resolution and modulator names are **compile-time checked** (§7.4).

---

## 3. The runtime, in one paragraph (the lowering target)

The Cycler tree nests arbitrarily by owning pointers; every node exposes a uniform
interface and a `progress()` = `frame()/length()` clock. `compile_impl` recurses
`Node.children` 1:1 onto `SequenceCycler` / `ParallelCycler` / `OneShotCycler` /
`RepeatCycler` / `OffsetCycler`. Every author-named node gets an `id` minted into a flat
`NodeMap`; `render_eval`'s `resolve_ident` reads any node by id and exposes six attributes
(`progress/frame/length/position/index/active`). Effects WRITE registers
(`images` + int32 `scalars`) on cycler ticks; the flat render block READS registers + node
clocks every frame and calls one of five `RenderStmt::Op`s. `Copy` (`images[dst]=images[src]`)
already exists; `Effect::Kind` has **16** members and no random-walk; `RenderStmt::Op::Spiral`
carries **no** numeric fields and `render_spiral()` takes no params.

**Structural defaults (resolves the v2 open par-vs-seq question):** a pattern body is
**parallel** — its draws and drivers co-run over the box clock (this is what makes a driver
leaf and a persistent draw share one duration). Patterns listed under a `seq { ... }`
adjective on the header run **sequentially**. Authors tag the box; they never write bare
`par`/`seq` statements.

---

## 4. Primitive set (syntax + exact lowering)

### 4.1 `pattern` — the only structural noun

```
pattern NAME for <len> [seq | loop N] { <body> }
<len> ::= <N>f | beats N | locked
```

- **Lowering.** A `Node` with `id = NAME` minted into the `NodeMap`
  (`pattern_compiler.cpp:103`). Default body = `ParallelCycler` over its children (draws and
  drivers co-run). `seq` ⇒ `SequenceCycler`. `loop N` ⇒ `RepeatCycler(count=N)` wrapping the
  body. `for <N>f` sets leaf lengths so `length()==N`, making `.progress` a true `0..1`
  clock. The top-level pattern ⇒ `One{ init-Action(SpiralNew/Themes), body }`, exactly as
  `parse_pattern` does today.
- **`beats N`** sets length to `N * locked_frames` (the compile-time entrainment period,
  `director.cpp:88`). **`locked`** sets length to `locked_frames`. Hard-error if no pulsed
  bed exists, exactly as `every locked` does today.
- **Modder note.** A pattern is a named box with a length; everything inside shares its
  `0..1` clock. Put a pattern inside a pattern to nest. Children run together unless you
  tag the box `seq`.

### 4.2 `image` / `word` / `caption` / `draw` — DRAW effects

```
image  <content> [-> REG] <param-mod>*
draw REG  <param-mod>*               # draw an existing image register without re-pulling
content ::= concept | reward          # the bi-thematic alternate bool — see §8
```

- **Lowering.** Two halves: (1) a schedule `Effect{Image, slot}` writes
  `regs.images[REG] = get_image(alternate)` (`concept`→`Slot::Primary`,
  `reward`→`Slot::Alternate`); (2) a `RenderStmt{Op::Image, image_reg=REG}` whose
  `alpha`/`origin`/`zoom` fields are the lowered modulator strings. `draw` emits only
  the draw half (no pull), so any register (e.g. `prev`) is drawable.
- **Modder note.** Draws a picture. `-> REG` names the layer so `copy`/crossfade can reach
  it; omit it for an auto-named layer. **Bi-thematic is a hard floor:** `concept`/`reward`
  are the entire content vocabulary; a third theme does not lower (§8).

### 4.3 `zoom` / `fade` / `origin` / `alpha` — CURVE-DRIVE effects (the unified class)

```
zoom   <mod>
origin <mod>
alpha  <mod>
fade   (in | out | inout | <mod>)
```

- **Lowering — pure `RenderStmt` param strings, NO runtime change.** Written on a draw
  (e.g. `image concept zoom (curve 0 -> 0.5) fade in`):
  - `zoom M`  ⇒ `RenderStmt.zoom  = lower(M)`
  - `origin M`⇒ `RenderStmt.origin= lower(M)`
  - `alpha M` ⇒ `RenderStmt.alpha = lower(M)`
  - `fade in`    ⇒ `alpha = "this.progress"`
  - `fade out`   ⇒ `alpha = "1 - this.progress"`
  - `fade inout` ⇒ `alpha = "1 - abs(2*this.progress - 1)"`

  `this` substitutes to the enclosing pattern's minted id. Evaluated every frame by
  `render_eval`. (Note: drawn zoom is post-scaled by the program `zoom_intensity` setting,
  `api.cpp:308` — documented, not hidden.)
- **Modder note.** zoom, fade, brightness are the SAME thing: a number a curve moves over
  the pattern's life. `fade in/out/inout` are named curve shapes; write a raw curve for
  anything else.

### 4.4 `spiral` — DRAW + the spiral SPEED axis only

```
spiral [speed <mod>]
```

- **Lowering — DRAW half:** `RenderStmt{Op::Spiral}` (parameterless `render_spiral()`,
  unchanged). **SPEED half:** see §9 Extension #1. Until that lands, `spiral speed M` lowers
  to the **stairstep**: a `Seq` of `spiral` Action leaves each firing
  `Effect{SpiralRot, rate=literal_k}` sampled from `M` at segment boundaries. With Extension
  #1, `spiral speed M` lowers to `RenderStmt{Op::Spiral, speed=lower(M)}` read live, exactly
  like zoom.
- **Shape/width/color are NOT modulators** — they are settings (§8).
- **Modder note.** Spiral SPEED is a curve just like zoom. Spiral SHAPE/COLOR are picked in
  the `look {}` settings header, not animated — the grammar only spins it.

### 4.5 `drunk` — CURVE-DRIVE effect: a random wander with an intensity knob

```
drunk <intensity-mod> [on origin | zoom]     # used as a driver line, OR
image concept origin (drunk <intensity-mod>) # used as a param modulator
```

- **What it morphs.** By default `origin` (the zoom pivot, a wandering off-center stagger),
  optionally `zoom` (a breathing wobble). It does **not** warp geometry — the painter's
  palette is `alpha/origin/zoom` only; a true positional/rotation warp is named but
  declined (§9, Extension #1b, out of scope for v3).
- **How intensity works.** The intensity is itself a modulator
  (`drunk (curve 0 -> 0.4)` ramps drunkenness up), scaling the per-tick random step exactly
  as `SpiralRot.rate` scales rotation. Intensity 0 = still; larger = wilder.
- **Lowering — requires §9 Extension #2 (one new `Effect::Kind::Walk`).**
  1. **State.** A scalar register `W`, stored in fixed-point **milli-units** (registers are
     `int32`), zero-centered and clamped.
  2. **Walk effect.** A per-frame `Action` leaf (length 1) inside the pattern fires
     `Effect{Kind::Walk, target=W, rate=intensity}`. One new `run_effect` case (beside
     `Inc`/`Roll`):
     `regs.scalars[W] = clamp(regs.scalars[W] + int(rate*1000)*(int(random(3))-1), -BOUND, +BOUND)`
     — a bounded zero-centered walk using the engine's existing RNG (same channel as `Roll`).
     (Critic note: a clamped walk biases toward center over long runs; `BOUND` is chosen
     loose and intensity-scaled so a typical wander never approaches it.)
  3. **Read.** The modulator compiles to `"0.001 * W"` fed into the chosen draw param.
- **Modder note.** Makes a picture stagger/breathe randomly. `intensity` = how drunk
  (step size). It is spiral-speed's chaotic cousin.

### 4.6 `copy` / `set` / `inc` / `roll` — STATE effects (crossfade handoff, author-visible)

```
copy SRC -> DST
set  REG N
inc  REG N
roll REG ( N, N, ... )
```

- **Lowering.** `copy` ⇒ `Effect{Kind::Copy, target=DST, src=SRC}`
  (`regs.images[DST]=regs.images[SRC]`). `set`/`inc`/`roll` ⇒ the existing
  `Set`/`Inc`/`Roll` scalar ops. Effects run in leaf order, so `copy` written before a
  draw's pull is the A→B→C handoff. No runtime change.
- **Modder note.** `copy cur -> prev` stashes a layer so the next image can dissolve INTO it.
  This is the missing piece that lets crossfade be written by hand.

### 4.7 `every` / `loop` — CADENCE inside a pattern body (drives WHEN, not WHAT)

```
every <len> { <body> }
every <len> -> NAME { <body> }   # name the per-beat leaf so curves can ride its clock
loop N { <body> }
```

- **Lowering.** `every <N>f` ⇒ `RepeatCycler(count = span_len/N, child = Action(length=N))`.
  `-> NAME` mints the leaf's id into the `NodeMap` (closing the "atomic flash leaf is
  unnamed" gap — every per-beat clock is now name-addressable). `loop N` ⇒
  `RepeatCycler(count=N)`.
- **Honest limit.** `every <curve>` (ramped cadence) still unrolls into **un-id'd**
  per-segment leaves (`build_ramp`); a curve over a ramped cadence reads whole-ramp progress,
  not the active segment (Extension #3, §9). Per-flash wobble inside a ramp is unauthorable
  until per-segment ids are minted.
- **Modder note.** Repeats its body on a beat. Name it with `-> beat` so a zoom can ride
  that per-beat clock.

### 4.8 Modulators — the values that fill any param

```
modulator ::= literal | curve | wander | beat | rawexpr   [ over NAME ]
literal   ::= NUMBER
curve     ::= curve NUMBER -> NUMBER [ ease (linear | late) ]
wander    ::= drunk <intensity-mod>
beat      ::= beat
rawexpr   ::= [ EXPR ]                  # this/self substituted
```

- **Lowering.** All compile to a render `[expr]` string read each frame:
  - `curve A -> B` ⇒ `"(A + (B-A) * <clock>.progress)"`
  - `<literal>`    ⇒ `"V"`
  - `beat`         ⇒ `"<beatclock>.progress"` (a `Repeat(length=locked_frames)` node)
  - `[expr]`       ⇒ passthrough with `this`/`self` substituted
  - `over NAME`    ⇒ swaps `<clock>` from `this` (enclosing pattern id) to ancestor `NAME`'s id
- **Only `linear` and `late` eases are implemented.** Any other ease word is a hard parse
  error (do not silently fall back to linear).
- **No runtime curve object** — curves are compile-time string sugar (recon-confirmed).

---

## 5. The unified curve-drivable effect class (and the spiral split)

zoom, fade, spiral-SPEED, and drunk-intensity are **one class**: *a number driven by a
modulator over a pattern's clock*, written `<param> <modulator> [over NAME]`. The class is
defined by lowering to `value · shape(clock.progress)` (or a register read for `drunk`).

| Effect | Where it writes | Lowers today? |
|---|---|---|
| `zoom` | `RenderStmt.zoom` | **Yes**, live `[expr]`. |
| `fade`/`alpha` | `RenderStmt.alpha` | **Yes**, identical machinery. |
| `origin` | `RenderStmt.origin` | **Yes**, live `[expr]`. |
| `spiral speed` | SpiralRot rate / new speed field | **Stairstep today**; continuous needs Extension #1. |
| `drunk` | scalar register → origin/zoom `[expr]` | Needs Extension #2 (`Walk`). |

**Honesty about the asymmetry.** zoom/fade/origin are *already* one class at the runtime
floor (interchangeable `[expr]` fields on `RenderStmt`). Spiral speed cannot reach the draw
call today because `render_spiral()` takes no params and rotation lives in a stateful
accumulator advanced only by `SpiralRot`. So spiral speed **joins** the class at the grammar
surface, and is **second-class at runtime** (visibly stepped) until Extension #1 lands. v3
does not claim spiral is fully first-class without that extension.

**The spiral types/color split (the named tension), resolved three ways:**

- **SPEED → GRAMMAR curve.** The only per-frame continuous spiral axis.
- **TYPE (1 of 7) / WIDTH → SETTINGS selector** (`look { spiral type=N width=W }`). These are
  discrete identity chosen once; today they are *random-only* via `change_spiral` (which
  no-ops 25% of the time). v3 adds a deterministic setter (§9, Extension #3) so the author
  can *select* a shape — but never *curve* it, because a shape is not a continuum.
- **COLOR / DIRECTION → SETTINGS** (per-Program proto: `spiral_colour_a/b`,
  `reverse_spiral_direction`). Unreachable from grammar and kept that way; grammar-driven
  color would require per-frame color uniforms (a real extension) and crosses the
  pattern/session boundary. Explicitly declined.

So "make spiral first-class like zoom" is **TRUE for speed, PARTLY-true for shape/width
(selector, not curve), FALSE for color.** The grammar reflects this asymmetry instead of
pretending spiral is uniform.

---

## 6. Crossfade emerges from primitives (no keyword)

Crossfade is the canonical worked example of "effect param = modulator" plus the exposed
`copy`/`prev`. The author writes a looping pattern whose body, each beat, stashes the
current image into `prev`, pulls a fresh `cur`, draws the old layer first, and fades the
new layer in above it. This matches the renderer's source-over blending: visually the old
image fades out as the new image fades in, without needing a baked crossfade keyword.
**The beat clock is visible**, which is how the double-zoom trap is avoided:

```
pattern xfade for beats 8 loop 8 {
  pattern life for beats 2 {            # the 2-beat image-life clock (VISIBLE)
    every (beats 1) -> beat {           # per-beat handoff leaf, id = beat
      copy cur -> prev                  # stash last beat's image (Effect::Copy)
      draw prev          zoom (curve 0.5 -> 1.0)
      image concept -> cur fade in zoom (curve 0.0 -> 0.5)
    }
  }
}
```

**Lowering, all to existing primitives:**

- The `every (beats 1) -> beat { copy; draw; image }` body lowers to a `RepeatCycler` whose
  leaf (id=`beat`) runs `Effect{Copy, cur->prev}` then `Effect{Image, ->cur}` **in order**
  (`compiled_visual.cpp:195`).
- The two draws lower to `RenderStmt{Op::Image}` with `image_reg` `prev`/`cur`. `prev` draws
  at full alpha underneath; `cur` has `alpha = "beat.progress"`. **The fade rides the
  per-beat `beat` clock** (NOT the whole `xfade` Repeat - this is the bug a critic caught
  in two proposals).
- The dissolve A→B→C falls out because `prev` literally IS last beat's `cur` (Copy
  semantics), reproducing the v2 hand-baked branch with zero baked C++.

**The two-beat double-zoom trap, solved legibly.** An image lives in two visible halves:
first as `cur`, then after `copy cur -> prev` as `prev`. Both halves ride the same per-beat
clock: `cur` zooms `0.0 -> 0.5`, and on the next beat the copied `prev` continues
`0.5 -> 1.0`. The hidden `+0.5` offset magic disappears because the split is written in the
source. Anchoring these zooms to the whole `life` clock is the regression to avoid: the second
image would enter already halfway zoomed, and the copied image would jump at the handoff.

`crossfade` survives only as an **optional, printable macro** (`--expand`) that expands to
exactly this text. The expansion IS the lowering path, never a parallel re-implementation.

---

## 7. Nesting model

### 7.1 Patterns nest patterns (structural, free)

```
pattern  ::= "pattern" NAME "for" len [arrangement] "{" body "}"
body     ::= ( effect | pattern | cadence )*
```

A pattern body may contain nested patterns to any depth. Each nested `pattern` ⇒ a child
`Node` with a minted `id`; sibling patterns co-run (Parallel, the body default) unless under
`seq`. `compile_impl` already recurses with zero special cases; `resolve_ident` reads any
node by id. **No runtime change.**

### 7.2 Curves/effects nest over a sub-pattern duration (the one rule)

Every modulator reads `this.progress` = the enclosing pattern's clock. To ride an ancestor's
longer clock, suffix `over NAME`. This generalizes the fixed `flash/section/pattern` trio to
"any enclosing named pattern." Because every nesting level mints an addressable id (including
the innermost per-beat leaf via `every ... -> NAME`), `over <any-ancestor>` resolves through
the flat `NodeMap` for free.

**Worked semantics.** A child pattern's `.progress` sweeps `0..1` over ITS length and resets
per child iteration; the same effect reading `over PARENT` sweeps once across the whole
parent. This is the flash-vs-section distinction, generalized to arbitrary depth.

### 7.3 Parallel vs sequence without raw par/seq

A pattern declares its arrangement as an **adjective on its own header**
(`pattern X for Nf seq { ... }`); the default is **parallel** so a per-frame/per-beat driver
leaf and a persistent draw co-run over the box clock (fixing mod's seq-default co-run bug).
Authors never write bare `par`/`seq` statements — they tag the box.

### 7.4 Compile-time resolution check (mandatory)

Because `render_eval` silently resolves unknown identifiers to `0.0`, the compiler — which
holds the full pattern-id and register declaration list — MUST verify, at parse time, that:

1. every `over NAME` names a real enclosing pattern id;
2. every wired modulator name resolves to a declared register/pattern;
3. (warning) a `prev`/`cur` zoom pair in a dissolve is anchored to the same-length clock.

A failure is a parse error (or warning for #3), never a silent dark screen. This is the
single most important safety property for non-programmer authors.

### 7.5 Honest carry-forward limits

- `OffsetCycler` pre-rolls its child silently with `trigger_actions=false` and phase-shifts
  its clock; any `over` read of an offset ancestor inherits this shift.
- `ParallelCycler` length = LCM of children; co-prime-length parallel children blow up the
  parent clock and make `over PARENT` sweep a surprising period.
- Ramped-cadence per-segment leaves stay un-id'd (Extension #3) — per-flash wobble inside an
  accelerate is unauthorable.
- Bi-thematic is a hard floor (§8); nested patterns cannot each carry their own theme.

---

## 8. Grammar-vs-settings decision table

**Rule:** GRAMMAR owns time-varying scalars (a curve can move them per frame); SETTINGS own
static identity/palette (chosen once, constant for the run). Settings live in a per-(sub)pattern
`look { ... }` header that writes proto fields, never per-frame `[expr]`s.

| Knob | Tier | Where | Why |
|---|---|---|---|
| image/text `alpha` (fade) | GRAMMAR curve | `RenderStmt.alpha` | per-frame `[expr]` |
| `zoom` amount | GRAMMAR curve | `RenderStmt.zoom` | per-frame `[expr]` |
| `origin` (zoom pivot) | GRAMMAR curve | `RenderStmt.origin` | per-frame `[expr]` |
| `zoom_intensity` ceiling | SETTING | Program proto | master multiplier, constant for run |
| spiral **SPEED** | GRAMMAR curve | SpiralRot rate / Ext #1 | continuous per-frame scalar |
| spiral **TYPE** (1 of 7) | SETTING selector | `look { spiral type=N }` (Ext #3) | discrete identity, no continuum |
| spiral **WIDTH** | SETTING selector | `look { spiral width=W }` (Ext #3) | discrete identity |
| spiral **COLOR** a/b | SETTING | Program proto | blended shader uniforms; session concern |
| spiral **DIRECTION** | SETTING | Program proto | global sign; grammar `speed` is magnitude |
| `drunk` intensity | GRAMMAR curve | scalar reg → `[expr]` (Ext #2) | per-frame scalar |
| `drunk` target | fixed (origin/zoom) | — | no new warp draw op in v3 |
| content theme (concept/reward) | GRAMMAR selector | the alternate bool | only content axis; 3+ themes do NOT lower |
| `global_fps`, font, theme weights | SETTING | Program/Theme proto | session/program identity |

**Direction note.** A signed `speed` modulator interacts with `reverse_spiral_direction`;
grammar `speed` is **magnitude**, the setting owns global **sign** — documented to avoid
double-control.

---

## 9. Required runtime extensions (named, ranked by cost)

Everything in §4–§8 lowers with **zero runtime change** EXCEPT the three below. Each has a
graceful-degradation path so the grammar surface never changes.

1. **Continuous spiral speed (~10 lines).** Add a `speed` `[expr]` field to
   `RenderStmt::Op::Spiral` and change `render_spiral()` → `render_spiral(float speed)` with a
   `_spiral_speed` read each frame. Touch points: `render_eval.cpp:290`, `api.cpp:303`,
   `api.h:60`. **Until then:** `spiral speed <curve>` lowers to the stairstep unroll (visibly
   stepped, but correct).
   - *1b (declined for v3):* a true positional/rotation/warp `drunk` would need a new
     `RenderStmt` offset/rotation param + vertex-shader uniform. Out of scope; `drunk warp`
     hard-errors until added (the way `spiral locked` errors today).

2. **`Effect::Kind::Walk` for drunk (one enum + one `run_effect` case + one register
   convention).** A bounded zero-centered random walk on an `int32` milli-unit scalar register,
   using the existing RNG. It does NOT lower to `Roll` (a one-shot re-roll) or `Inc` (a fixed
   step). This is the smallest honest extension, in the spirit of `Burst`/`SuperFastTick` being
   "the one concession to state."

3. **Deterministic spiral selector (`SpiralSet`).** A setter beside `change_spiral` so
   `look { spiral type=N width=W }` pins type/width instead of re-rolling. Without it the
   grammar can only RE-ROLL, not select. (Promoting type/width to a per-Program proto field is
   the alternative.)

**Out of scope / hard non-goals:** grammar-driven spiral color (per-frame color uniform);
3+ simultaneous themes (two live theme slots, VRAM budget); live phase-accurate beat
(`every locked` uses a compile-time period, not a live audio clock); per-segment ramp ids
(Extension #3, a separate unroller change).

---

## 10. Worked examples

### EX1 — simplest draw

```
pattern hello for 240f {
  image concept zoom (curve 0 -> 0.5)
}
```
Lowers to `One{ Action[Effect{Image,Primary,->auto}],
RenderStmt{Image, zoom="(0 + 0.5*hello.progress)"} }`. One line: draw concept, zoom from 0
to 0.5 over its 240f life.

### EX2 — one number as curve, fade, AND spiral speed (the unified class)

```
pattern unified for 240f {
  image concept zoom (curve 0 -> 0.5) fade inout
  spiral speed (curve 0.1 -> 1.0)
}
```
`zoom` ⇒ `"(0+0.5*unified.progress)"`; `fade inout` ⇒ `alpha="1-abs(2*unified.progress-1)"`;
`spiral speed` ⇒ (with Ext #1) `RenderStmt{Spiral, speed="(0.1+0.9*unified.progress)"}`, else
the stairstep `Seq` of `SpiralRot` literals. All three are the same shape: a modulator over
`unified.progress`. The body is parallel, so all three co-run.

### EX3 — NESTED pattern; inner clock vs outer clock via `over`

```
pattern show for 480f seq {
  pattern flashA for 240f {
    image concept zoom (curve 0 -> 0.3)            # rides flashA (resets at 240f)
  }
  pattern flashB for 240f {
    image reward  zoom (curve 0 -> 0.6 over show)  # rides the WHOLE 480f show clock
  }
}
```
Lowers to `One{ init, Seq{ flashA:One{...}, flashB:One{...} } }`, both ids in the `NodeMap`.
`flashA`'s zoom anchors `flashA.progress`; `flashB`'s zoom anchors `show.progress` — the
generalized `over`-anchor, no runtime change.

### EX4 — crossfade EMERGES (no keyword)

See §6. The body's per-beat `beat` leaf runs `Copy`, draws `prev`, then pulls/draws `cur`
in order. The new `cur` layer fades in on `beat.progress`; both zoom halves also ride that
beat clock (`0.0 -> 0.5`, then `0.5 -> 1.0` after the copy). The baked crossfade branch is
deleted.

### EX5 — the new DRUNK effect; intensity ramps up

```
pattern stagger for 300f {
  image concept origin (drunk (curve 0 -> 0.3))
}
```
Lowers to a length-1 `Walk` leaf inside `stagger` firing `Effect{Kind::Walk, target=W_cur,
rate=intensity}` each frame (a milli-unit zero-centered clamped walk via the existing RNG);
`RenderStmt{Image, origin="0.5 + 0.001*W_cur"}`. Intensity (itself a curve) scales the step,
so the picture starts still and wanders harder over 300f. Requires Ext #2.

### EX6 — re-authored `flash_text` (replaces the v1/v2 baked preset)

```
pattern flash_text for beats 16 loop 16 {
  look { spiral type=3 width=6 }        # SHAPE/COLOR are settings, pinned once
  spiral speed 0.4                       # constant spin (grammar magnitude)
  pattern life for beats 2 {
    every (beats 1) -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image concept -> cur fade in zoom (curve 0.0 -> 0.5)
    }
  }
}
```
`flash_text` built entirely from primitives: per-beat copy+pull handoff, source-over fade-in,
and two explicit zoom halves instead of the hidden `+0.5` hack. Spiral SPEED is a grammar
magnitude; spiral SHAPE/COLOR are settings. No crossfade keyword, no super_fast FSM, no
baked opacity ladder — every line the modder sees is
`<effect> <param> <modulator>`.

---

## 11. Full EBNF

```
pattern        ::= "pattern" NAME "for" len [arrangement] "{" body "}"
arrangement    ::= "seq" | "loop" NUMBER
len            ::= NUMBER "f" | "beats" NUMBER | "locked"
body           ::= ( stmt )*
stmt           ::= look | pattern | cadence | draw_effect | drive_effect | state_effect

look           ::= "look" "{" look_prop* "}"                       (* SETTINGS, not per-frame *)
look_prop      ::= "spiral" ( "type" "=" NUMBER | "width" "=" NUMBER )

cadence        ::= "every" len [ "->" NAME ] "{" body "}"
                 | "loop" NUMBER "{" body "}"
                 | "every" curve "{" body "}"                       (* ramped: un-id'd segments *)

draw_effect    ::= ("image" | "word" | "caption") content [ "->" REG ] param*
                 | "draw" REG param*
content        ::= "concept" | "reward"                            (* the bi-thematic bool *)

drive_effect   ::= "zoom"   modulator
                 | "origin" modulator
                 | "alpha"  modulator
                 | "fade"   ( "in" | "out" | "inout" | modulator )
                 | "spiral" ( "speed" modulator )?
                 | "drunk"  modulator [ "on" ("origin" | "zoom") ]

state_effect   ::= "copy" REG "->" REG
                 | "set"  REG NUMBER
                 | "inc"  REG NUMBER
                 | "roll" REG "(" NUMBER ("," NUMBER)* ")"

param          ::= ("zoom" | "origin" | "alpha") modulator
                 | "fade" ("in" | "out" | "inout" | modulator)
                 | "drunk" modulator [ "on" ("origin" | "zoom") ]

modulator      ::= ( literal | curve | wander | beat | rawexpr ) [ "over" NAME ]
literal        ::= NUMBER
curve          ::= "curve" NUMBER "->" NUMBER [ "ease" easeword ]
wander         ::= "drunk" modulator                               (* intensity is itself a mod *)
beat           ::= "beat"
rawexpr        ::= "[" EXPR "]"                                     (* this/self substituted *)
easeword       ::= "linear" | "late"                               (* only these implemented *)
EXPR           ::= (* render_eval grammar: ?: and/or compare +-*/%^ min/max/abs,
                      <node>.(progress|frame|length|position|index|active), <scalar> *)
```

---

## 12. Open questions

See `open_questions` in the accompanying plan; the load-bearing ones are the drunk
clamp-bias behavior, whether spiral type/width should be a `look {}` selector or promoted to a
proto field, and whether `beat` should remain a compile-time period or wait for a live audio
clock.

---

## 13. V3 enhancements to consider next

These are intentionally **not** shipped in v3 today. They are the next places where v3 can regain
more of the old hardcoded patterns' capability without falling back into implementation-shaped
grammar.

### 13.1 Expose the existing burst/random cycler in v3

The runtime already has `BurstCycler` and the legacy grammar can parse `burst { ... }`
(`pattern_parser.cpp`). V3 did not surface it; it only exposes lighter randomness:

- `chance P` on draw effects, lowered to a roll + guard.
- `anim every Nth`, lowered to a pulse gate.
- `runtime` slots, which pick primary/secondary at fire time.

That means the v3 `super_fast` reauthor keeps the felt rapid-cut intent, but loses the old
"short random animation burst with cooldown" shape. A v3 surface should expose the existing
cycler before inventing new randomness machinery. Candidate shape:

```text
pattern super_fast for 2048f {
  burst rapid length 2048f period 8f chance 1/256 cooldown 16f duration 128f..256f {
    base  { image runtime zoom 0.5 }
    burst { image runtime anim zoom (curve 0 -> 1 over rapid) }
  }
  word concept chance 0.25
  spiral speed 3
}
```

The exact syntax can change, but the lowering should stay boring: one `Node::Burst`, base effects,
burst effects, and `burst.index` available to render expressions.

### 13.2 Parent-clock envelopes are good; live curve lengths are risky

The `ACCELERATE` simplification shows the current v3 weakness: three fixed phases are legible,
but they no longer express a true accelerating cadence. The right conceptual primitive is a
shared parent envelope:

```text
pattern accelerate for 3000f {
  every ramp 56f -> 12f steps 45 ease late -> cut {
    image concept zoom (curve 0 -> 0.5)
  }
  spiral speed (curve 2 -> 6 over accelerate)
}
```

The parent clock (`accelerate.progress`) is the right source for whole-pattern envelopes:
spiral speed, global intensity, text probability, and maybe palette/warp intensity. It was **not**
the right fix for `FLASH_TEXT` by itself, because image lifetime there is per-register state:
`cur` becomes `prev` with an age offset. For handoffs, explicit local halves (`cur` 0.0->0.5,
copied `prev` 0.5->1.0) are still safer than a global parent clock.

Avoid making `for <curve>` or `every <curve>` a live runtime length. Cycler lengths are structural:
`ParallelCycler` computes LCMs, `SequenceCycler` sums children, `RepeatCycler` indexes by child
length, and render expressions read `.length`/`.progress`. If a node's length changes while it is
running, all of those invariants get messy.

Preferred lowering: **compile-time sampled ramp expansion**.

- Parse `every ramp A -> B steps N [ease ...] -> NAME { body }`.
- Sample N integer durations from the curve at parse time.
- Lower to a `SequenceCycler` of fixed `ActionCycler` leaves.
- Mint stable ids for the active segment, e.g. `cut` for the current segment and optionally
  `cut_00`, `cut_01`, ... for inspection.
- Keep render modulators anchored to either the local segment (`cut.progress`) or the parent
  (`over accelerate`).

This recovers most of hardcoded `ACCELERATE`'s capability without making pattern lengths dynamic
or adding a general algebra language to `for`.

### 13.3 Keep `[expr]` math as render math, not scheduling algebra

Raw `[expr]` is useful for render-time values because it is side-effect-free and evaluated against
live registers/node clocks. Extending it into structural lengths (`for [expr]f`) should stay
compile-time only unless there is a very strong reason. If a future syntax needs math in a length,
prefer constants and generate-time bindings over live node attributes.

---

## Appendix A — Historical review findings

This appendix is retained as implementation history. The shipped behavior is defined by
Sections 0-11 above and the parser/runtime source. Some findings below are now resolved or
intentionally superseded.

This spec is the output of a multi-agent design workflow (18 recon analysts, 5 divergent design proposals, 15 adversarial critiques, synthesis, then a completeness critic). **Note:** 5 recon analysts failed (including the dedicated *spiral* and *drunk-feasibility* readers), so those areas were grounded by the designers/critic reading the code directly rather than a dedicated pass. The completeness critic (confidence **76/100**) found the gaps below. Treat §1–§12 as a strong draft, **not** final, until these are resolved.

### A.1 Gaps (the load-bearing one is first: text cannot crossfade)
- TEXT CANNOT CROSSFADE OR STASH — FATAL for the flagship example (EX6 §10, and §6/§4.6). `word`/`caption` lower to Effect::Kind::Text/SmallSub which call api.change_text()/change_small_subtext() mutating a SINGLE internal VisualControl text queue (_current_text in api.cpp:114-129). RenderStmt::Op::Text (render_eval.cpp:313-318) calls render_text(origin,zoom,shadow_origin,shadow_zoom) — it takes NO content register and NO alpha. Copy only moves images[] (compiled_visual.cpp:175-176), there is no text register. Therefore EX6's `word concept -> cur` + `image-reg prev/cur fade out/in` DOES NOT LOWER: text has exactly one live slot, cannot be Copy'd to `prev`, and cannot be alpha-faded at all. The doc presents flash_text (text crossfade) as built 'entirely from primitives' but the crossfade-from-primitives mechanism (§6) is image-only. The doc must either (a) restrict crossfade to images, (b) name a text-register + render_text-alpha runtime extension, or (c) re-author flash_text as image content. §4.2 lists `word`/`caption` as DRAW effects with `-> REG` and `alpha`/`fade` params as if symmetric with `image`; they are not.
- The `-> REG` register-naming and `image-reg REG` (draw-without-pull) primitives (§4.2) are NOT verified to exist in the current parser/runtime. The runtime CAN draw any image register (RenderStmt.image_reg, render_eval.cpp:293-294 reads images[name], empty if unset), so image-reg lowers, but the v2 parser's `-> REG` path (pattern_parser_v2.cpp:783) defaults to 'current' and the doc never confirms an `image-reg` statement exists in v2 — it is a NEW parser surface the plan must build (Phase 1), not an existing lowering. The doc's framing ('omit the pull half') is sound for IMAGES only.
- §4.1/§4.3 conflate cycler leaves with render statements when justifying 'parallel-by-default'. A DRAW is a flat RenderStmt evaluated every frame (render_eval.cpp:285), NOT a cycler child with a length. The Walk leaf (§4.5) and `every` cadence ARE cycler ActionCyclers. So 'a driver leaf and a persistent draw co-run over the box clock' is half-true: the draw never participates in the Par/LCM at all; only schedule-side effect leaves do. The parallel-default matters for co-running TWO effect leaves (e.g. a Walk leaf + an `every`-copy leaf), not for draws. The rationale in §2/§3 ('fixing mod's seq-default co-run bug') is correct for effect leaves but the doc's wording implies draws are scheduled.
- §4.4 / §9 Ext#1 spiral stairstep degradation is under-specified for the continuous-curve case. The stairstep lowers `spiral speed M` to a Seq of SpiralRot literal-rate leaves sampled at segment boundaries — but the doc never says HOW MANY segments, what the segment length is, or how the sampling interacts with `over NAME` (a curve riding an ancestor clock cannot be statically sampled into fixed-rate segments without knowing the ancestor's period). build_ramp in v2 (the existing unroller) produces UN-ID'd segments (§4.7 honest limit, §7.5), so a spiral-speed curve `over life` has no defined stairstep lowering. Gap: stairstep segment count/length and its incompatibility with `over` curves.
- §9 Ext#2 drunk clamp/BOUND and fixed-point milli-units are asserted but the int32 register range and frame-rate dependence are unverified. random(T) is std::uniform_int_distribution{0,max-1} (util.h:28-32) so `random(3)` gives 0/1/2 and `int(random(3))-1` gives -1/0/1 (correct, verified). BUT: the per-frame step is NOT normalized for global_fps (recon: rotate_spiral normalizes by 32*sqrt(width); the doc's Walk has no analogous normalization), so wander speed changes meaning if global_fps changes. The doc's open-question on clamp-bias (§12) acknowledges bias but not fps-dependence. Also `int(rate*1000)` truncates small intensities to 0 — at intensity 0.0005 the step is 0 every frame (dead zone). Gap: fps-normalization + small-intensity truncation in the milli-unit encoding.
- §9 Ext#3 (SpiralSet deterministic selector) is required for `look { spiral type=N width=W }` (§8 table, EX6) but NO graceful-degradation path is given for it, unlike Ext#1/#2. change_spiral() re-rolls randomly and no-ops 25% (api.cpp:92,95-96); there is NO existing setter. So until Ext#3 ships, `look { spiral type=N }` cannot lower AT ALL (it can only re-roll). The doc claims (§9 intro) 'each has a graceful-degradation path so the grammar surface never changes' — false for Ext#3. EX6 uses `look { spiral type=3 width=6 }` as if it works.
- §7.4 compile-time resolution check #3 ('warn when prev/cur zoom pair anchored to different-length clocks') is under-specified: how does the compiler know two draws form a 'dissolve pair'? There is no `prev`/`cur` typing in the runtime — register names are pure convention (recon: 'current/prev/next... are conventions established by the patterns, not types'). The compiler cannot generically detect a dissolve pair without hardcoding the `prev`/`cur` names, which re-introduces the baked-convention coupling the redesign wants to remove. Either the check is hardcoded to those names (a baked convention) or it cannot be implemented as stated.
- spiral 'direction = magnitude vs sign' (§8 direction note, §5) is asserted but rotate_spiral already flips sign via reverse_spiral_direction (api.cpp:78). The doc says grammar `speed` is magnitude and the setting owns sign — but a curve `0.1 -> 1.0` produces only positive rates today; there is no defined behavior for a NEGATIVE speed curve, and the interaction with the existing sign-flip is documented but not lowered. Minor, but the 'avoid double-control' claim needs the Ext#1 implementation to actually drop the setting's sign or define precedence.
- Resolved: the plan needed a golden around FLASH_TEXT's zoom-continuity invariant before deleting the baked crossfade branch. The shipped version uses explicit per-beat zoom halves (`cur` 0.0->0.5, copied `prev` 0.5->1.0) and `tests/v3_grammar_test.cpp` checks that fade and both zoom halves ride the same beat clock.

### A.2 Verified-false / unverified claims
- §4.2 / EX6 / §6: that `word`/`caption` (text) can be drawn from a named register with complementary fades. VERIFIED FALSE — text has one live slot, no register, no alpha (api.cpp:114-129,206-228; render_eval.cpp:313-318). Crossfade-from-primitives is IMAGE-ONLY.
- §4.2: that an `image-reg REG` statement and `-> REG` register naming already exist as lowerings. The runtime supports drawing arbitrary image registers (render_eval.cpp:293-294) but the v2 parser hardcodes target='current' (pattern_parser_v2.cpp:783); these are NEW parser surfaces to build, not existing lowerings. Doc's 'lowers with zero runtime change' is true for the runtime, not for the parser.
- §9 intro: 'each [extension] has a graceful-degradation path so the grammar surface never changes.' FALSE for Ext#3 (SpiralSet) — without it, `look { spiral type=N }` cannot select at all, only re-roll (api.cpp:90-97).
- §4.5/EX5: that `int(rate*1000)*(int(random(3))-1)` is a well-behaved bounded walk. random(3) verified correct (-1/0/1), but the milli-unit truncation creates a dead zone for intensity < 0.001 and the step is not fps-normalized (unlike rotate_spiral, api.cpp:81).
- §7.4 #3: that the compiler can detect a 'prev/cur zoom pair in a dissolve' generically. Register names are untyped conventions (pattern_runtime.h); detection requires hardcoding names.
- §4.1: that `beats N` and `locked` lower today. `every locked` works (pattern_parser_v2.cpp:796) but `spiral locked` hard-errors unconditionally (pattern_parser_v2.cpp:677-683); the doc's §4.4 spiral does not claim `locked` but §4.1 `locked` len + §9 'live phase-accurate beat out of scope' should explicitly note spiral-locked is dead.
- §3/§4.1: that a pattern body 'parallel by default' makes draws and drivers 'share one duration'. Draws are flat RenderStmts, not cycler children (render_eval.cpp:285); only effect leaves participate in the Par/LCM (cyclers.cpp:210-213).

### A.3 Recommended fixes (do these before/early in implementation)
- Resolve the TEXT crossfade gap before Phase 1: either (a) restrict §6 crossfade and EX6 flash_text to IMAGE content and re-author flash_text with images, or (b) add a named runtime extension (Ext#4) for a text-content register + an alpha param on render_text(). The doc currently presents flash_text (the flagship 'every line is <effect><param><modulator>' proof) on a substrate (text) that cannot crossfade. This is the single most load-bearing fix.
- Add a graceful-degradation path for Ext#3 (SpiralSet) or move spiral type/width OUT of v3's must-ship surface. State explicitly that `look { spiral type=N }` is a hard-error (like `spiral locked`) until Ext#3 lands, mirroring the §9 honesty for the other extensions. Fix EX6 which uses it as if it works.
- Specify the stairstep lowering for `spiral speed <curve>` precisely: segment count/length, and explicitly hard-error (or forbid) `spiral speed <curve> over NAME` until Ext#1, since build_ramp segments are un-id'd and an ancestor-anchored curve cannot be statically sampled (§4.7, §7.5).
- Re-specify §7.4 check #3 without relying on untyped register-name conventions: either drop it, or make it a general 'two image draws of registers written by the same per-beat leaf, anchored to different-length clocks' structural check, and document that it is name-agnostic.
- Tighten §4.5 drunk lowering: define fps-normalization for the Walk step (parallel to rotate_spiral's 32*sqrt(width)), define behavior for intensity below the milli-unit resolution (dead zone), and pick BOUND relative to the milli-unit scale and global_fps. Fold fps-dependence into the §12 open question.
- Correct the §3/§4.1 'parallel-by-default co-run' rationale to distinguish schedule-side effect leaves (which Par/LCM) from flat render statements (which do not). The parallel default is justified by co-running multiple EFFECT leaves, not by co-running a draw with a driver.
- Done: add a golden test asserting the v3 FLASH_TEXT zoom halves ride the same beat clock, preventing the copied image from jumping at the handoff.

### A.4 Plan risks
- Phase 1 ports EX6's crossfade-from-primitives as a smoke pattern, but EX6 uses `word` (text) which cannot crossfade. Phase 1 will either fail to lower or silently render wrong (render_text ignores the register and alpha). The text-vs-image asymmetry must be resolved BEFORE Phase 1, not discovered in it.
- Phase 1 lists `image/word/caption/image-reg` draws as one unit lowering 'with zero runtime change', but word/caption text has no register/alpha and image-reg/`-> REG` are new parser surfaces. The phase under-scopes the text limitation and over-claims the zero-change property.
- Phase 2b changes render_spiral() signature and all callers 'in one commit' with a stairstep fallback flag — but the stairstep path for CURVED (non-literal) speed is itself unbuilt and incompatible with `over NAME` curves. The fallback may not exist for the cases 2b is meant to replace.
- Phase 2 bundles Ext#3 (SpiralSet / `look { spiral type/width }`) into the spiral phase but gives it no degradation path; if Ext#3 slips, every v3 builtin using `look { spiral ... }` (EX6) is unauthorable, blocking the Phase 4 cutover.
- Phase 4 deletes the v2 crossfade branch after v3 goldens 'match v2 visual behavior', but the only stated tolerance is 'same effect not same frames' for super_fast — the zoom-continuity invariant (the +0.5 double-zoom fix) is frame-shape-sensitive and could regress legibly without a dedicated golden.
- The plan keeps v2 and v3 parsers coexisting (Phase 0) and flips a director flag at Phase 4. Register-name collisions across nested v3 subpatterns sharing 'current'/'prev' (recon: registers are a single flat namespace, no scoping) are not addressed in any phase — nested patterns each minting `cur`/`prev` will clobber. This needs a register-mangling or scoping decision in Phase 1, not Phase 4.

### A.5 Ambiguous decisions needing a call
- Grammar-vs-settings for text: the doc puts image alpha/zoom/origin in GRAMMAR (§8) but never states that TEXT has no per-frame alpha/content-register at the runtime floor, so the grammar-vs-settings table implies text is symmetric with images when it is not. Decision needed: is text a draw-with-modulators (requires runtime extension) or a fixed-look draw (settings-tier, no fade)?
- §7.4 check #3 (dissolve-pair warning) leaves ambiguous whether the compiler hardcodes the `prev`/`cur` register names (a baked convention) or detects pairs structurally. The doc wants no baked conventions but the check as written needs them.
- spiral `speed` sign: §8 says grammar speed is magnitude and the setting owns sign, but whether a negative speed-curve is a parse error, clamped, or honored (overriding reverse_spiral_direction) is undecided.
- Whether `every <curve>` (ramped cadence, §4.7) is even allowed in v3 given its un-id'd segments break `over` anchoring and per-flash wobble — the doc names it an honest limit but does not decide if v3 ships it or defers it (Extension #3 territory).
- Register scoping for nested subpatterns: whether `cur`/`prev` are auto-mangled per subpattern or share one flat namespace (collision risk) is left unstated despite §7.1 claiming arbitrary-depth nesting.

### A.6 Open questions
- DRUNK clamp bias: a bounded zero-centered random walk on an int32 register biases toward the clamp center over long runs (unlike spiral's wrap), so a long-lived `drunk` may decay from 'staggers around' to 'hovers near middle'. Should the Walk effect wrap (like _spiral) instead of clamp, use a reflecting boundary, or add a weak restoring force? Needs empirical tuning of BOUND and intensity scaling against real footage.
- Spiral selector home: should `look { spiral type=N width=W }` lower to a deterministic SpiralSet *effect* (Extension #3) or be promoted to per-Program proto fields edited in the wxWidgets ProgramPage? The effect keeps it per-(sub)pattern and grammar-local; the proto field gives editor support but crosses into session config. Pick one before Phase 2b.
- `beat` modulator semantics: today `beat`/`locked` is a compile-time period (round(global_fps/pulse_hz)), not a live phase-accurate audio clock. v3 ships the compile-time version. Do we ever want true phase coincidence (a live audio->visual beat counter), which is a genuine runtime extension exposing the audio thread's pulse_phase to the render loop? Named but deferred.
- Per-segment ramp ids (Extension #3): `every <curve>` unrolls to un-id'd leaves, so per-flash wobble inside an accelerate is unauthorable. Is this worth a runtime/unroller change (mint per-segment ids) in a later milestone, or is whole-ramp progress an acceptable permanent limitation? Carried forward, not solved.
- Drunk as warp vs wobble: v3 confines drunk to origin/zoom (a zoom-pivot stagger). Some authors will expect positional jitter or image warp, which needs a new RenderStmt offset/rotation param + vertex-shader uniform. Is that demand strong enough to justify Extension #1b in a future version, or is the zoom-pivot wobble sufficient?
- super_fast re-authoring fidelity: Phase 4 re-authors super_fast from chance/roll primitives, accepting 'same effect, not same frames' (the v2 decision). Do we keep super_fast's C++ FSM as a `raw {}` escape hatch for exact reproduction, or fully commit to the randomness-primitive approximation and delete SuperFastTick?
- Macro/expansion drift: `crossfade`/`pulse` survive as printable macros whose expansion IS the lowering path. What enforces that the macro text and the primitive lowering never drift (the exact failure mode of the v2 crossfade keyword)? A golden test that the macro's --expand output re-parses to the identical Node/RenderStmt tree as hand-written primitives is the proposed guard — confirm it is sufficient.
---

## Appendix B — Phased implementation plan (build stays green at each step)

### Phase 0 — Spec + parser scaffolding (build stays green; no behavior change)

**Work:** Land docs/spec-grammar-v3.md (this design). Add a v3 parser entry point alongside patternv2::parse (do NOT remove v2). The v3 parser produces the SAME pattern::Node + vector<RenderStmt> IR, so it compiles through the existing pattern_compiler with zero runtime change. Wire it behind a feature flag / separate builtin set so nothing in the shipping path calls it yet.

**Stays green:** New code is additive and unreferenced by the live director path; existing v2 builtins and the equivalence tests are untouched, so the build and all current patterns compile and run exactly as before.

### Phase 1 — Core nouns + the zero-runtime-change subset (pattern nesting, draws, zoom/fade/origin, copy)

**Work:** Implement: `pattern NAME for len [seq|loop N] { body }` lowering to One/Seq/Par/Rep nodes with minted ids (parallel-by-default body); `image/word/caption/draw` statements; the curve-drive class (zoom/fade/origin/alpha) lowering to RenderStmt [expr] strings; modulators (literal/curve/beat/rawexpr) with `over NAME` anchor resolution; `every ... -> NAME` cadence minting per-beat leaf ids; `copy/set/inc/roll` state effects. Add the §7.4 compile-time resolution check (every `over NAME` and wired name resolves). Port EX1, EX3, and EX6's crossfade-from-primitives as v3 smoke patterns.

**Stays green:** This entire subset lowers to existing runtime constructs (verified: Copy effect exists, Image RenderStmt has alpha/origin/zoom, resolve_ident reads any minted id). No Effect::Kind or RenderStmt::Op changes. Existing equivalence/golden tests for v2 still pass; new v3 patterns get their own golden tests asserting they lower to the expected Node/RenderStmt shape.

### Phase 2 — Spiral speed (stairstep first, then Extension #1)

**Work:** Step 2a: implement `spiral` draw + `spiral speed <mod>` lowering to the STAIRSTEP unroll (Seq of SpiralRot literal-rate leaves) — zero runtime change, ships first. Step 2b (Extension #1): add a `speed` [expr] field to RenderStmt::Op::Spiral and change render_spiral() to render_spiral(float speed) with a _spiral_speed read each frame; switch `spiral speed` lowering to the live RenderStmt path. Add the deterministic SpiralSet setter (Extension #3) and the `look { spiral type/width }` header.

**Stays green:** 2a is additive and uses existing SpiralRot — green by construction. 2b changes one virtual signature (render_spiral) and all its callers in one commit, with the stairstep path kept as a fallback flag until the live path passes its golden test; the spiral draw default (parameterless behavior) is preserved when no speed expr is present, so existing patterns are unaffected.

### Phase 3 — The drunk effect (Extension #2)

**Work:** Add Effect::Kind::Walk to the enum and one run_effect case (clamped zero-centered milli-unit random walk using existing random()). Implement `drunk <intensity-mod> [on origin|zoom]` lowering to a length-1 Walk leaf + an origin/zoom [expr] reading the milli-unit register. Hard-error on `drunk warp` (declined Extension #1b). Port EX5. Validate clamp-bias behavior empirically and tune BOUND.

**Stays green:** Adding an enum member + a switch case is localized and exhaustive-switch-safe (all existing run_effect cases untouched); no existing pattern emits Walk, so prior behavior is identical. New drunk patterns are gated behind v3-only goldens.

### Phase 4 — crossfade/pulse macros, flash_text cutover, and v2 deprecation

**Work:** Implement `crossfade`/`pulse` as printable (--expand) macros whose expansion IS the Phase-1 primitive lowering (copy + `draw prev` + source-over fade-in of `cur`). Re-author all 8 builtins in v3 (flash_text per EX6). Once v3 goldens match v2 visual behavior (accepting 'same effect, not same frames' for super_fast), flip the director path to v3 builtins and delete the baked `crossfade` keyword branch from the v2 parser.

**Stays green:** Macros are sugar over already-tested primitives, so they add no new lowering risk. The v2 parser and its crossfade keyword are removed only AFTER the v3 builtins pass goldens and the director is switched, in a single commit with the old goldens retired; if anything regresses, the flag flips back to v2.
