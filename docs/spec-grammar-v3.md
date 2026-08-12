# Trance Visual Grammar v3 — Design Specification

> Status: **SHIPPED, v3-only.** `pattern_parser_v3.{h,cpp}` + `builtin_patterns_v3.cpp` is now
> the ONLY grammar/parser — the v1 legacy parser (`pattern_parser.{h,cpp}`) has been retired and
> deleted this sprint, and the intermediate v2 was retired before it. There is no fallback
> parser; playback is v3-only. This document defines the v3 surface and its exact lowering to
> the cycler + effect + render-block runtime. The runtime extensions it needed are built:
> curve-driven spiral speed, the `SpiralSet` selector, the wave warp shader, and (landed after
> the original §9 estimate) the sampled ramp cadence and the `burst` surface (§13 is no longer
> speculative — both shipped; see §4.10/§4.11). A later wave (issue #42) added four
> **parser-only** vocabulary extensions — `show` (§4.15), `env` (§4.16), `line` (§4.17) and
> `alternate` (§4.18) — which added nothing to §9: they reach `RenderStmt.when`/`alpha`,
> `Effect::split` and `Effect::slot_reg`+`Toggle`, all of which already shipped and already ran
> every frame. Their motivation is the eight-visual intent audit in
> `docs/intent-screenplays.md`. The **one deferred** piece is a text-content
> register so text can crossfade like images (Ext#4) — the text path is a single live slot, not
> a register. Where this spec and any older prose disagree, **the parser and runtime enums are
> the source of truth** (`pattern_ast.h`, `pattern_parser_v3.cpp`, `render_eval.cpp`, `api.cpp`).
> §0 (locked decisions) governs the rest; §11's EBNF is regenerated directly from the parser and
> is the current normative grammar. Honest-limits tone is kept throughout; §12 and Appendix A
> record what is still open, not a task list of what shipped.

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
dead-zone gaps the pre-ship review raised, since time comes from the frame clock, though `warp_speed` still
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
  silent fallback). **[Ext#3 has since shipped — see §9/§4.12;
  `look { spiral type=N }` lowers today, it no longer hard-errors.]**
- `spiral speed <curve>` → drives the `time` uniform's per-frame rate (Extension #1).
  **[Ext#1 has since shipped — see §4.4/§9; fully live, no stairstep.]**
- `acolour`/`bcolour`/direction remain Program-proto settings, walled off from the grammar.

**0.5 `super_fast`: commit to randomness primitives; delete `SuperFastTick`.** No `raw {}`
escape hatch. Accept "same effect, not the same frames." Closes that §12 open question.

**0.6 Register scoping — lexical, pattern-scoped, compile-time qualified (resolves the
review's flagged collision risk).** A register name is **local to its nearest enclosing `pattern`**; cadence blocks
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
   manual (`image primary zoom (curve 0 -> 0.5)` = "draw primary, zoom from 0 to 0.5 over
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
content ::= primary | secondary | runtime   # the bi-thematic alternate bool, + runtime — see §8
```

- **Lowering.** Two halves: (1) a schedule `Effect{Image, slot}` writes
  `regs.images[REG] = get_image(alternate)` (`primary`→`Slot::Primary`,
  `secondary`→`Slot::Secondary`, `runtime`→`Slot::Runtime`, resolved to primary/secondary at
  FIRE time rather than parse time); (2) a `RenderStmt{Op::Image, image_reg=REG}` whose
  `alpha`/`origin`/`zoom` fields are the lowered modulator strings. `draw` emits only
  the draw half (no pull), so any register (e.g. `prev`) is drawable. `word`/`caption`/
  `subtext` take the same `content` vocabulary but have no `-> REG` (text has one live slot,
  §0.3) — see §4.13/EBNF §11 for the full draw-statement shape across all four verbs.
- **Modder note.** Draws a picture. `-> REG` names the layer so `copy`/crossfade can reach
  it; omit it for an auto-named layer. **Bi-thematic is a hard floor:** `primary`/`secondary`
  (plus `runtime`, which randomly rolls primary-vs-secondary at each firing, `resolved_slot` in
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
  (e.g. `image primary zoom (curve 0 -> 0.5) fade in`):
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
  no `Effect::Kind::SpiralRot` leaf. (`SpiralRot` has since been deleted from the
  `Effect::Kind` enum outright -- nothing emitted it.) `spiral speed M over NAME` works
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
image primary origin (drunk <intensity-mod>) # used as a param modulator
```

- **What it morphs.** By default `origin` (the zoom pivot, a wandering off-center stagger),
  optionally `zoom` (a breathing wobble). It does **not** warp geometry — the painter's
  palette is `alpha/origin/zoom` only; a true positional/rotation warp is named but
  declined (§9, Extension #1b, out of scope for v3).
- **How intensity works.** The intensity is itself a modulator
  (`drunk (curve 0 -> 0.4)` ramps drunkenness up), scaling the per-tick random step exactly
  as `spiral speed` scales rotation. Intensity 0 = still; larger = wilder.
- **Lowering — requires §9 Extension #2 (one new `Effect::Kind::Walk`).**
  1. **State.** A scalar register `W`, stored in fixed-point **milli-units** (registers are
     `int32`), zero-centered and clamped.
  2. **Walk effect.** A per-frame `Action` leaf (length 1) inside the pattern fires
     `Effect{Kind::Walk, target=W, rate=intensity}`. One new `run_effect` case (beside
     `Set`/`Roll`):
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
> gated draw. `Effect::Kind::Set`/`Toggle`/`Roll`/`Pulse` remain real runtime ops (used
> internally by `chance` and `anim every Nth`'s pulse counter; `Inc` never had an emitter and
> has since been deleted) but there is no grammar surface
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
  `every 96f offset 32f { image primary -> b alpha 0.5 zoom (curve 0 -> 0.875) }`.

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
rawexpr   ::= [ EXPR ]                  # this/self + in-scope clock names substituted
```

- **Lowering.** All compile to a render `[expr]` string read each frame:
  - `curve A -> B` ⇒ `"(A + (B-A) * <clock>.progress)"`
  - `<literal>`    ⇒ `"V"`
  - `[expr]`       ⇒ passthrough with `this`/`self` substituted, and any in-scope
    pattern/clock name used as `NAME.attr` substituted to that clock's id — the
    expr-level analog of `over NAME` (e.g. accelerate's
    `zoom [0.4 * accelerate.progress + 0.1 * this.progress]`)
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
audio <content> [loop] [volume <mod>]   # content ::= primary | secondary | runtime
audio stop
```

- **What it is.** The showcase primitive for beats: `every beats N { audio mantra }` fires a
  precanned mantra/cue phase-locked to the entrainment bed, exactly the way `image`/`word`
  are phase-locked today. **No TTS, ever** — `audio` always plays a file from the theme's
  PRECANNED `audio_path` pool (`ThemeBank::get_audio`, `docs/audio.md`); the GRAMMAR only
  decides *when* and *how loud* to play it, never what it says. Themes own the audio pool
  exactly like the image/font pools; this primitive is what lets a modder reach it.
- **Two nouns still apply.** `content` is the SAME bi-thematic vocabulary as `image`/`word` —
  `primary` (theme 0), `secondary` (theme 1), `runtime` (rolled primary-vs-
  secondary at FIRE time, not parse time — identical to `image runtime`, §4.2). `volume` is
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
    - A bare numeric literal (`volume 0.6`) ⇒ `Effect::rate` (the field the deleted
      `SpiralRot` used for its rate; `Audio` is now its only user), applied ONCE via
      `set_theme_audio_volume` right after `play_theme_audio` in the same firing. No
      render-side cost.
    - A `curve`/`[expr]` volume (`volume (curve 0.2 -> 0.8)`) ⇒
      `RenderStmt{Op::AudioVolume, speed=lower(mod)}` (reusing the `speed` field — same
      convention `Warp` reuses `zoom`/`origin`/`speed` for unrelated params, §4.6), evaluated
      and applied every frame by `render_eval`, exactly like `spiral speed`. Clamped to
      `[0,1]` before the call (`Audio::set_theme_audio_volume` clamps again on the engine
      side — belt and braces, not a double-source-of-truth).
    - No `volume` param at all ⇒ engine keeps whatever volume was last set (same "no explicit
      write, no change" default every other param has). The channel's INITIAL volume is
      **full (1.0)** — a bare `audio primary` is audible without any volume line. Written
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
- **Modder note.** `audio mantra` (well, `audio primary`/`audio secondary`) plays a spoken line
  from the theme's audio folder, timed by the SAME `every`/`beats`/`burst` cadence machinery
  that times a flash. `loop` keeps it going; `volume` fades it in/out like any other curve;
  `audio stop` cuts it. This is the whole reason theme audio pools exist: drop a folder of
  mantras next to your images and fonts, and the pattern grammar can trigger them the same way
  it triggers a flash.

### 4.15 `show` — the VISIBILITY WINDOW on a draw (issue #42, SHIPPED, parser-only)

```
show <A> ".." <B>          # fractions of the enclosing clock:  show 0.5..1
show <A>f ".." <B>f        # frames on that same clock:         show 0f..8f
show "[" EXPR "]"          # raw condition escape:              show [this.frame < 8]
```

- **What it is.** The duty/gate knob. A draw with a `show` window paints only while the window
  holds; its content and effects still fire on their own cadence. This is the surface that was
  missing when the eight built-ins were first authored in v3 — `RenderStmt.when` existed and was
  honored every frame (`render_eval.cpp`), but **no syntax wrote it**, so every text layer
  painted continuously for the life of its pattern and every "appears for a beat, then vanishes"
  rhythm in the originals was unportable as authored. See `docs/intent-screenplays.md` (drift
  class S1) for the eight-visual audit that motivated it.
- **It is a `draw_param`, not a statement** — parsed by `parse_params`, so it is available on
  every draw verb (`image` / `draw` / `word` / `line` / `caption` / `subtext`) with no per-verb
  code, exactly like `zoom`/`fade`/`alpha`.
- **Lowering — pure `RenderStmt.when`, ZERO runtime change.**
  - `show A..B`   ⇒ `when = "(clk.progress >= A and clk.progress < B)"`
  - `show Af..Bf` ⇒ `when = "(clk.frame >= A and clk.frame < B)"`
  - `show [expr]` ⇒ `when = <expr>` with `this`/`self` substituted, like any raw modulator
  
  `clk` is the enclosing clock (`this`) — the same id a bare `zoom (curve ...)` on the same
  statement would ride. The window is **ANDed onto whatever gate is already there**, never
  assigned over it: `push_render`'s enclosing-pattern `.active` gate, a burst block's FSM gate,
  and a text draw's `chance` guard all survive alongside it. Half-open by construction
  (`>= A`, `< B`) so two adjacent windows tile a clock without overlapping on the seam.
- **Validation (hard parse errors, no silent clamp).** `B <= A`; a frame window ending past the
  enclosing clock's length (the message names that length); a fractional window ending past 1;
  and **mixing denominations in one window** (`show 0f..0.5`). The last one is deliberate: the
  two forms answer different questions and coercing one silently is exactly the guess this
  grammar refuses to make elsewhere (cf. `beats` with no bed, §4.1).
- **Modder note.** `show` is when a layer is ON. Write it as a slice of the box's life
  (`show 0.5..1` = the second half) or in frames (`show 0f..8f` = the first 8 frames of each
  cut). Everything else about the draw is unchanged — it still pulls its content on cadence,
  it just isn't painted outside the window.

### 4.16 `env` — attack/hold/release ALPHA ENVELOPE (issue #42, SHIPPED, parser-only)

```
env in <X> [hold <Y>] out <Z>     # each operand: Nf (frames) or a fraction of the clock
```

- **What it is.** A piecewise-linear alpha envelope: rise 0→1 over `in`, flat 1 over `hold`,
  fall 1→0 over `out`, then **0 for the remainder of the clock**. ADSR minus the S. Omitting
  `hold` gives a triangle that still has the absent tail.
- **Why it is not `fade inout`.** `fade inout` is a whole-clock triangle
  (`1 - abs(2p - 1)`): its peak is an instant and its alpha is nonzero at nearly every frame, so
  a layer beneath it is **never alone on screen**. `env` has a genuine hold and a genuine hole.
  This is the exact shape the original `animation` visual used for its still layer (16f in,
  hold across the wrap, 16f out, **32f absent** while the animation holds the stage alone) and
  what the v3 port lost by reaching for `fade inout`. `fade inout` is not deprecated — it stays
  the right answer when a symmetric whole-clock triangle IS the intent; `env` supersedes it only
  where a hold was meant.
- **Lowering — one compile-time alpha `[expr]`, ZERO runtime change.** Operands normalize to
  fractions of the clock at parse time (`Nf` divides by the enclosing clock's length), then:

  ```
  alpha = max(0, min(min(clk.progress / IN, (END - clk.progress) / OUT), 1))
  where END = IN + HOLD + OUT
  ```

  Nested `min`/`max` over `this.progress` — the same class as `fade in/out/inout` (§4.3) and
  built from operators `render_eval`'s evaluator already implements. `env` writes
  `RenderStmt.alpha`, so a later `alpha`/`fade` param on the same statement overwrites it
  (last-writer-wins, the documented `draw_param` rule).
- **Validation (hard parse errors).** `in + hold + out` may not exceed the clock — silently
  clipping the release would be precisely the "grammar quietly degraded the intent" failure this
  extension exists to fix. A zero/absent `in` or `out` leg, a fractional operand outside `0..1`,
  and an `Nf` operand under a clock of unknown length are also errors.
- **Modder note.** `env in 16f hold 16f out 16f` on a 64f box: fade up for 16 frames, sit at
  full for 16, fade down for 16, **then be gone for the last 16**. That last clause is the whole
  point — it is how you let the layer underneath have the screen to itself.

### 4.17 `line` — the whole-phrase text verb (issue #42, SHIPPED, parser-only)

```
line <content> <draw_param>* [chance P]      # identical to `word`, except the split
```

- **What it is.** `line` is the `word` statement with `Effect.split = SPLIT_LINE` — a whole
  phrase on screen at once instead of one word at a time. Five of the eight original visuals
  used `SPLIT_LINE`; `word` hardcoded `SPLIT_WORD` (the enum's 0 default) and there was no
  surface for anything else, so those layers were either downgraded to single words or dropped.
- **Lowering — one field, ZERO runtime change.** `Effect{Kind::Text, split = SPLIT_LINE}` plus
  the identical `RenderStmt{Op::Text}` (same `origin`/`zoom` 0.75 defaults) `word` emits.
  `change_text` has always implemented the `SPLIT_LINE` branch (`api.cpp`); the field just had
  no author-facing surface. `word` is unchanged — this adds a verb, it does not re-point one.
- **`spell` is NOT implemented.** SubText's type-out stream (`SPLIT_WORD` reset followed by
  repeated `SPLIT_ONCE_ONLY` advances) is a real follow-up and remains unbuilt; do not read
  `line` as having covered it.
- **Modder note.** `word` = one word at a time. `line` = the whole phrase. Everything else —
  content vocabulary, params, `show`, `chance` — is the same on both.

### 4.18 `alternate` — deterministic A/B theme ping-pong (issue #42, SHIPPED, parser-only)

```
image alternate [chance P] <draw_param>*    # a CONTENT word, beside primary|secondary|runtime
anim alternate [chance P]                   # the standalone animation load
```

- **What it is.** A fourth content word for `image` draws and the standalone `anim` load, giving
  a draw **deterministic** A/B theme alternation instead of a pinned side (`primary`/`secondary`)
  or an independent re-roll per firing (`runtime`). **`alternate` is not `secondary`:**
  `secondary` pins theme 1, `alternate` switches sides. The originals ping-ponged on a counter
  (`_alternate = !_alternate`); the runtime already supported it (`Effect::slot_reg` +
  `Kind::Toggle`), but no grammar reached it, so every port flattened to `runtime` or a pin.
- **Two forms.**
  - **`alternate`** — flips on every firing: A B A B A B.
  - **`alternate chance P`** — the **toggle** fires with probability `P` per pull, so the side
    HOLDS between flips. At `P = 0.5` each pull is an independent coin flip over the two sides,
    i.e. exactly uniform-random theme per image — but as a stateful walk rather than a per-fire
    re-roll, which is what lets a lower `P` express "hold this world for a while, then pivot."
    Note the guard lands on the **toggle**, not the draw: unlike a draw's own `chance P` (§4.12),
    the image still paints every firing.
- **Lowering — ZERO runtime change.** The parser mints a hidden scalar register, emits an
  `Effect{Kind::Toggle}` on it ordered **before** the draw's own pull, and sets the pull's
  `Effect::slot_reg` to that register. `resolved_slot` (`compiled_visual.cpp`) already reads a
  non-empty `slot_reg` as a primary/alternate selector, and `Kind::Toggle` already exists. The
  `chance P` form reuses the identical 100-bucket `Roll` + `Guard::Ge` machinery §4.12 describes,
  gating the toggle. A trailing `anim` on the same draw inherits the same `slot_reg`, so a still
  and its animation never come from opposite themes.
- **The register is STATEMENT-scoped** — a fresh minted name per `alternate`, so two alternating
  draws in one pattern keep independent phase. A named/shared form (`alternate as NAME`, so two
  statements can share one phase) is **deliberately not implemented**; it is the obvious next
  step if a pattern ever needs it, and §4.7's rejected `set`/`inc`/`roll` surface stays closed.
- **Bi-thematic floor untouched.** `alternate` still selects between exactly the two live theme
  slots (§8) — it changes *how* the side is chosen, not how many sides exist.
- **Modder note.** `image primary` is always theme A. `image runtime` rolls a coin every time.
  `image alternate` goes A, B, A, B — and `image alternate chance 0.25` holds a side for a
  while before pivoting.

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
      image primary -> cur fade in zoom (curve 0.0 -> 0.5)
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
earlier drafts described a planned `crossfade`/`pulse` macro system
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
| content theme (primary/secondary) | GRAMMAR selector | the alternate bool | only content axis; 3+ themes do NOT lower |
| theme audio **content** (primary/secondary/runtime) | GRAMMAR selector | `Effect::Kind::Audio` slot (§4.14) | same alternate-bool axis as `image`, resolved at fire time |
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

**The §4.15–§4.18 extensions (issue #42) added NOTHING to this list — that is the point of
them.** `show`, `env`, `line` and `alternate` are all **parser-only sugar**: they lower entirely
onto `RenderStmt.when`, `RenderStmt.alpha`, `Effect::split` and `Effect::slot_reg` +
`Effect::Kind::Toggle` — fields and effect kinds that already existed and were already evaluated
every frame by machinery that already ran. No new `Effect::Kind`, no new `RenderStmt::Op`, no
new node type, no scheduling change, not one line of runtime code. They exist because the
`docs/intent-screenplays.md` audit found the *opposite* failure mode to the one this section
guards against: wherever the runtime already had a capability but the grammar had no surface for
it, the v3 ports of the eight built-ins silently dropped or flattened the behaviour instead of
flagging it. The compile-down floor is about refusing runtime magic that sneaks in through the
grammar; it was never an argument for leaving shipped runtime capability unreachable.

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
  image primary zoom (curve 0 -> 0.5)
}
```
Lowers to `One{ Action[Effect{Image,Primary,->auto}],
RenderStmt{Image, zoom="(0 + 0.5*hello.progress)"} }`. One line: draw primary, zoom from 0
to 0.5 over its 240f life.

### EX2 — one number as curve, fade, AND spiral speed (the unified class)

```
pattern unified for 240f {
  image primary zoom (curve 0 -> 0.5) fade inout
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
    image primary zoom (curve 0 -> 0.3)            # rides flashA (resets at 240f)
  }
  pattern flashB for 240f {
    image secondary zoom (curve 0 -> 0.6 over show)  # rides the WHOLE 480f show clock
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
  image primary zoom 0.5
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
      image primary -> cur fade in zoom (curve 0.0 -> 0.5)
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
    image primary zoom (curve 0 -> 0.5)
    word primary chance 0.5
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
  every 8f { word primary chance 0.25 }
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
  every beats 4 { audio primary loop volume (curve 0.2 -> 0.8) }
  every beats 1 { image primary zoom (curve 0 -> 0.4) }
  spiral speed 2
}
```

With an 8 Hz pulsed bed at 60 fps (`locked_period_frames = 8`, §"Beats" in
`docs/authoring-v3-patterns.md`), this pattern runs 16*8 = 128 frames and re-fires `audio
primary` every 4 beats (32f) — a fresh precanned mantra line pulled from the primary theme's
`audio_path` pool each time, looping, its volume ramping `0.2 -> 0.8` across each 32f window
(a curve, so it lowers to a per-frame `RenderStmt{Op::AudioVolume}` — see §4.14). Meanwhile
`every beats 1` flashes an image once per pulse, independent of the mantra cadence, so the
visual beat and the (four-times-slower) mantra beat both lock to the SAME entrainment bed
without one driving the other. Single-slot v0 means the SECOND `audio primary` firing (at
frame 32) replaces the first line outright — no crossfade/queue for audio, unlike the image
crossfade shape (EX4). Swap `audio primary` for `audio stop` in a later phase to cut the
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

anim_stmt      ::= "anim" ( content | alternate_content )
                    (* standalone: switch WHICH animation the streamer plays (an
                       Effect::Kind::Anim), no image pull, nothing drawn. One-shot setup --
                       e.g. a burst `enter { anim runtime }` picks the burst's animation
                       once; a `draw REG ... anim` then renders it. `anim alternate`
                       ping-pongs the side off its own toggle (§4.18). *)

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
draw_stmt      ::= "image" ( content | alternate_content ) [ "-" ">" REG ]
                        draw_param* anim_param? chance_param?
                 | ( "word" | "line" | "caption" | "subtext" ) content draw_param* chance_param?
                 | "draw" REG draw_param* [ "anim" ]
                    (* trailing `anim` on `draw`: render the animation stream layer instead
                       of the still -- pure render, no change-animation effect (pair with a
                       standalone anim_stmt to pick which); no `every Nth` form here.
                       `line` is `word` with Effect::split = SPLIT_LINE (§4.17) -- identical
                       in every other respect, including the Op::Text RenderStmt and its
                       0.75 origin/zoom defaults. *)
content        ::= "primary" | "secondary" | "runtime"   (* Slot::Primary / Secondary / Runtime;
                                                            "runtime" resolves the theme at
                                                            fire time, not parse time *)
alternate_content
               ::= "alternate" [ chance_param ]
                    (* §4.18, `image` draws and the standalone anim_stmt ONLY. Mints a hidden
                       statement-scoped scalar register, emits Effect{Kind::Toggle} on it
                       BEFORE the pull, and sets the pull's Effect::slot_reg to it. The
                       optional chance_param gates the TOGGLE (the flip is probabilistic; the
                       draw still paints every firing) -- unlike a draw's own chance_param,
                       which gates the draw. `word alternate` etc. is a hard parse error. *)

draw_param     ::= ( "zoom" | "origin" | "alpha" | "brightness" ) modulator
                 | "fade" ( "in" | "out" | "inout" | modulator )
                 | "show" show_window
                 | "env" "in" env_len [ "hold" env_len ] "out" env_len
                    (* NOTE: draw_param is parsed in a loop (parse_params) so any number of
                       these may repeat/mix in one statement; last writer for a given
                       zoom/origin/alpha field wins. `brightness` is a literal alias for
                       `alpha` — same RenderStmt field. `env` writes the same alpha field, so
                       a later `alpha`/`fade` on the statement overwrites it. *)

show_window    ::= FLOAT ".." FLOAT                    (* fractions of the enclosing clock *)
                 | UINT "f" ".." UINT "f"              (* frames on that same clock *)
                 | "[" EXPR "]"                        (* raw condition, this/self substituted *)
                    (* §4.15. Lowers to RenderStmt.when, ANDed onto any gate already there
                       (pattern .active, burst FSM, chance guard). Half-open: >= A, < B.
                       Hard parse errors: B <= A; a frame window past the clock's length; a
                       fractional window past 1; mixing frames and fractions in one window. *)

env_len        ::= UINT "f" | FLOAT                    (* frames, or a fraction of the clock *)
                    (* §4.16. Lowers to ONE alpha [expr]:
                       max(0, min(min(clk.progress/IN, (END-clk.progress)/OUT), 1)),
                       END = IN+HOLD+OUT -- trapezoid with a true absent tail past END.
                       Hard parse error if IN+HOLD+OUT overruns the clock. *)
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
only, never top-level `stmt`s; `spell <content> every Nf` (the SubText type-out stream, the
deliberately-unbuilt follow-up to §4.17's `line`); and `alternate as NAME` / `alternate with
NAME` (a shared-phase toggle across two statements — §4.18 ships statement-scoped only).

---

## 12. Open questions

The draft carried a longer list; most of it was settled by shipping. The drunk clamp-bias
question is moot (drunk shipped as warp-sugar, §4.6 — no register/clamp/RNG at all, §4.5's
design was dropped rather than tuned); `super_fast`'s fidelity question is closed by §0.5;
and the macro-drift question is moot because no macro layer was ever built (§6).

**What is still open is listed in Appendix A.** The most commonly hit of them: whether
`beats`/`locked` should remain a compile-time period or wait for a live audio clock (§9's
non-goals; see `docs/authoring-v3-patterns.md` for the current compile-time semantics).

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

## Appendix A — Open design questions carried forward

Questions the shipped design deliberately left unanswered. Everything in §0–§11 is
settled; these are not. They are recorded so they get re-argued on purpose rather than
re-discovered by accident.

(The original Appendix A — a completeness critic's findings against the pre-ship draft —
and Appendix B — the phased implementation plan — have been deleted. Both were fully
executed or superseded, and both cited `pattern_parser_v2.cpp`, a file that no longer
exists. Git history holds them.)

- Spiral selector home: should `look { spiral type=N width=W }` lower to a deterministic SpiralSet *effect* (Extension #3) or be promoted to per-Program proto fields edited in the session-config editor (the F2 panel)? The effect keeps it per-(sub)pattern and grammar-local; the proto field gives editor support but crosses into session config. (Originally phrased against the wxWidgets ProgramPage, which no longer exists; the design tension it names does not depend on which editor is doing the editing.)
- `beat` modulator semantics: today `beat`/`locked` is a compile-time period (round(global_fps/pulse_hz)), not a live phase-accurate audio clock. v3 ships the compile-time version. Do we ever want true phase coincidence (a live audio->visual beat counter), which is a genuine runtime extension exposing the audio thread's pulse_phase to the render loop? Named but deferred.
- Per-segment ramp ids: `every ramp` unrolls to per-segment leaves with minted ids (`NAME_00`, `NAME_01`, …) — but a bare `every <curve>` still unrolls to un-id'd leaves, so per-flash wobble inside such a ramp is unauthorable. Is closing that worth an unroller change in a later milestone, or is whole-ramp progress an acceptable permanent limitation? Carried forward, not solved.
- Drunk as warp vs positional wobble: `drunk`/`warp` shipped as a sinusoidal *sampling* displacement in the fragment shader (§0.2, §4.6). Some authors will instead expect the whole image to jitter or rotate, which needs a new `RenderStmt` offset/rotation param plus a vertex-shader uniform — Extension #1b, explicitly declined for v3 (§9.1b). Is that demand strong enough to justify #1b in a future version, or is the wave warp sufficient?
