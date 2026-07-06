# Roadmap — a v2 visual grammar

> **SUPERSEDED — v3 shipped instead; kept for history.**

> **Status: design notes / opinions, not a spec.** Forward-looking. The current
> system (see [visuals.md](visuals.md)) is done and shipping. This doc captures *why*
> the current grammar is shaped the way it is, where that shape is over-complex, and
> a thesis for a v2 grammar. Written end-of-session so the thinking isn't lost; meant
> to be pressure-tested by Codex + a brainstorm pass before any of it is built.

## The goal (in the user's words)

> "Define a v2 grammar that accomplishes a ~90% similar pattern for all 8, affords
> infinitely more, and is described in less than half the grammar."

Each of the 8 built-ins should read in ~3–6 lines. The vocabulary should shrink, not
grow. And the affordance space (more themes, arbitrary pairings, new cadences) should
get *bigger* as the grammar gets *smaller*.

The trigger for this whole rabbit hole: **"what the fuck does SUPER_PARALLEL actually
do?"** — the Creator tooltip is worthless, and the v1 grammar over-corrected into
something *too* complex to answer that at a glance either. The target is a grammar
where the pattern *is* its own readable explanation.

## The key constraint to drop: "must be byte-identical"

The entire v1 grammar was built under one rule: **reproduce the 8 hardcoded visuals
exactly**, proven by a frame-for-frame render-equivalence test. That constraint was
correct for a safe migration — it let us delete 600+ lines of C++ with confidence —
but it is *the* source of the complexity we now want to shed. Identity forced:

- per-segment `generate` unrolling (ACCELERATE = 45 segments),
- a whole imperative scalar-register state machine (`set`/`inc`/`toggle`/`roll`/
  `pulse`/`when`/`copy`) to mirror hidden member variables,
- a bespoke native FSM effect (`super_fast_tick`),
- the exact `one`/`seq`/`par` nesting that mirrored the original C++ structure.

**v2 drops identity and targets ~90% of the *felt* behaviour instead.** That single
relaxation is what unlocks "half the grammar."

## Hard compatibility constraint

v2 cannot simply replace the v1 parser for stored sessions. Custom visual patterns
are persisted as raw `VisualPatternSource.source_text` with no grammar-version field,
and playback reparses that text at runtime. Any implementation plan must first choose
one of:

- keep a v1 parser/compiler path indefinitely and dispatch by detected syntax;
- add an explicit pattern grammar/source version to `VisualPatternSource` and migrate
  saved patterns;
- introduce a new v2 source field/message while leaving v1 `source_text` intact.

Built-in patterns can be rewritten freely because they are compile-time sources. User
stored patterns need a compatibility story before v2 can become the default authoring
or playback grammar.

## Audit — every v1 item, what it affords, and the v2 verdict

(See [visuals.md](visuals.md) / `pattern_parser.h` for the authoritative v1 grammar.)

### Structure: `seq` / `par` / `one`
- **What they are.** `seq` = children in order (length = sum). `par` = children
  together, repeating (length = LCM). `one` (OneShot) = children together but each
  fires only *once*, length = max — in practice it's used almost entirely for the
  **"init leaf then body"** idiom (run a setup action once, then loop the rest).
- **Affordance.** `seq` = phases over time; `par` = simultaneity/overload; `one` =
  one-shot setup.
