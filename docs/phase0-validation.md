# Phase 0 — paper validation of the v2 intent grammar

**Status: validation pass, no code.** This is the go/no-go gate `q1-q2-next-steps.md` §8
calls for: write all 8 built-ins in the proposed surface, check each against the *actual*
source in `builtin_patterns.cpp`, and let the breakages become the spec. It also settles
the terminology (`§1`) the other docs left inconsistent.

It draws on `q1 response.md` (which already drafted v2 versions of all 8) and
`q1-q2-next-steps.md` (the authoritative synthesis), but every claim below was
re-checked against the real DSL sources, not against the other docs. Where the
docs and the code disagree, the code wins and it's flagged.

---

## 1. Terminology decision

The v2 corpus inherited its vocabulary wholesale from **music composition** (voice,
phase, signal, cadence, score) and **compiler theory** (lane, register, IR). For a
hypnosis-adjacent audience writing patterns, several of those terms carry the wrong
connotation. The single rule: **a term should mean, to a non-expert author, what it
actually does — no domain-transfer required.**

| v2 docs used | Canonical term | Why the old term misleads |
|---|---|---|
| **voice** (q2; next-steps §3 chose it) | **stream** | The worst offender. In an *audiovisual hypnosis* tool (which has a binaural entrainment bed) "voice" reads as **spoken/human audio**. A "voice" here is a *visual* content stream — image cuts, text, spiral. `stream` (or `track`) says that without the audio implication. |
| **signal** (q1/q2) | **curve** | Reads as audio/DSP/electrical. The thing is "a value that changes over time" — a `ramp`. `tension curve` is self-explanatory; `tension signal` is not. (`ramp(...)`, `pick(...)`, `chance(...)` stay as the constructors; **curve** is just the category name.) |
| **phase** (everywhere) | **section** | Two clashing meanings: the intended "named span of time (build, flash)" vs. **oscillator phase** — and the engine already uses "phase" for `OffsetCycler`'s phase-shift. An entrainment audience reads the wrong one. `section "SLOW" for 1024` is unambiguous. |
| **lane** (q1; IR term in next-steps) | *(internal only)* | Fine as a compiler/IR word; keep it out of author-facing text. Where it leaked into the surface, it's now **stream**. |
| **slot** (v1) | **theme** | Already overloaded (theme-slot vs image-register slot vs loader slot). The migration's plan to say `theme N` is right; purge residual "slot" from author docs. |
| **stack** (shape) | **stack** *(keep, gloss it)* | Programmers read data-structure; authors should read "N image layers composited." Keep the name (it's the established shape) but always gloss as "layered." |
| **cadence** (q1/q2) | **rate / every** | Music/cycling term. The v1 word `every` was already clearer; keep `every N` and `rate` and don't introduce `cadence`. |

**Shapes keep their names** — `focus` / `fade` / `stack` / `cut` are concrete enough and
already established in the design. **`description "..."`** (the human-readable per-phase
annotation — named `description`, *not* `intent`, since "intent" names the whole grammar/layer) stays.

The rest of this doc is written in the canonical terms: **stream**, **curve**, **section**,
**theme**, **shape**.

> The q1/q2 *response* and *prompt* files are kept verbatim as historical design input
> (per `docs-cleanup-plan.md`); they are **not** retro-edited to this vocabulary. The living
> doc (`q1-q2-next-steps.md`) is updated to match this decision.

---

## 2. Per-pattern validation

Format per pattern: **intent** (the one-sentence test from next-steps §6) · **v2 sketch**
(canonical terms) · **verdict**. Verdicts: **CLEAN** (translates with no meaningful loss),
**LOSSY-OK** (loses detail, but the loss is acceptable felt-behavior trimming), **BREAKS**
(the proposed model can't express it; needs design before committing).

### 2.1 `slow_flash` — *two laps of slow primary flashes, then fast alternate flashes*
- Source: `repeat 2 seq { section SLOW (1024f) ; section FAST (512f) }`. SLOW = `repeat 16`
  primary image every 64; FAST = `repeat 32` of (alt image every 8 + alt text every 16@8).
- v2: two `section`s with parallel streams; `spiral rate 2` / `rate 4` per section. Maps cleanly.
- **Catch:** the v1 **render** branches on section — the zoom formula differs in SLOW vs FAST
  (`slow_loop.active ? … : …`), and `anim` shows only on *odd* repeats (`slow_repeat.index % 2`).
  q1 hides this behind `focus(… mode slow_fast)` — an unspecified "mode" that's really a
  section-conditional render. A `focus` shape with no section-awareness can't reproduce it.
- **Verdict: LOSSY-OK**, *if* `focus` gains a per-section zoom param. The "odd-repeat anim" is
  droppable. Flag: this is the first hint that the "4 fixed shapes" carry hidden per-pattern modes.

### 2.2 `accelerate` — *a cadence ramp from length 56 down to 12*
- Source: 6 `generate` blocks unrolling ~45 segments; slot bands (56–48 alt, 47–36 primary,
  35–25 alt, 24 alt, 23–16 primary, 15–12 primary — **q1's band table is accurate**); anim
  modulus `pick(2,4,8)`; `upload` only while L>24; spiral `1+(56-L)/16`.
- v2: one `len = ramp(56 -> 12, dwell pow6)` curve driving image cadence, spiral rate, upload
  gate, and zoom. This is the keystone win — 45 segments → one curve.
- **Verdict: LOSSY-OK, and less lossy than the docs fear** — see §3.1. The v1 render block here
  is *already* a self-admitted approximation (source comment, lines 47–52: the per-image zoom
  wobble and exact text gate were already dropped in the committed v1). So accelerate has **no
  byte-identity to lose** — that ship sailed at the render-as-data port. But this is also the
  pattern next-steps §6 rightly flags for **modifier-soup density**: the one-liner carries
  `bands(...) every len anim every pick(2,4,8)th … while len>24 … rate ramp(...)` — fewer lines,
  not fewer concepts. Density gate applies here.

### 2.3 `sub_text` — *an alternating image stream with a subtext cadence that slows over time*
- Source: toggle `alt` slot each fire; `pick(3,5,7)` anim modulus; **`inc sub_speed`** selects
  among 12/24/48-frame subtext cadences.
- **New finding (hidden assumption / latent bug):** `sub_speed` is **never reset** — the init does
  `set animation_counter 0, set alt 1` but only `inc sub_speed`. So it grows *monotonically across
  every replay of the pattern for the whole session*. The subtext doesn't "ramp over this cycle";
  it permanently slows the longer the session runs. Almost certainly not intended. q1's
  `ramp(12 -> 48, over cycles)` quietly *changes the behavior* (a per-cycle ramp), and that's
  probably the right fix — but it should be a **conscious** "we're correcting muddy persistent
  state," not an accidental reinterpretation. This is exactly the kind of thing an intent grammar
  should surface and a paper pass should catch.
- **Verdict: LOSSY-OK**, with an explicit note that v2 *reinterprets* `sub_speed` rather than
  preserving it.

### 2.4 `flash_text` — *previous/current crossfade with an optional captured animation*
- Source: `roll animated : 1 0` (coin, re-rolled per loop); `alt` toggles per oneshot;
  `copy end -> start` hands the previous image into the crossfade.
- v2: `render fade(...)` + `transition previous`; `animated = coin(0.5)`. `copy` disappears behind
  `transition previous`, which is the concept the visual actually wants.
- **Verdict: CLEAN.** The `fade` shape was designed for exactly this; the register choreography
  is incidental.

### 2.5 `simple` (enum `PARALLEL`) — *one steady image; every third one animates on the alternate theme*
- Source: `pulse simple_counter every 3 -> anim_on`; that flag pulls the alt slot + fires anim.
  Text on `counter.index == 1 or 2`.
- v2: `image img theme 0 every 64 anim every 3rd`. The every-third accent is first-class.
- **Verdict: CLEAN.** This is the canonical "image-modifier animation" case — the one the modifier
  model is *right* for. Also the clearest argument to **rename the enum** (`PARALLEL` → something
  honest) independently of v2.

### 2.6 `super_parallel` — *three staggered image streams*
- Source: 3 `offset 0/32/64` streams, each image every 16 then idle (lap = 96f, `repeat 12` =
  1152f); `alt_anim` toggles per lap, anim on **lane 0 only**; render alphas `[1, ½, ⅓]` with a
  "solo" mode when one stream is in its active 16-frame window.
- v2: `render stack(img[3], solo 16, alpha [1,.5,.33])` + `image img[0..2] … stagger 32 active 16`.
- **Verdict: CLEAN** — *the* proof the lane/stream model pays off (30 lines → ~6). **Catch:** the
  "solo mode" compositing (when a stream is in its window, show it alone at α=1) is non-trivial and
  lives inside the `stack` shape as a `solo` param. Like slow_flash, a shape carries a hidden mode.

### 2.7 `animation` — *animation-dominant, with a backup/current fade window*
- Source: `image backup anim alt […]` — the **backup image is drawn as animation, always**;
  `current` is a crossfade overlay gated by `start_end_timer = seq{32, 960, 32}` (fades in over
  the first 16 / out over the last 16 of the 960 middle).
- **This is the model break next-steps §5 predicted, confirmed against source.** The *subject* is
  the animated backup, not an accent on an image stream. A modifier hanging off an image stream
  (`image … anim every N`) cannot say "the animation is the main event." q1 itself half-acknowledges
  this by splitting `anim` onto its own line.
- **Verdict: BREAKS** under the image-modifier model. Resolvable with the **first-class `anim`
  stream** (next-steps §5): `stream flash: anim of backup theme alternating(0,1)` with its own
  cadence. Must be designed, not hand-waved.

### 2.8 `super_fast` — *rapid cuts with animation bursts*
- Source: the one genuine FSM — `super_fast_tick` (native C++), 4 states, ticking every 8 frames,
  writing `sf_state/sf_alternate/sf_anim_timer/sf_text_mod` + current/next; render branches on all
  of them; blank-overlay near cut ends; anim fired by the FSM **decoupled** from the cut cadence.
- v2: `render cut(...)` + `burst(...)`. Loses the FSM, the blank overlay, exact current/next handoff.
- **Verdict: BREAKS (largest loss).** Two honest options: **(a)** accept it — `cut + burst(chance,
  dur, cooldown)` is "rapid cuts with bursts," which is the felt effect (the 90%-rule call both
  reviews make); or **(b)** keep `super_fast` exact behind the v1 `mechanics { }` escape hatch.
  Either is fine; it just needs an explicit decision (next-steps §9.4).

### Scoreboard

| Pattern | Verdict | Why |
|---|---|---|
| `flash_text` | **CLEAN** | `fade` + `transition previous` fits exactly. |
| `simple` | **CLEAN** | Image-modifier anim is correct here. |
| `super_parallel` | **CLEAN** | Stream/stagger model's best case. |
| `slow_flash` | **LOSSY-OK** | needs section-aware `focus`; odd-repeat anim dropped. |
| `accelerate` | **LOSSY-OK** | ramp keystone; already non-identical in v1; watch density. |
| `sub_text` | **LOSSY-OK** | reinterprets monotonic `sub_speed` (probably a fix). |
| `animation` | **BREAKS** | anim-dominant; needs first-class `anim` stream. |
| `super_fast` | **BREAKS** | FSM; accept-as-bursts or keep behind escape hatch. |

3 clean, 3 acceptable-loss, 2 need-design. **No pattern is inexpressible** — but the two BREAKS
both turn on the *animation model*, which is therefore the one true blocker.

---

## 3. Cross-cutting findings

### 3.1 The "frame-identical safety net" is already partly gone — v2 gives up less than feared
Every v2 doc treats "all 8 proven frame-identical, and v2 abandons that net" as the central risk.
Against the source that's softer than stated:
- **`accelerate`'s committed v1 render is self-admittedly an approximation** (source comment lines
  47–52: dropped per-image zoom wobble + text gate, because generated un-id'd nodes can't be
  addressed). So accelerate is **already not identical** to the original hardcoded visual.
- The render-as-data port's own equivalence claims were "modulo the RNG-dependent anim-type enum"
  for several patterns, and `super_fast` was only ever eyeballed.

So byte-identity is **already** not the live invariant. That *weakens the strongest objection* to
v2 (you're not demolishing a pristine guarantee — it's already cracked) **and** strengthens the case
for a real replacement validation story, because "eyeball it" is what produced the silent
accelerate drift in the first place.

### 3.2 The animation model is the one blocker (confirmed against source)
`simple` and `super_parallel` use anim as an accent → image-modifier is correct. `animation` and
`super_fast` use anim as the *subject* / a *decoupled burst* → image-modifier **cannot** express it.
next-steps §5's first-class `anim` stream (binds to a slot, owns its own cadence/gating/theme)
covers all four. **Validated: design this before any grammar commitment, with all four anim
patterns as the test set.** This is non-negotiable; it's the spec's load-bearing wall.

### 3.3 "4 fixed shapes" is really "4 shapes + hidden per-pattern modes"
`slow_flash` needs section-aware zoom in `focus`; `super_parallel` needs `solo` compositing in
`stack`; `animation` needs a fade-window in `fade`; `super_fast` needs blank-overlay in `cut`. Each
shape is acquiring pattern-specific parameters that are really mini render-programs. The "small
fixed library" stays small in *count* but each shape's parameter surface is where the old
render-expression complexity will re-accrete. **Watch this:** if shape params start needing
conditionals, you've reinvented `render { }` with extra steps. Keep a hard line that shape params
are values/curves, never predicates.

### 3.4 "Simpler" is real but uneven — gate it, don't assume it
Confirmed: `super_parallel` collapses dramatically (the upside is real). `accelerate` trades line
count for modifier density (fewer lines, not fewer concepts). next-steps §6's gates are the right
mitigation: mandatory `description "…"`, a ~3-modifier-per-line smell cap (push overflow into a named
`curve`), and judge readability on `accelerate`/`sub_text`/`animation`, never on `super_parallel`.

### 3.5 `theme N` for N>2 is syntax-only until a separate runtime project
Unchanged from the docs, restated because it's the headline affordance: the grammar can accept
`theme 2`, but the binary primary/alternate slot + ThemeBank/loader means N>2 does nothing until a
scoped ThemeBank redesign lands. **Don't sell 3+ themes as a v2 feature** — it's a separate project
gated behind v2. The AST change (`Slot` → `ThemeRef`) is the only part that's "front-end."

---

## 4. Blocking decisions (need your sign-off before any build)

1. **Animation model** — adopt the first-class `anim` stream (§2.7, §3.2)? *Recommend yes;* it's
   the only thing that makes `animation` and `super_fast` expressible. **Hard blocker.**
2. **`super_fast` FSM** — accept-as-bursts (lossy) or keep exact behind a v1 `mechanics { }` hatch?
   *Recommend accept-as-bursts*, hatch available if a side-by-side looks wrong.
3. **Fidelity sign-off** — explicitly OK these losses: slow_flash odd-repeat anim; accelerate zoom
   wobble (already gone); sub_text `sub_speed` *reinterpretation*; animation easing; super_fast FSM.
4. **Shape-param discipline** — agree the hard line in §3.3 (shape params are values, never
   predicates) so render complexity can't crawl back in.
5. **Primary win** — when "simpler" and "more expressive" conflict (accelerate), which wins? This
   sets the grammar's character (next-steps §9.5).

Deferred per next-steps §4 (not re-litigated here): Score IR, the 3-tier escape hatch (→ one
`mechanics` block), the 5th `field` shape, `feel` semantics, speculative K>2 runtime.

---

## 5. Go / no-go

**Go — conditionally.** The paper pass clears the cheapest gate: all 8 are expressible, 6 cleanly
or acceptably, and the 2 that break both break on the *same* issue (animation model) for which a
concrete fix exists. The intent bet is sound.

But **not yet "fuck it lets ball."** Two things must land first, both paper-only:
- **(a)** Resolve the **animation model** (§3.2) on paper against all 4 anim patterns — if the
  first-class `anim` stream expresses all four, animation is solved; if not, the breakages are the
  spec.
- **(b)** Write the **replacement validation story** (§3.1): shape golden tests + per-pattern
  signature metrics + visual-review artifacts. "Eyeball it" already produced silent accelerate
  drift; don't abandon the net without a successor.

Then the de-risked build order (next-steps §8) is right: **curves → shapes → theme-index → surface
grammar**, ~70% of the felt simplification arriving (as curves + shapes) before a single line of
new grammar.

---

*Sources cross-checked: `src/trance/visual/builtin_patterns.cpp` (all 8, read in full),
`pattern_ast.h`, `pattern_parser.h`, `render_eval.h`, `api.h`. Design inputs: `q1 response.md`,
`q2 response.md`, `q1-q2-next-steps.md`, `roadmap-grammar-v2.md`.*
