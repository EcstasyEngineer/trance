# Trance Visual Grammar v3 — Design Specification

> Status: **SHIPPED, v3-only.** `pattern_parser_v3.{h,cpp}` + `builtin_patterns_v3.cpp` is now
> the ONLY grammar/parser — the v1 legacy parser (`pattern_parser.{h,cpp}`) has been retired and
> deleted this sprint, and the intermediate v2 was retired before it. There is no fallback
> parser; playback is v3-only. This document defines the v3 surface and its exact lowering to
> the cycler + effect + render-block runtime. The runtime extensions it needed are built:
> curve-driven spiral speed, the `SpiralSet` selector, the wave warp shader, and (landed after
> the original §9 estimate) the sampled ramp cadence and the `burst` surface (§13 is no longer
> speculative — both shipped; see §4.10/§4.11). The **one deferred** piece is a text-content
> register so text can crossfade like images (Ext#4) — the text path is a single live slot, not
> a register. Where this spec and any older prose disagree, **the parser and runtime enums are
> the source of truth** (`pattern_ast.h`, `pattern_parser_v3.cpp`, `render_eval.cpp`, `api.cpp`).
> §0 (locked decisions) governs the rest; §11's EBNF is regenerated directly from the parser and
> is the current normative grammar. Honest-limits tone is kept throughout; §12 and Appendix A/B
> remain as implementation history, not a live task list.

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
  silent fallback — corrects the A.1 note). **[Ext#3 has since shipped — see §9/§4.12;
  `look { spiral type=N }` lowers today, it no longer hard-errors.]**
- `spiral speed <curve>` → drives the `time` uniform's per-frame rate (Extension #1).
  **[Ext#1 has since shipped — see §4.4/§9; fully live, no stairstep.]**
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
     a *driver* (`zoom`/`fade`/`spiral speed`/`warp`/`drunk`), or a *state* op (`copy` —
     the only one that shipped; `set`/`roll` as author-facing keywords never did, see §4.7).
   - **THE RULE** — every numeric an effect takes is a **MODULATOR**: a literal, a
     `curve`, or a raw `[expr]`. Every modulator implicitly reads `this.progress` — the
     clock of the enclosing pattern — unless redirected with `over NAME` to an ancestor
     pattern's clock. (`drunk`/`warp` and `beat` are NOT modulator kinds that plug into
     another param — `drunk`/`warp` are standalone driver statements and a bare `beat`
     modulator was never built; see §4.13.)

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