- **v2 verdict.** `one` is a faithful mirror of a cycler class but a *confusing*
  authoring primitive ("what is `one`?" — exactly the user's question). Replace its
  common setup use with an explicit scoped `init { ... }` block or an `@enter` effect
  modifier. The important semantic is **entering the containing cycle/phase/repeat**,
  not construction-time setup and not only global frame-0 setup. Current built-ins use
  `one` inside repeats and generated segments for per-subcycle initialization, so v2
  needs nested enter semantics. In a v1-shaped transitional syntax, keep `seq` + `par`
  as the two structural combinators; the target authoring surface below may mostly
  hide explicit `par` behind named lanes.

### Cadence: `every N [@K]`, `divide`, `repeat`, `offset`
- **What they afford.** The timing skeleton — how often a leaf fires, phase offset,
  looping, lane staggering (`offset` is how SUPER_PARALLEL interleaves 3 lanes).
- **v2 verdict.** Keep `every`/`repeat`. `offset` stays (cheap, enables interleave).
  `divide` is subsumed by the cadence-modifier idea below.

### State machine: `set` / `inc` / `toggle` / `roll` / `pulse` / `when` / `copy`
- **What it is.** The imperative escape hatch. It exists *only* to reproduce hidden
  member state in the originals: `_animation_mod` (random N), `_anim_cycle` (parity),
  `_alternate` (toggle), `_sub_speed` (ramp), `_animated` (captured coin-flip).
- **This is the single biggest complexity sink in v1.** It's ~7 effect kinds + a
  parallel scalar register block + the `when` guard, and it turns a declarative
  grammar partly imperative.
- **v2 verdict.** Almost everything it expresses collapses into a handful of
  declarative **cadence/value modifiers**:
  - `pulse … → flag … when flag` (do something every Nth time) → **`every 3rd: …`**
  - `toggle alt` + `… reg alt` (alternate the slot) → **`alternating`** modifier on a
    content pull.
  - `roll mod : 2 4 8` + counter (random period) → **`every random(2..8): …`**.
  - `inc sub_speed` + `when sub_speed >= N` (a value that climbs) → **`ramp`** modifier.
  Net: replace ~7 imperative ops + registers + guard with ~4 declarative modifiers.
  **This is the headline v2 simplification.**

### Content pulls + slots: `image/text/anim/subtext/small_text(primary|alternate|…)`
- **What "slot" means** (the keystone — see [visuals.md](visuals.md) §themes): the
  `ThemeBank` keeps exactly **two** themes live (slot 1 = primary, slot 2 = alternate);
  the slot bool just picks which live theme a pull reads from. `primary`/`alternate`
  is *theme A vs theme B*.
- **The limitation.** The selector is binary because only two themes are live. There
  is no way to express a 3-theme pattern.
- **v2 verdict — the highest-leverage low-level change.** Generalise the slot from a
  bool to a small **theme index** over a live set of size *K* (default 2, extensible):
  `image(theme 0)`, `image(theme 2)`. This single change:
  1. removes the primary/alternate special-casing,
  2. opens 3+ -theme patterns once the bank/API can keep K>2 live — this is a real
     `ThemeBank` and public visual API redesign; slot 3 is currently the async loader,
  3. makes **associative-conditioning** patterns first-class (deliberately pair
     theme-A image with theme-B text on one beat),
  4. is exactly the "maybe we simplify something super low-level like `get_image`
     itself" the user intuited. `get_image(bool)` → `get_image(uint slot)`.

### `generate VAR from A to B { … }` + `[expr]`
- **What it affords.** Compile-time unrolling of parameterised structure (ACCELERATE's
  45 length-shrinking segments).
- **v2 verdict.** Powerful but heavy for the *common* case, which is just "speed up
  over time." Add a `ramp(from, to, over)` time-modifier for that; keep `generate`
  for genuinely parameterised structure. ACCELERATE's 45 lines → a few.

### `burst { … }` (BurstCycler) and `super_fast_tick`
- **What they afford.** `burst` = a base loop randomly interrupted by a bounded burst
  + cooldown. `super_fast_tick` = SUPER_FAST's bespoke 4-state FSM, isolated as a
  native effect (the one place v1 admits "this didn't fit the grammar").
- **v2 verdict.** Under "90% similar," SUPER_FAST's *felt* behaviour is "very fast
  image cuts with occasional animation bursts" — which is `burst` over a fast image
  cadence. **v2 deletes `super_fast_tick` entirely.** `burst` stays as a general
  primitive.

## The v2 thesis

Replace v1's low-level imperative + structural machinery — the scalar-register state
machine, the FSM effect, generate-unrolling, binary slots, three structural
combinators — with a **small set of declarative modifiers over a generalised
content-pull**:

- **Content:** `image/text/anim(theme i)` over a live theme set of size *K*.
- **Cadence modifiers:** `every Nth`, `alternating`, `ramp(from,to,over)`,
  `burst(...)`, `every random(a..b)`.
- **Structure:** named lanes are parallel by default; ordered `phase` blocks express
  sequence; scoped `init` / `@enter` handles setup when a cycle, phase, or repeated
  lane iteration begins. `seq`/`par` remain useful IR/lowering concepts and possibly
  a compatibility syntax, but they are not the preferred v2 authoring surface.
- **Render:** bind parameterised render shapes to named lanes. Layers/overlay become
  first-class instead of hidden in per-pattern C++ presets.

Predicted outcome: each of the 8 built-ins in 3–6 lines, a smaller vocabulary, and a
*larger* affordance space (3+ themes, arbitrary cross-theme pairings, new cadences,
overload/conditioning patterns that don't exist today).

## Honest open questions / risks

1. **Compatibility/versioning.** Stored custom patterns are raw v1 DSL text today.
   Before v2 ships as a replacement, add a pattern-source versioning/migration plan or
   retain a v1 parser fallback. This is separate from visual validation: it protects
   user sessions.
2. **Validation.** Dropping identity discards the render-equivalence harness — our
   current safety net. v2 needs a new validation story: visual review, "render-family"
   snapshot tests, or property tests over the modifiers. **This is the biggest gap.**
3. **Render is still hand-written C++ per pattern** (`render_preset.cpp`).
   *(Since built — render is now data: per-pattern `render { }` blocks run by
   `render_eval.cpp`; `render_preset.cpp` is deleted. See `visuals.md`.)* The deep
   v2 question: can render *also* become data? **→ Resolved in the Codex notes below:**
   a small fixed library of *parameterised render shapes* (`focus`/`fade`/`stack`/
   `cut`) configured from the DSL — NOT a full render-expression language. This is the
   single most important v2 decision; everything else assumes it.
4. **Genuinely bespoke geometry.** FLASH_TEXT's crossfade, SUPER_PARALLEL's triple
   fade, ANIMATION's start/hold/end window are specific render math. "90% similar" is
   a per-pattern judgement; some may need a dedicated render shape regardless.
5. **The `ThemeBank` K>2 extension** is a non-trivial change to async loading /
   memory (currently 2 live + 1 loading + 1 unloading). Scope it before promising
   3-theme patterns.

## Next actions (for next week)

1. **Codex design-synthesis pass** on this thesis — "here's the problem + constraints,
   what v2 grammar would you design?" Don't pre-bias it with my vocabulary; let it
   propose. (One pass is folded into this doc's "Codex notes" below.)
2. **Brainstorm swarm** (the user's "army of subtasks"): spin several agents to each
   re-express the 8 built-ins in a *proposed* v2 grammar and report where it breaks —
   the breakages are the real spec. Compare proposals, keep the convergent core.
3. **Resolve the render-as-data question (#3 above) first** — it's the gate.
4. Define the persisted-source compatibility plan: version field vs new source message
   vs v1 parser fallback.
5. Then write an actual v2 spec, and a migration/validation plan that doesn't rely on
   byte-identity.

## Codex notes

A Codex design-synthesis pass (asked to propose independently, then critique my five
proposals). It converged with the thesis but reframed and sharpened it:

**The big reframe — v2 is a "lane + render-shape language," not a smaller Cycler
syntax.** Authors describe: what content *lanes* exist, when each ticks, which live
theme it pulls from, and which *render shape* presents them. The compiler still lowers
to Cyclers, but that's an implementation detail, not the authoring surface. Make blocks
**parallel by default**; ordered `phase NAME for D:` lines give sequence — this mostly
deletes explicit `par`. Render binds to **named lanes**, not arbitrary node ids.

**The render gate — resolved (this is the important one).** Render *must* become data,
but NOT a full render-expression language (that just becomes a second complex DSL).
The middle path: a **small fixed library of parameterised render shapes** in C++,
configured/bound from the DSL. Enough shapes to cover the 8:
- `focus` — one dominant image/anim layer + text/subtext/spiral
- `fade` — previous/current (or start/end) crossfade
- `stack` — N interleaved image lanes (SUPER_PARALLEL)
- `cut` — current/next hard cuts + optional burst overlay
The current hand-written presets cap the "infinite patterns" claim because they only
let authors change *schedules*, not layer topology / transition / zoom / text
placement. Parameterised shapes lift that cap without a render-expression rabbit hole.

**Verdicts on my five proposals:** all five *keep*, with refinements:
1. `init{}` — yes, but **define it as scoped enter semantics**: once when the
   containing pattern, phase, repeat iteration, or lane cycle starts. "Once per cycle
   at frame 0" is the top-level case, not the whole feature.
2. Modifiers — yes, but **distinguish `choice(2,4,8)th` (pick once per cycle/phase)
   from `random(2..8)` (reroll every time).** If you don't, you accidentally
   re-invent registers to say "picked once" vs "jitter." Add `chance(p)` and a
   `window(...)`/`active N` modifier too.
3. theme-index — yes, high-leverage, but **bigger than `get_image(uint)`:** it touches
   `get_image`/`get_animation`/`get_text`/`get_font`/`change_animation`, the debug
   overlay, and the ThemeBank active/loading/unloading model (slot 3 is the loader, not
   live). It's a real ThemeBank redesign. Also: call it `theme`, drop the word `slot`.
4. delete `super_fast_tick` — yes, but only works cleanly once render is shape-driven
   (the old preset expects FSM registers).
5. `ramp` — yes, but make it a **first-class signal**, not just an `every` modifier:
   image cadence, spiral speed, zoom, text density, and anim probability may all bind
   to the *same* ramp.

**Validation without byte-identity (replaces the equivalence harness) — four layers:**
1. **Shape golden tests** — exact render-command logs for `focus`/`fade`/`stack`/`cut`
   on deterministic fake inputs (this is the old harness, re-pointed at shapes).
2. **Pattern-signature tests** — seeded traces checked by *metrics*: image-cadence
   range, text density, anim-burst rate, theme mix, layer count, no blank frames.
3. **Property/fuzz** — parse+compile random valid patterns; assert bounded cycles,
   valid theme indices, alpha ranges, no crashes.
4. **Visual-review artifacts** — short deterministic clips / contact sheets, optionally
   perceptual hashes with tolerance.

**Codex's SUPER_PARALLEL in v2 (~6 lines), to feel the delta:**
```
pattern super_parallel {
  init { themes; font; spiral.new }
  render stack(img[3], solo 16, alpha [1,.5,.33], zoom sweep(0.125,.875), text flash(32 half), spiral)
  cycle 1152:
    image img[0..2] theme [0,0,1] every 96 stagger 32 active 16; anim img[0] theme alternating(0,1) every 96
    text word theme any every 32; upload every 32 @16; spiral 3.5
}
```

**Net effect on the thesis:** the headline isn't "shrink the grammar," it's **two
languages collapse into lanes + shapes** — schedule and render both become data, slots
become themes, and the imperative state machine disappears into modifiers. The render
gate (#3 in Open Questions) is answered: *fixed parameterised shapes, not a render
expression language.*