4. **Name what cannot lower.** Two things required a runtime extension: a *continuous*
   curve-driven spiral speed, and the new `warp`/`drunk` wave shader. Both have SHIPPED
   (§9) — there is no remaining degraded/stairstep path for either. The one construct that
   still cannot lower today is text crossfade (Ext#4, §0.3) — text has one live slot, not a
   register.

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
`RepeatCycler` / `OffsetCycler` / `BurstCycler` (the last added post-ship for §4.11). Every
author-named node gets an `id` minted into a flat `NodeMap`; `render_eval`'s `resolve_ident`
reads any node by id and exposes six attributes (`progress/frame/length/position/index/
active`). Effects WRITE registers (`images` + int32 `scalars`) on cycler ticks; the flat
render block READS registers + node clocks every frame and calls one of **six**
`RenderStmt::Op`s (`Image, Text, Subtext, SmallText, Spiral, Warp` — `Warp` added post-ship
for §4.6). `Copy` (`images[dst]=images[src]`) already exists; `Effect::Kind` has **17**
members (`SpiralSet` added post-ship for §4.12) and no random-walk (the `Walk` kind in §4.5
never shipped); `RenderStmt::Op::Spiral` now carries a live `speed` `[expr]` field and
`render_spiral()` is called after `rotate_spiral(speed)` advances the phase — both were
"carries no numeric fields" / "takes no params" at the time this paragraph was first written,
before Extension #1 shipped (§9).

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
(image | word | caption | subtext)  <content> [-> REG]  <param-mod>*   # -> REG: image only
draw REG  <param-mod>*               # draw an existing image register without re-pulling
content ::= concept | reward | runtime   # the bi-thematic alternate bool, + runtime — see §8
```

- **Lowering.** Two halves: (1) a schedule `Effect{Image, slot}` writes
  `regs.images[REG] = get_image(alternate)` (`concept`→`Slot::Primary`,
  `reward`→`Slot::Alternate`, `runtime`→`Slot::Runtime`, resolved to primary/alternate at
  FIRE time rather than parse time); (2) a `RenderStmt{Op::Image, image_reg=REG}` whose
  `alpha`/`origin`/`zoom` fields are the lowered modulator strings. `draw` emits only
  the draw half (no pull), so any register (e.g. `prev`) is drawable. `word`/`caption`/
  `subtext` take the same `content` vocabulary but have no `-> REG` (text has one live slot,
  §0.3) — see §4.13/EBNF §11 for the full draw-statement shape across all four verbs.
- **Modder note.** Draws a picture. `-> REG` names the layer so `copy`/crossfade can reach
  it; omit it for an auto-named layer. **Bi-thematic is a hard floor:** `concept`/`reward`
  (plus `runtime`, which randomly rolls concept-vs-reward at each firing, `resolved_slot` in
  `compiled_visual.cpp`) are the entire content vocabulary; a third theme does not lower (§8).

### 4.3 `zoom` / `fade` / `origin` / `alpha`/`brightness` — CURVE-DRIVE PARAMS (the unified class)

```
param ::= ("zoom" | "origin" | "alpha" | "brightness") modulator
        | "fade" ("in" | "out" | "inout" | modulator)
```

**These are trailing params on a draw statement, not statements of their own** — `zoom`/
`origin`/`alpha`/`brightness`/`fade` only parse where `parse_params` is called, i.e. after
`image`/`draw`/`word`/`caption`/`subtext`. A bare `zoom (curve 0 -> 0.5)` on its own line
inside a pattern body is a parse error (`unknown statement 'zoom'`) — this corrects earlier
draft prose that implied a standalone drive-effect statement.

- **Lowering — pure `RenderStmt` param strings, NO runtime change.** Written on a draw
  (e.g. `image concept zoom (curve 0 -> 0.5) fade in`):
  - `zoom M`  ⇒ `RenderStmt.zoom  = lower(M)`
  - `origin M`⇒ `RenderStmt.origin= lower(M)`
  - `alpha M` / `brightness M` ⇒ `RenderStmt.alpha = lower(M)` (`brightness` is a literal
    alias for `alpha` in the parser — same field)
  - `fade in`    ⇒ `alpha = "this.progress"`
  - `fade out`   ⇒ `alpha = "1 - this.progress"`
  - `fade inout` ⇒ `alpha = "1 - abs(2*this.progress - 1)"`

  `this` substitutes to the enclosing pattern's minted id. Evaluated every frame by
  `render_eval`. (Note: drawn zoom is post-scaled by the program `zoom_intensity` setting,
  `api.cpp:308` — documented, not hidden.)
- **Modder note.** zoom, fade, brightness are the SAME thing: a number a curve moves over
  the pattern's life. `fade in/out/inout` are named curve shapes; write a raw curve for
  anything else.

### 4.4 `spiral` — DRAW + the spiral SPEED axis (standalone statement)

```
spiral [speed <mod>]
```

- **Lowering — DRAW half:** `RenderStmt{Op::Spiral}` (parameterless `render_spiral()`,
  unchanged). **SPEED half — Extension #1, SHIPPED, fully live (no stairstep):**
  `spiral speed M` ⇒ `RenderStmt{Op::Spiral, speed=lower(M)}`. Every frame `eval_render`
  calls `api.rotate_spiral(eval_num(speed))` immediately before `api.render_spiral()`
  (`render_eval.cpp`), so a curve reads exactly like zoom/fade/origin — no stairstep unroll,
  no `Effect::Kind::SpiralRot` leaf. (`SpiralRot` remains in the `Effect::Kind` enum for the
  legacy runtime shape but the v3 parser never emits it.) `spiral speed M over NAME` works
  like any other modulator's `over` — it is a live per-frame read, so the stairstep-vs-`over`
  incompatibility called out in earlier drafts no longer applies.
- **Shape/width/color are NOT modulators** — they are settings (§8), set via
  `look { spiral type=N width=W }` (§4.7).
- **Modder note.** Spiral SPEED is a curve just like zoom. Spiral SHAPE/COLOR are picked in
  the `look {}` settings header, not animated — the grammar only spins it.

### 4.5 [SUPERSEDED by §0.2 / §4.6] `drunk` as a random-walk register wander

> **SUPERSEDED.** This subsection describes the pre-ship design: `drunk` as an
> `Effect::Kind::Walk` register random-walk feeding an `origin`/`zoom` param modulator. That
> design was replaced before shipping by **§0.2: `drunk` is wave-warp sugar**, a `RenderStmt`
> shader param with no register/RNG/`Walk` effect at all. See §4.6 for what actually shipped.
> Kept below as implementation history (do not implement against this text).

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

### 4.6 `warp` / `drunk` — the wave-warp SHIPPED design (supersedes §4.5)

```
warp [amplitude <mod>] [wavelength <mod>] [speed <mod>]   # standalone statement
drunk <mod>                                                # sugar for `warp amplitude <mod>`
```

- **What it is.** A sinusoidal displacement of the sampled texture coordinate in the image
  fragment shader (`shaders.h`) — an "under-water" ripple, not a positional/origin jitter and
  not a register-driven random walk. `drunk` is pure sugar: `drunk M` ⇒ `warp amplitude M`
  with default `wavelength=0.15`, `speed=2` (the parser's literal defaults). There is no
  `drunk ... on origin|zoom` form and no `image ... origin (drunk ...)` param form — both
  belonged to the superseded §4.5 design and never shipped; `warp`/`drunk` are standalone
  pattern-body statements, not draw params.
- **Lowering — zero register, zero RNG, all three fields are live `[expr]` reads, exactly
  like zoom/fade/origin.** `warp`/`drunk` ⇒ `RenderStmt{Op::Warp, zoom=lower(amplitude),
  origin=lower(wavelength), speed=lower(speed)}` (the RenderStmt field names are reused —
  `zoom` carries amplitude, `origin` carries wavelength, `speed` carries speed; there is no
  separate `RenderStmt::Op::Warp`-specific field set). Each frame, `eval_render` calls
  `api.set_warp(amplitude, wavelength, speed)` once before any `Image` draws in the same
  block, so later `image`/`draw` statements in the pattern pick up the warp uniforms.
  Amplitude 0 (the default when `warp`/`drunk` is never written) ⇒ no displacement.
- **Modder note.** Makes the picture ripple like it's underwater. `amplitude` = how far it
  displaces; `wavelength` = ripple tightness; `speed` = ripple speed. `drunk <amount>` is the
  one-knob shorthand for "just make it wobble."

### 4.7 [SUPERSEDED / never shipped] `set` / `inc` / `roll` as user-facing statements

> **SUPERSEDED / never shipped.** No `set`, `inc`, or `roll` keyword exists in the parser's
> statement grammar (`parse_statement` recognizes `pattern`, `every`, `burst`, `look`,
> `warp`/`drunk`, `spiral`, `copy`, and the draw verbs only — see §11). What actually exists:
> **`copy`** (§4.8, the only user-visible state effect) and an **internal chance roll** — a
> `chance P` param on a draw (§4.9's `parse_chance`) compiles to an `Effect::Kind::Roll` the
> AUTHOR NEVER WRITES; the parser synthesizes it from `chance P` and prepends it before the
> gated draw. `Effect::Kind::Set`/`Inc`/`Toggle`/`Roll` remain real runtime ops (used
> internally by `chance` and `anim every Nth`'s pulse counter) but there is no grammar surface
> that lets an author write `set`/`inc`/`roll` directly. Kept below as implementation history.

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

### 4.8 `copy` — the one shipped STATE effect (crossfade handoff, author-visible)

```
copy SRC -> DST
```

- **Lowering.** `copy` ⇒ `Effect{Kind::Copy, target=DST, src=SRC}`
  (`regs.images[DST]=regs.images[SRC]`), both sides register names qualified per §0.6.
  Effects run in leaf order, so `copy` written before a draw's pull is the A→B→C handoff.
  No runtime change. This is the ONLY state-effect statement in the grammar — see §4.7 for
  what `set`/`inc`/`roll` would have been and why they never shipped.
- **Modder note.** `copy cur -> prev` stashes a layer so the next image can dissolve INTO it.
  This is the missing piece that lets crossfade be written by hand.

### 4.9 `every` / `loop` — CADENCE inside a pattern body (drives WHEN, not WHAT)

```
every <len> { <body> }
every <len> -> NAME { <body> }   # name the per-beat leaf so curves can ride its clock
loop N { <body> }
```

- **Lowering.** `every <N>f` ⇒ `RepeatCycler(count = span_len/N, child = Action(length=N))`.
  `-> NAME` mints the leaf's id into the `NodeMap` (closing the "atomic flash leaf is
  unnamed" gap — every per-beat clock is now name-addressable). `loop N` ⇒
  `RepeatCycler(count=N)`.
- **`every <curve>` (a bare curve modulator as the cadence length) was never shipped and is
  not what "ramped cadence" means today.** The shipped ramped cadence is the dedicated
  `every ramp A -> B steps N ...` form (§4.10) — a distinct piece of grammar with its own
  compile-time sampling, not a `curve` value plugged into `every`'s `<len>` slot. `<len>`
  only ever accepts `Nf | beats N | locked` (§4.1); `every <curve> { }` is a parse error
  (`expected a length`).
- **Modder note.** Repeats its body on a beat. Name it with `-> beat` so a zoom can ride
  that per-beat clock. For an accelerating/decelerating cadence, reach for `every ramp`
  (§4.10) instead of trying to feed a curve into `every`'s length.
- **`offset Nf`** (after the optional `-> NAME`) delays the lane's start by N frames
  (lowers to an `OffsetCycler` wrapper) — the staggered parallel-lane idiom, e.g.
  `super_parallel`'s three image layers a third of a 96f cycle apart:
  `every 96f offset 32f { image concept -> b alpha 0.5 zoom (curve 0 -> 0.875) }`.

### 4.10 `every ramp A -> B steps N [ease] [-> NAME]` — SHIPPED sampled ramp cadence

```
every ramp <N>f -> <N>f steps <N> [ease (linear | late | early)] [-> NAME] { <body> }
```

- **What it's for.** A cadence whose per-flash length itself accelerates/decelerates —
  e.g. cuts starting at 56f and rushing down to 12f over the pattern's life — without making
  cycler lengths dynamic at runtime (§13.3's non-goal). This is the real, shipped answer to
  the honest limit named in older drafts of §4.9 ("`every <curve>` reads whole-ramp progress,
  not the active segment") — it was resolved by sampling at PARSE time, not by adding a live
  ramped clock.
- **Lowering — compile-time sampled, then a plain `Seq` of fixed `Action` leaves (zero
  cycler/compiler change).**
  1. Sample `steps` integer segment durations from `A -> B` along the ease curve
     (`sample_ramp`), at segment midpoints `(i+0.5)/steps`, using the SAME ease formulas as
     the `curve` modulator (`linear` / `late` = progress³) so the shape matches what a
     continuous `curve A -> B` would render.
  2. Scale the raw samples so they sum to exactly the enclosing pattern's span before
     rounding, then fold the residual rounding error into the tail segments one frame at a
     time (walking backward) so no segment ever drops below 1f.
  3. Re-parse the body span once per sampled segment (seeking the cursor back to the body's
     opening `{`), each time pushing a fresh clock+register scope — this is what lets a bare
     (no-`over`) modulator inside the body ride "the currently active segment's clock" even
     though each segment is a distinct minted id.
  4. Each segment gets its own id: `-> NAME` mints `NAME_00`, `NAME_01`, ... (zero-padded
     2-digit suffix); with no `-> NAME` the segments are anonymous internal ids. This closes
     the older "ramped segments stay un-id'd" gap — every sampled segment IS individually
     addressable, and a body modulator's bare `this`/no-`over` correctly anchors to the
     ACTIVE segment (not whole-ramp progress) because each re-parse pushes that segment's own
     clock scope.
  5. Lowers to a `Seq` of `steps` `Action` leaves; a whole-pattern envelope (e.g. spiral speed
     accelerating smoothly across the ramp) still reads `over <enclosingPattern>` exactly like
     any other modulator — see EX-ACCELERATE below and §10.
- **Requires a bounded enclosing span** (`for <N>f`/`beats N`/`locked`, not `0`) and
  `span >= steps` (each segment needs at least 1f) — both are parse errors otherwise.
- **Modder note.** Use this when the FEEL should accelerate (or decelerate) smoothly across a
  pattern's life — the cut length itself is the thing sliding along a curve. Name the segment
  clock with `-> cut` (or similar) if you want per-cut modulators; otherwise it's fire-and-
  forget shape.

### 4.11 `burst [-> NAME] period Nf chance 1/K cooldown Nf duration Amin..Amax { base {} burst {} }` — SHIPPED

```
burst [-> NAME] period <N>f chance 1/<N> cooldown <N>f duration <N>f[..<N>f] {
  base  { <body> }
  enter { <body> }   # optional: fires ONCE at each burst's start
  burst { <body> }
}
```

- **What it's for.** Surfaces the existing `BurstCycler`: a base loop, randomly interrupted
  (roughly every `period` frames, `1/chance_den` odds per roll) by a bounded burst lasting
  `duration_min..duration_max` frames, then a `cooldown` before the next roll is eligible.
  This is the real, shipped answer to §13.1's "expose the existing burst/random cycler" —
  the felt "rapid-cut base, then it suddenly plays an animation for a bit, then settles back
  down" shape, built from the primitive instead of a baked FSM (`super_fast`'s
  `SuperFastTick` is retired; see §0.5).
  - `-> NAME` mints a stable node id so `NAME.index` (1 while the burst is active, else 0) is
    readable from any render `[expr]` in scope, via the existing `NodeMap`/`resolve_ident`
    path — no new plumbing.
  - There is **no separate `length` keyword** — unlike the illustrative §13.1 sketch, the
    burst's total length is the ENCLOSING pattern's span (like `every`'s implicit length; the
    author never restates it).
  - `chance 1/K` is written literally as `1/K` (the parser expects the `1` and the `/`
    verbatim, then a denominator — `chance 1/24`, not `chance 0.04` or `chance 1 / 24`
    with a bare fraction elsewhere).
  - `duration Af` (a single fixed duration) or `duration Af..Bf` (a min..max range) are both
    valid; `A..B` uses a literal `..` (two dots, no space required between them).
  - **`enter { }`** (optional) fires once at each burst's START, before that tick's burst
    action — one-shot setup like `anim runtime` (pick which animation this burst plays).
    Effects that live in the per-tick `burst { }` block instead re-fire every `period`.
  - **Draws are FSM-gated.** Render statements from the `base` block are additionally gated
    on `NAME.index == 0` and those from `burst`/`enter` on `NAME.index >= 1`, so the base's
    layers stop painting during a burst and vice versa. (Ungated, a burst-block
    `image ... anim` painted its animation over the base cuts for the WHOLE pattern.)
- **Lowering.** One `Node::Burst` (`n.burst_period/_chance_den/_cooldown/_dur_min/_dur_max`),
  `base { }`'s statements ⇒ `Node.effects`, `burst { }`'s statements ⇹ `Node.burst_effects`.
  Both blocks share the SAME enclosing pattern's clock scope — no separate clock/register
  scope of their own, so a bare modulator inside either block still rides `this` untouched.
  `base`/`burst` accept the same statement grammar as any cadence body (draws, `copy`, nested
  `pattern`/`every`). `pattern_compiler.cpp` compiles `Node::Burst` by synthesizing two tiny
  `Node`s (one per effect list) through the same `MakeAction` seam every other leaf uses, then
  wraps them in a `BurstCycler`.
- **Modder note.** Use this for "usually calm, occasionally spikes" — a rapid-cut or animated
  burst dropped into an otherwise steady loop, with a cooldown so it doesn't spike back-to-
  back. Name it `-> rapid` (or similar) if you want a render expr to know whether the burst is
  currently firing.

### 4.12 `look { }` / `chance` / `anim` — the other SETTINGS + lighter-randomness surfaces

```
look           ::= "look" "{" "spiral" ( "type" "=" <N> | "width" "=" <N> )* "}"
chance-param   ::= "chance" ( <FLOAT> | "(" <FLOAT> ")" )
anim-param     ::= "anim" [ "every" <N> ("st"|"nd"|"rd"|"th") ]
```

- **`look { spiral type=N width=W }`** — SETTINGS, fires once (not per-frame): a deterministic
  `Effect::Kind::SpiralSet` pinning spiral type (1 of 7) / arm-count width (Extension #3,
  §0.4, §9). This is the ONLY `look` property today — no other settings keys exist in the
  parser (`unknown look property` is a hard parse error for anything else).
- **`chance P`** — a trailing param on a draw statement (image/word/caption/subtext), NOT a
  standalone statement. Lowered to a synthesized `Effect::Kind::Roll` over a 100-bucket table
  (`P` clamped to 1..99 buckets) that the parser prepends before the gated draw's effect,
  plus a `Guard::Ge` on the draw itself — this is the "internal chance roll" referenced by
  §4.7's banner: the author never writes `roll` directly, `chance P` IS the surface.
- **`anim` / `anim every Nth`** — a trailing param on an `image` draw only. Bare `anim` always
  animates (`RenderStmt.has_anim = true`, plus an `Effect::Kind::Anim` load). `anim every
  Nth` additionally gates the animation behind a `Pulse` effect + counter, so the image only
  animates on every Nth firing (`anim_gate` on the `RenderStmt`); the ordinal suffix
  (`st`/`nd`/`rd`/`th`) is accepted but not checked against `N`.
- **Modder note.** These three are how the grammar answers "pin a static setting once"
  (`look`), "sometimes, not always" (`chance`), and "animate occasionally" (`anim every
  Nth`) without inventing a general conditional/probability language.

### 4.13 Modulators — the values that fill any param

```
modulator ::= literal | curve | rawexpr   [ over NAME ]
literal   ::= NUMBER
curve     ::= curve NUMBER -> NUMBER [ ease (linear | late | early) ]
rawexpr   ::= [ EXPR ]                  # this/self substituted
```

- **Lowering.** All compile to a render `[expr]` string read each frame:
  - `curve A -> B` ⇒ `"(A + (B-A) * <clock>.progress)"`
  - `<literal>`    ⇒ `"V"`
  - `[expr]`       ⇒ passthrough with `this`/`self` substituted
  - `over NAME`    ⇒ swaps `<clock>` from `this` (enclosing pattern id) to ancestor `NAME`'s id
- **`drunk` and bare `beat` are NOT modulator kinds** — `parse_modulator` accepts exactly
  three forms: `[expr]`, `curve A -> B [ease ...]`, and a numeric literal. `drunk` is a
  standalone statement (§4.6), not a value usable inside another param's parens; a bare
  `beat` modulator (an implicit `Repeat(length=locked_frames)` clock) was never built — ride
  the beat's own clock instead by naming a cadence leaf (`every beats 1 -> beat { ... }`,
  §4.9) and writing `over beat`, or use `beats N` in a `for`/`every` length (§4.1).
- **Only `linear`, `late` and `early` eases are implemented.** Any other ease word is a
  hard parse error (do not silently fall back to linear). `late` is cubic dwell at the
  START value (`p^3`); `early` is its mirror, rushing off the start and dwelling at the END
  value (`1 - (1-p)^3`) — for a once-per-sample ramp this is what reproduces the original
  accelerate's time-at-fast distribution (~25% of runtime at <=16f cuts).
- **No runtime curve object** — curves are compile-time string sugar (recon-confirmed).

### 4.14 `audio` — grammar-driven THEME audio (issue #23, SHIPPED)

```
audio <content> [loop] [volume <mod>]   # content ::= concept | reward | runtime
audio stop
```

- **What it is.** The showcase primitive for beats: `every beats N { audio mantra }` fires a
  precanned mantra/cue phase-locked to the entrainment bed, exactly the way `image`/`word`
  are phase-locked today. **No TTS, ever** — `audio` always plays a file from the theme's
  PRECANNED `audio_path` pool (`ThemeBank::get_audio`, `docs/audio.md`); the GRAMMAR only
  decides *when* and *how loud* to play it, never what it says. Themes own the audio pool
  exactly like the image/font pools; this primitive is what lets a modder reach it.
- **Two nouns still apply.** `content` is the SAME bi-thematic vocabulary as `image`/`word` —
  `concept` (primary theme), `reward` (alternate theme), `runtime` (rolled primary-vs-
  alternate at FIRE time, not parse time — identical to `image runtime`, §4.2). `volume` is
  an ORDINARY modulator (§4.13 above), not a bespoke keyword class: a literal, a
  `curve A -> B`, or a raw `[expr]`, riding the enclosing pattern's clock unless redirected
  with `over NAME`, same as zoom/fade/spiral-speed.
- **Lowering.**
  - `audio <content>` ⇒ `Effect{Kind::Audio, slot=lower(content)}`. At FIRE time (not parse
    time — same `resolved_slot` path every other bi-thematic effect uses), the compiled
    action resolves `content` to primary/alternate, pulls a path via the new
    `VisualControl::get_theme_audio(bool alternate)` accessor (mirrors `get_image` exactly —
    added alongside this primitive since the plumbing wave shipped the playback verbs but not
    a schedule-side pull accessor), and calls `VisualControl::play_theme_audio(path, loop)`.
  - `loop` ⇒ `Effect::force = true` (reusing the same field `Font`/`SmallSub` use for their
    force flags), passed straight through as `play_theme_audio`'s `loop` bool.
  - `audio stop` ⇒ `Effect{Kind::AudioStop}` → `VisualControl::stop_theme_audio()`. A
    dedicated `Kind` rather than a flag on `Audio`, so a bare `audio stop` never needs a
    (meaningless) content word — kept boring per the task's own framing.
  - `volume <mod>` — **folds at PARSE time based on modulator shape**, the same "constant vs.
    curve" split zoom/origin/alpha already make implicitly by being `[expr]` strings evaluated
    every frame regardless of shape; audio's volume instead explicitly forks because a
    per-frame `sf::Music::setVolume` call every frame forever is wasteful for the common case
    (a static mantra volume) and there is no visual harm in a fire-once set:
    - A bare numeric literal (`volume 0.6`) ⇒ `Effect::rate` (reusing the field
      `SpiralRot` uses for its rate — audio and spiral-rotation effects never coexist on one
      `Effect`), applied ONCE via `set_theme_audio_volume` right after `play_theme_audio` in
      the same firing. No render-side cost.
    - A `curve`/`[expr]` volume (`volume (curve 0.2 -> 0.8)`) ⇒
      `RenderStmt{Op::AudioVolume, speed=lower(mod)}` (reusing the `speed` field — same
      convention `Warp` reuses `zoom`/`origin`/`speed` for unrelated params, §4.6), evaluated
      and applied every frame by `render_eval`, exactly like `spiral speed`. Clamped to
      `[0,1]` before the call (`Audio::set_theme_audio_volume` clamps again on the engine
      side — belt and braces, not a double-source-of-truth).
    - No `volume` param at all ⇒ engine keeps whatever volume was last set (same "no explicit
      write, no change" default every other param has). The channel's INITIAL volume is
      **full (1.0)** — a bare `audio concept` is audible without any volume line. Written
      `volume 0` is a real mute (the parser distinguishes "absent" from "zero" via a
      rate sentinel; `pattern_ast.h` Effect::rate).
  - **`VisualRender` gained `set_theme_audio_volume` too** (dual-declared like
    `rotate_spiral`, §4.4) so `render_eval.cpp`'s render-only `VisualRender&` can reach it for
    the curve/`[expr]` branch above; `VisualApiImpl`'s single override satisfies both base
    pure virtuals, same shape as `rotate_spiral`.
- **Single-slot v0 (documented beside the text-slot limitation, §0.3 / `docs/audio.md`).** One
  live grammar-driven theme audio at a time — a second `audio` fire replaces whatever was
  already playing on the dedicated `_theme_audio_channel`; there is no queueing. `audio stop`
  is the only way to silence it early without starting a replacement.
- **Volume scale note.** `audio volume M` is `0..1` (matches `Audio::set_theme_audio_volume`'s
  signature), NOT the `0..100` scale `AudioEvent.volume` (playlist audio) uses — the same
  scale mismatch `docs/audio.md`'s plumbing section already flags; intentional, not a bug.
- **Modder note.** `audio mantra` (well, `audio concept`/`audio reward`) plays a spoken line
  from the theme's audio folder, timed by the SAME `every`/`beats`/`burst` cadence machinery
  that times a flash. `loop` keeps it going; `volume` fades it in/out like any other curve;
  `audio stop` cuts it. This is the whole reason theme audio pools exist: drop a folder of
  mantras next to your images and fonts, and the pattern grammar can trigger them the same way
  it triggers a flash.

---

## 5. The unified curve-drivable effect class (and the spiral split)

zoom, fade, origin, spiral-SPEED, and warp's amplitude/wavelength/speed are **one class**: *a
number driven by a modulator over a pattern's clock*, written `<param> <modulator> [over
NAME]`. The class is defined by lowering to `value · shape(clock.progress)` — all as live
`[expr]` reads, no register/RNG anywhere in this class (`drunk`'s pre-ship register-walk
design, §4.5, never shipped).

| Effect | Where it writes | Lowers today? |
|---|---|---|
| `zoom` | `RenderStmt.zoom` | **Yes**, live `[expr]`. |
| `fade`/`alpha`/`brightness` | `RenderStmt.alpha` | **Yes**, identical machinery. |
| `origin` | `RenderStmt.origin` | **Yes**, live `[expr]`. |
| `spiral speed` | `RenderStmt.speed` (`Op::Spiral`) | **Yes**, live `[expr]` — Extension #1 shipped, no stairstep. |
| `warp`/`drunk` amplitude/wavelength/speed | `RenderStmt.zoom`/`origin`/`speed` (`Op::Warp`) | **Yes**, live `[expr]`, all three params. |

**The asymmetry that remains is shape/width and color, not speed.** zoom/fade/origin/spiral
speed/warp are ALL live `[expr]` fields on `RenderStmt`, evaluated every frame — spiral speed
is no longer second-class; the stairstep-unroll design in earlier drafts of this section was
superseded once Extension #1 shipped (`spiral speed <curve> over NAME` is a normal live read,
not a static-sampling problem).

**The spiral types/color split (the named tension), resolved three ways:**

- **SPEED → GRAMMAR curve.** The only per-frame continuous spiral axis. Fully live (above).
- **TYPE (1 of 7) / WIDTH → SETTINGS selector** (`look { spiral type=N width=W }`,
  §4.12). These are discrete identity chosen once; the pre-v3 runtime is *random-only* via
  `change_spiral` (which no-ops 25% of the time). v3 ships a deterministic `SpiralSet` setter
  (`compiled_visual.cpp`, §0.4) so the author can *select* a shape — but never *curve* it,
  because a shape is not a continuum.
- **COLOR / DIRECTION → SETTINGS** (per-Program proto: `spiral_colour_a/b`,
  `reverse_spiral_direction`). Unreachable from grammar and kept that way; grammar-driven
  color would require per-frame color uniforms (a real extension) and crosses the
  pattern/session boundary. Explicitly declined.

So "make spiral first-class like zoom" is **TRUE for speed (fully live), PARTLY-true for
shape/width (selector, not curve), FALSE for color.** The grammar reflects this asymmetry
instead of pretending spiral is uniform.

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
    every beats 1 -> beat {             # per-beat handoff leaf, id = beat (`every LEN`, no parens)
      copy cur -> prev                  # stash last beat's image (Effect::Copy)
      draw prev          zoom (curve 0.5 -> 1.0)
      image concept -> cur fade in zoom (curve 0.0 -> 0.5)
    }
  }
}
```

**Lowering, all to existing primitives:**

- The `every beats 1 -> beat { copy; draw; image }` body lowers to a `RepeatCycler` whose
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

**No `crossfade` keyword and no `--expand` macro shipped.** This subsection's closing line in
earlier drafts described a planned `crossfade`/`pulse` macro system (Appendix B Phase 4)
whose `--expand` output would print exactly the primitives above. That macro layer was never
built — grep confirms no `crossfade`/`expand` token anywhere in `pattern_parser_v3.cpp`.
Every v3 built-in (including the shipped `flash_text`, §10 EX6) writes the copy/draw/image
primitives out longhand each time it needs a crossfade; there is no shorthand. If a macro
layer is ever added, its expansion should still BE the lowering path (never a parallel
re-implementation) — that design constraint remains sound even though nothing implements it.

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
holds the full pattern-id and register declaration list — verifies, at parse time:

1. **Every `over NAME` names a real enclosing pattern/clock id** — `resolve_clock` walks the
   live clock-scope stack and throws a `ParseError` (hard failure) if `NAME` isn't found.
2. **Every register reference resolves.** `qualify_reg` hard-errors immediately (`ParseError`)
   on a bare name with no enclosing pattern, or a qualified `Other.reg` naming a pattern not
   in scope. Separately, `check_register_resolution` (run once at the end of `parse()`) walks
   every register READ (`copy`'s src, `draw REG`) against the set of registers ever WRITTEN
   (`image ... -> REG`, `copy`'s dst) and emits a **warning** (not a hard parse error) for a
   read with no matching write — that register would draw as an empty image.
3. **Not implemented: a dissolve-pair anchoring check.** An earlier draft of this section
   described a compile-time warning for "a `prev`/`cur` zoom pair anchored to different-length
   clocks." That check does not exist in the shipped parser — there is no dissolve-pair
   detection of any kind (register names like `cur`/`prev` are pure author convention, never
   typed by the compiler). Getting the two zoom halves' clocks right (§6's worked example) is
   on the author.

A resolution failure (#1/#2's hard-error path) is a parse error, never a silent dark screen.
This is the single most important safety property for non-programmer authors; #2's
never-written-register case is a warning rather than a hard error because a register that is
merely unused-so-far is a common, harmless authoring stage (e.g. a `draw prev` on the very
first loop iteration, before any `copy` has run).

### 7.5 Honest carry-forward limits

- `OffsetCycler` pre-rolls its child silently with `trigger_actions=false` and phase-shifts
  its clock; any `over` read of an offset ancestor inherits this shift.
- `ParallelCycler` length = LCM of children; co-prime-length parallel children blow up the
  parent clock and make `over PARENT` sweep a surprising period.
- **Resolved (was an open gap in earlier drafts): ramped-cadence per-segment leaves ARE
  id'd.** `every ramp A -> B steps N -> NAME` (§4.10) mints a per-segment id (`NAME_00`,
  `NAME_01`, ...) for every sampled segment, so per-flash wobble inside an accelerate IS
  authorable via that segment's own bare-modulator clock. The remaining honest limit is
  narrower: an UNNAMED ramp segment (no `-> NAME`) still gets an anonymous internal id that
  isn't author-addressable by name, and a modulator that wants to read a SPECIFIC OTHER
  segment's clock (not "the currently active one") has no syntax to name one out of the
  zero-padded `NAME_00.. NAME_(N-1)` set without already knowing `N`.
- Bi-thematic is a hard floor (§8); nested patterns cannot each carry their own theme.

---

## 8. Grammar-vs-settings decision table

**Rule:** GRAMMAR owns time-varying scalars (a curve can move them per frame); SETTINGS own
static identity/palette (chosen once, constant for the run). The grammar's ONE settings
surface is the per-(sub)pattern `look { ... }` header (§4.12) — it fires once (not a
per-frame `[expr]`) and lowers to a deterministic runtime setter (`api.set_spiral`, a member
field on `VisualApiImpl`, NOT a `.session` proto field). The genuinely proto-backed settings
below (`zoom_intensity`, `spiral_colour_a/b`, `reverse_spiral_direction`, `global_fps`, ...)
are session/program identity, unreachable from any grammar surface — `look {}` only reaches
spiral type/width.

| Knob | Tier | Where | Why |
|---|---|---|---|
| image/text `alpha` (fade) | GRAMMAR curve | `RenderStmt.alpha` | per-frame `[expr]` |
| `zoom` amount | GRAMMAR curve | `RenderStmt.zoom` | per-frame `[expr]` |
| `origin` (zoom pivot) | GRAMMAR curve | `RenderStmt.origin` | per-frame `[expr]` |
| `zoom_intensity` ceiling | SETTING | Program proto | master multiplier, constant for run |
| spiral **SPEED** | GRAMMAR curve | `RenderStmt.speed` (`Op::Spiral`), live | continuous per-frame scalar |
| spiral **TYPE** (1 of 7) | SETTING selector | `look { spiral type=N }` (shipped, `SpiralSet`) | discrete identity, no continuum |
| spiral **WIDTH** | SETTING selector | `look { spiral width=W }` (shipped, `SpiralSet`) | discrete identity |
| spiral **COLOR** a/b | SETTING | Program proto | blended shader uniforms; session concern |
| spiral **DIRECTION** | SETTING | Program proto | global sign; grammar `speed` is magnitude |
| `warp`/`drunk` amplitude/wavelength/speed | GRAMMAR curve | `RenderStmt.zoom/origin/speed` (`Op::Warp`), live | per-frame scalar, shipped (§4.6, supersedes §4.5's register-walk design) |
| content theme (concept/reward) | GRAMMAR selector | the alternate bool | only content axis; 3+ themes do NOT lower |
| theme audio **content** (concept/reward/runtime) | GRAMMAR selector | `Effect::Kind::Audio` slot (§4.14) | same alternate-bool axis as `image`, resolved at fire time |
| theme audio **volume** | GRAMMAR curve OR fire-once | `RenderStmt.speed` (`Op::AudioVolume`) or `Effect.rate` (§4.14) | literal folds to fire-once; curve/`[expr]` rides per-frame like spiral speed |
| theme audio **pool contents** (which files) | SETTING | `Theme.audio_path` (proto, `docs/audio.md`) | which files exist is theme identity, not a grammar concern |
| `global_fps`, font, theme weights | SETTING | Program/Theme proto | session/program identity |

**Direction note.** A signed `speed` modulator interacts with `reverse_spiral_direction`;
grammar `speed` is **magnitude**, the setting owns global **sign** — documented to avoid
double-control.

---

## 9. Required runtime extensions (named, ranked by cost) — ALL SHIPPED

Everything in §4–§8 lowers with **zero runtime change** except the extensions below, all of
which have now shipped (this section is retained to name what each one cost and where it
landed, not as an open TODO list).

1. **Continuous spiral speed — SHIPPED.** `RenderStmt::Op::Spiral` carries a `speed` `[expr]`
   field; `eval_render` calls `api.rotate_spiral(speed)` each frame before `render_spiral()`
   (`render_eval.cpp`). No stairstep path exists in the v3 parser — `spiral speed <curve>`
   (with or without `over NAME`) is always a live per-frame read (§4.4, §5).
   - *1b (still declined for v3):* a true positional/rotation warp distinct from the shipped
     wave-warp (§4.6) — an offset/rotation `RenderStmt` param + vertex-shader uniform — remains
     out of scope. `warp`/`drunk` is the sinusoidal-displacement shape only.

2. **The warp shader (shipped as `warp`/`drunk`, §4.6) — NOT the `Effect::Kind::Walk`
   register-walk this section originally specced (§4.5, superseded).** The shipped design
   needed zero new `Effect::Kind` and zero new register: `RenderStmt::Op::Warp` carries three
   live `[expr]` fields (amplitude/wavelength/speed, reusing the `zoom`/`origin`/`speed`
   field names) read by `eval_render`'s `api.set_warp(...)` each frame, plus shader-side
   uniform plumbing (`shaders.h`, `api.cpp`/`render.cpp`) and a per-frame `warp_time`. This
   extension turned out cheaper than originally scoped BECAUSE the design changed (§0.2), not
   because the original `Walk`-register design got optimized down — that design was dropped
   entirely.

3. **Deterministic spiral selector (`SpiralSet`) — SHIPPED.** `compiled_visual.cpp` runs
   `Effect::Kind::SpiralSet` as `api.set_spiral(type, width)`, so `look { spiral type=N
   width=W }` (§4.12) pins type/width instead of re-rolling via `change_spiral`. No
   degradation path was needed in the end — it shipped alongside the grammar surface that
   uses it.

4. **Grammar-driven theme audio (`Audio`/`AudioStop`/`AudioVolume`) — SHIPPED, §4.14, issue
   #23.** Two new `Effect::Kind` members (`Audio`, `AudioStop`) plus one new `RenderStmt::Op`
   (`AudioVolume`). The underlying engine bridge (`Audio::play_theme_audio` / `stop_theme_audio`
   / `set_theme_audio_volume`, the dedicated `_theme_audio_channel`, `VisualControl`'s three
   audio verbs, `ThemeBank::get_audio`) had already shipped in a prior plumbing wave (this
   grammar wave only added `VisualControl::get_theme_audio` — the schedule-side pull accessor
   that was the one missing piece — and `VisualRender::set_theme_audio_volume`, dual-declared
   like `rotate_spiral` so the per-frame curve path can reach it from `render_eval.cpp`); this
   extension is almost entirely schedule+lowering, not new engine capability.

**Out of scope / hard non-goals (still true):** grammar-driven spiral color (per-frame color
uniform); 3+ simultaneous themes (two live theme slots, VRAM budget); live phase-accurate beat
(`every locked`/`beats N` use a compile-time period, not a live audio clock; see
`docs/authoring-v3-patterns.md`); a text-content crossfade register (Ext#4, §0.3, still
deferred — the only genuinely open extension left).

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
`spiral speed` ⇒ `RenderStmt{Op::Spiral, speed="(0.1+0.9*unified.progress)"}`, read live every
frame (Extension #1 is shipped — no stairstep path exists). All three are the same shape: a
modulator over `unified.progress`. The body is parallel, so all three co-run.

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

### EX5 — `drunk` (warp-sugar); amplitude ramps up

```
pattern stagger for 300f {
  image concept zoom 0.5
  drunk (curve 0 -> 0.3)
}
```
`drunk` is a standalone statement — sugar for `warp amplitude (curve 0 -> 0.3)` with the
default wavelength/speed (§4.6). It is NOT a param modulator nested inside `origin`/`zoom`
(that §4.5-era shape never shipped — see §4.5's banner). Lowers to
`RenderStmt{Op::Warp, zoom="(0 + 0.3*stagger.progress)", origin="0.15", speed="2"}`: the
image fragment shader displaces the sampled texture coordinate by a sinusoid whose amplitude
ramps from 0 to 0.3 over the pattern's 300f life. No register, no `Effect::Kind::Walk` — see
§0.2.

### EX6 — re-authored `flash_text` (replaces the v1/v2 baked preset)

```
pattern flash_text for beats 16 loop 16 {
  look { spiral type=3 width=6 }        # SHAPE/COLOR are settings, pinned once
  spiral speed 0.4                       # constant spin (grammar magnitude)
  pattern life for beats 2 {
    every beats 1 -> beat {
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

### EX7 — `every ramp` accelerating cadence (the shipped `accelerate` built-in, §4.10)

```
pattern accelerate for 2772f {
  every ramp 56f -> 12f steps 120 ease early -> cut {
    image concept zoom (curve 0 -> 0.5)
    word concept chance 0.5
  }
  spiral speed (curve 1 -> 4 over accelerate)
}
```
`ease early` (cubic, `1-(1-p)^3`) rushes the cut length off the slow end and dwells at the
fast end — with once-per-sample segments this reproduces the original's `1 + d^6/56^5` repeat
curve within a point or two (~26% of runtime at ≤16f cuts; `ease late` here was the shipped
regression: 46 mostly-slow cuts and ~3% at fast). Each cut is its own minted id
(`cut_000`..); the image's `zoom (curve 0 -> 0.5)` — a bare modulator inside the ramp body —
rides the ACTIVE cut's own clock, so every image zooms over its own on-screen life at the
current cut rate. `spiral speed` reads `over accelerate` — the WHOLE pattern's parent clock,
not any one segment — so the spin accelerates smoothly across the entire span independent of
the ramp's per-segment granularity. This is the real, shipped version of the pre-ship §13.2
sketch.

### EX8 — `burst` random-interrupt cadence (the shipped `super_fast` built-in, §4.11)

```
pattern super_fast for 2048f {
  burst -> rapid period 8f chance 1/12 cooldown 64f duration 64f..128f {
    base  { image runtime zoom 0.15 }
    enter { anim runtime }
    burst { draw cur zoom 0.4 anim }
  }
  every 8f { word concept chance 0.25 }
  spiral speed 3
}
```
`base` cuts a still image every 8f; roughly every 8f there's a 1-in-12 roll to enter a
64f..128f burst, then a 64f cooldown before the next roll is eligible. On entry the `enter`
block picks the burst's animation ONCE (`anim runtime` — a standalone anim statement, no
image pull); the per-tick `burst` block then just renders it (`draw cur ... anim`, a pure
render). Base and burst draws are FSM-gated on `rapid.index` so exactly one side paints at
any frame — an ungated always-anim burst draw painted one animation over the whole pattern
(the "no cuts at all" regression). This is the real, shipped version of the pre-ship §13.1
sketch — note the shipped syntax drops the sketch's `length 2048f` (redundant with the
enclosing pattern's span) and moves `-> rapid` to right after `burst` (matching `every`'s
`-> NAME` placement).

### EX9 — `audio` phase-locked to the entrainment bed (issue #23, THE beats showcase)

```
pattern mantra_pulse for beats 16 {
  every beats 4 { audio concept loop volume (curve 0.2 -> 0.8) }
  every beats 1 { image concept zoom (curve 0 -> 0.4) }
  spiral speed 2
}
```

With an 8 Hz pulsed bed at 60 fps (`locked_period_frames = 8`, §"Beats" in
`docs/authoring-v3-patterns.md`), this pattern runs 16*8 = 128 frames and re-fires `audio
concept` every 4 beats (32f) — a fresh precanned mantra line pulled from the primary theme's
`audio_path` pool each time, looping, its volume ramping `0.2 -> 0.8` across each 32f window
(a curve, so it lowers to a per-frame `RenderStmt{Op::AudioVolume}` — see §4.14). Meanwhile
`every beats 1` flashes an image once per pulse, independent of the mantra cadence, so the
visual beat and the (four-times-slower) mantra beat both lock to the SAME entrainment bed
without one driving the other. Single-slot v0 means the SECOND `audio concept` firing (at
frame 32) replaces the first line outright — no crossfade/queue for audio, unlike the image
crossfade shape (EX4). Swap `audio concept` for `audio stop` in a later phase to cut the
mantra early without waiting for the pattern to end.

---

## 11. Full EBNF

Regenerated directly from `pattern_parser_v3.cpp` (read end to end for this pass — every
production below traces to a specific `parse_*` function). This is the CURRENT normative
grammar; where any prose in §0–§10 or §13 disagrees with this section, this section (backed
by the parser) wins.

```
(* ---- top level (Parser::parse / parse_pattern) ---- *)
pattern        ::= "pattern" NAME "for" len [ arrangement ]* "{" body "}"
arrangement    ::= "seq" | "loop" NUMBER
len            ::= UINT "f" | "beats" UINT | "locked"    (* beats/locked hard-error if the
                                                              program has no pulsed bed *)
body           ::= stmt*
stmt           ::= pattern | cadence | burst | look
                 | warp_stmt | spiral_stmt | copy_stmt | draw_stmt | audio_stmt | anim_stmt

anim_stmt      ::= "anim" content
                    (* standalone: switch WHICH animation the streamer plays (an
                       Effect::Kind::Anim), no image pull, nothing drawn. One-shot setup --
                       e.g. a burst `enter { anim runtime }` picks the burst's animation
                       once; a `draw REG ... anim` then renders it. *)

(* ---- look{} settings header (parse_look) — the ONLY settings surface ---- *)
look           ::= "look" "{" look_prop* "}"
look_prop      ::= "spiral" ( "type" "=" UINT )? ( "width" "=" UINT )?   (* either/both, any order *)

(* ---- cadence (parse_cadence / parse_ramp_cadence) ---- *)
cadence        ::= "every" len [ "->" NAME ] [ "offset" len ] "{" body "}"
                 | "every" ramp_len "{" body "}"
                    (* `offset Nf` delays the lane's start: an OffsetCycler wrapper *)

ramp_len       ::= "ramp" FLOAT "f" "-" ">" FLOAT "f"
                    "steps" UINT [ "ease" easeword ] [ "-" ">" NAME ]
                    (* SHIPPED, §4.10. Requires a bounded enclosing span, span >= steps.
                       Compile-time sampled: lowers to a Seq of `steps` fixed-length Action
                       leaves, NOT a live/dynamic cycler length. `-> NAME` mints per-segment
                       ids NAME_00 .. NAME_(steps-1). *)

(* ---- burst (parse_burst) — SHIPPED, §4.11 ---- *)
burst          ::= "burst" [ "->" NAME ]
                    "period" UINT "f"
                    [ "chance" "1" "/" UINT ]
                    [ "cooldown" UINT "f" ]
                    [ "duration" UINT "f" [ ".." UINT "f" ] ]
                    "{" burst_block* "}"
                    (* `period` is mandatory; the other three are optional and may appear in
                       any order (a for(;;) loop over peeked keywords). No `length` keyword —
                       length is always the enclosing pattern's span. *)
burst_block    ::= ( "base" | "burst" | "enter" ) "{" body "}"
                    (* same statement grammar as any body; all blocks share the enclosing
                       pattern's clock/register scope. `enter` effects fire once at burst
                       start. Draws are FSM-gated: base => NAME.index == 0, burst/enter =>
                       NAME.index >= 1. *)

(* ---- warp/drunk (parse_statement, kw=="warp"||"drunk") — SHIPPED, §4.6, supersedes §4.5 ---- *)
warp_stmt      ::= "warp" ( "amplitude" modulator | "wavelength" modulator
                           | "speed" modulator )*
                 | "drunk" modulator                (* sugar: == warp amplitude <mod>, with
                                                          default wavelength=0.15 speed=2 *)

(* ---- spiral (parse_statement, kw=="spiral") ---- *)
spiral_stmt    ::= "spiral" [ "speed" modulator ]    (* live [expr], no stairstep *)

(* ---- copy (parse_statement, kw=="copy") — the only state_effect ---- *)
copy_stmt      ::= "copy" REG "-" ">" REG

(* ---- audio (parse_audio, kw=="audio") — SHIPPED, §4.14, issue #23. PRECANNED only, no TTS. *)
audio_stmt     ::= "audio" content [ "loop" ] [ "volume" modulator ]
                 | "audio" "stop"
                    (* content resolved at FIRE time via VisualControl::get_theme_audio, same
                       as an image draw's content; `loop` -> Effect::force; a literal `volume`
                       folds to a fire-once Effect::rate, a curve/[expr] volume instead emits a
                       RenderStmt{Op::AudioVolume} evaluated every frame like spiral speed. *)

(* ---- draws (parse_draw) ---- *)
draw_stmt      ::= "image" content [ "-" ">" REG ] draw_param* anim_param? chance_param?
                 | ( "word" | "caption" | "subtext" ) content draw_param* chance_param?
                 | "draw" REG draw_param* [ "anim" ]
                    (* trailing `anim` on `draw`: render the animation stream layer instead
                       of the still -- pure render, no change-animation effect (pair with a
                       standalone anim_stmt to pick which); no `every Nth` form here. *)
content        ::= "concept" | "reward" | "runtime"   (* Slot::Primary / Alternate / Runtime;
                                                            "runtime" resolves the theme at
                                                            fire time, not parse time *)

draw_param     ::= ( "zoom" | "origin" | "alpha" | "brightness" ) modulator
                 | "fade" ( "in" | "out" | "inout" | modulator )
                    (* NOTE: draw_param is parsed in a loop (parse_params) so any number of
                       these may repeat/mix in one statement; last writer for a given
                       zoom/origin/alpha field wins. `brightness` is a literal alias for
                       `alpha` — same RenderStmt field. *)
anim_param     ::= "anim" [ "every" UINT ( "st" | "nd" | "rd" | "th" ) ]
                    (* "image" draws ONLY — word/caption/subtext/draw do not accept `anim`. *)
chance_param   ::= "chance" ( FLOAT | "(" FLOAT ")" )
                    (* synthesizes an internal Roll effect (100-bucket table, clamped 1..99)
                       + a Guard::Ge on the draw; the author never writes `roll` directly. *)

(* ---- modulators (parse_modulator / parse_over) ---- *)
modulator      ::= [ "(" ] ( literal | curve | rawexpr ) [ "over" NAME ] [ ")" ]
literal        ::= FLOAT                              (* a bare number: a CONSTANT, no clock *)
curve          ::= "curve" FLOAT "-" ">" FLOAT [ "ease" easeword ]
rawexpr        ::= "[" EXPR "]"                        (* this/self substituted with the
                                                            resolved clock id *)
easeword       ::= "linear" | "late" | "early"         (* any other word is a hard parse error;
                                                            late = p^3, early = 1-(1-p)^3 *)
EXPR           ::= (* render_eval grammar, evaluated live each frame: ternary ?:, and/or,
                      compare (== != < > <= >=), arithmetic (+ - * / % ^, unary - and !),
                      min/max/abs, identifiers as <node-id>.(progress|frame|length|position|
                      index|active) or a bare scalar register name *)

REG            ::= NAME [ "." NAME ]     (* bare = qualified to the nearest enclosing pattern's
                                             register scope; "Other.name" reads another
                                             pattern's register by its declared name *)
```

**Surfaces this EBNF intentionally does NOT include (never shipped — see the relevant §4.x
banner for why):** `set REG N`, `inc REG N`, `roll REG (N, N, ...)` as user-writable statements
(§4.7) — `Roll` exists only as the parser-internal effect `chance P` synthesizes, never an
author-facing keyword; a bare `beat` modulator kind (§4.13); `drunk <mod> on origin|zoom` and
`image ... origin (drunk <mod>)` (§4.5, superseded by §4.6's `warp`/`drunk`); `every <curve>`
as a bare-curve cadence length (§4.9) — the shipped ramped cadence is the dedicated `every
ramp A -> B steps N` form above, not a `curve` value in `<len>`'s slot; standalone
`zoom`/`origin`/`alpha`/`fade` statements outside a draw (§4.3) — these are `draw_param`
only, never top-level `stmt`s.

---

## 12. Open questions

See `open_questions` in the accompanying plan (historical); most of these have since been
settled by shipping: the drunk clamp-bias question is moot (drunk shipped as warp-sugar, §4.6
— no register/clamp/RNG at all, §4.5's design was dropped rather than tuned), and spiral
type/width stayed a `look {}` selector (§4.12) rather than being promoted to a proto field.
Genuinely still open: whether `beats`/`locked` should remain a compile-time period or wait
for a live audio clock (§9's non-goals; see `docs/authoring-v3-patterns.md` for the current
compile-time semantics).

---

## 13. V3 enhancements to consider next

13.1 and 13.2 below were originally written as **not-yet-shipped** proposals. Both have since
**SHIPPED** — the normative syntax/lowering now lives in §4.11 (burst) and §4.10 (ramp
cadence) respectively; this section is kept as two short stubs pointing there (history: what
motivated each feature) rather than duplicating the spec. 13.3 remains a live, unshipped
design constraint.

### 13.1 [SHIPPED — see §4.11] Expose the existing burst/random cycler in v3

The runtime already had `BurstCycler` and the legacy (now-deleted) v1 grammar could parse
`burst { ... }`; v3 originally only exposed lighter randomness (`chance P`, `anim every Nth`,
`runtime` slots — all three still true and still documented at §4.12) and left the
short-random-burst-with-cooldown shape unreachable. That gap is now closed: `burst [->
NAME] period ... chance ... cooldown ... duration ... { base {} burst {} }` lowers to exactly
one `Node::Burst`, and the shipped `super_fast` built-in uses it (§4.11, EX8). The exact
syntax that shipped differs slightly from this subsection's original illustrative sketch (no
`length` keyword, `-> NAME` placement) — §4.11 is the accurate one.

### 13.2 [SHIPPED — see §4.10] Parent-clock envelopes for accelerating cadence

The pre-ship `ACCELERATE` reauthor (three fixed phases) lost the true accelerating-cadence
feel of the original hardcoded pattern. The fix that shipped is exactly the "compile-time
sampled ramp expansion" this subsection proposed: `every ramp A -> B steps N [ease ...] ->
NAME { body }` samples `N` integer segment durations from the curve at PARSE time (not a live
runtime length — cycler lengths stay structural, per this subsection's original caution about
`ParallelCycler` LCMs / `SequenceCycler` sums / `RepeatCycler` indexing), lowers to a `Seq` of
fixed `Action` leaves, and mints a per-segment id (`NAME_00`, `NAME_01`, ...) rather than one
shared `cut` id — a refinement on the original sketch that gives every segment (not just "the
current one") a stable address. Whole-pattern envelopes still ride `over <pattern>` exactly as
proposed (§4.10, EX7's `spiral speed (curve 2 -> 6 over accelerate)`). This recovered
hardcoded `ACCELERATE`'s capability without making pattern lengths dynamic or adding a general
algebra language to `for` — the shipped design honors this subsection's original caution.

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
