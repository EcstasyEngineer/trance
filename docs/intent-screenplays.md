# Intent analysis: the 8 pre-fork visuals vs. the v3 built-ins

> This is the design analysis behind issue #42 — the audit that motivated the `show` / `env` /
> `line` / `alternate` grammar extensions (`docs/spec-grammar-v3.md` §4.15–§4.18) and the
> built-in re-authoring that follows them.

> **Status — read this before trusting the drift lists below.** This document was written
> *before* the extensions it proposes were built. The four parser-only surfaces have since
> **shipped**, and two built-ins have been re-authored against them, so the "not authorable in
> v3" verdicts scattered through §1–§8 are now wrong for those cases. Specifically:
>
> | Proposal | Status |
> |---|---|
> | **E1 `show`** — visibility window on any draw | **Shipped** (§4.15) |
> | **E2 `env`** — attack/hold/release alpha envelope | **Shipped** (§4.16) |
> | **E3 `line`** — whole-phrase text verb | **Shipped** (§4.17). The optional `spell` follow-up was **not** built. |
> | **E4 `alternate`** — deterministic A/B ping-pong | **Shipped** (§4.18), statement-scoped only; `alternate as NAME` was **not** built. |
> | **E5 `shadow` params + `font` cadence effect** | **Not built.** |
> | **E6 burst-progress export** — the one runtime extension | **Not built.** |
>
> Re-authored built-ins: **`animation`** (the anchor — its still layer is now a real
> trapezoid, 8f in / 17f hold / 8f out / 33f absent, ground-truthed by dumping compiled
> per-frame alpha; 16/16/16 legs cannot fit a 64f clock, so the ramps were traded down to
> preserve the full absence hole) and **`accelerate`** (2048f total, 140-step up-ramp,
> 50% per-image theme swap via `alternate chance 0.5`, restored whole-run lean-in and
> `anim every 4th`).
>
> **The other six built-ins' text lanes have not been re-authored.** That is what this
> document is still *for*: §1–§8's screenplays are derived from the original `visual.cpp`
> at commit `ae7d94c`, which is deleted, and reconstructing them is expensive. Whoever
> finishes #42 should read the screenplay, ignore the "authorable?" column, and check the
> current `builtin_patterns_v3.cpp` instead.

Sources: originals read from `git show ae7d94c:src/trance/visual/visual.cpp` (cited below as
`orig:<line>`); v3 built-ins from `src/trance/visual/builtin_patterns_v3.cpp`; envelope math
derived from the actual render lambdas plus cycler semantics in `cyclers.cpp` (ActionCycler
fires on frame `action_frame` of each period; `OneShotCycler::calculate_active` deactivates a
completed child, which is what makes several originals' "window" gates work); v3 lowering
semantics from `pattern_parser_v3.cpp`, `compiled_visual.cpp`, `render_eval.cpp`,
`pattern_ast.h`. Everything below is derived from those files; the few places I could not
fully verify are labeled **[uncertain]**.

One global fact that shapes the whole diff, established from the parser:

> **v3 render statements have no author-facing visibility gate.** `push_render`
> (`pattern_parser_v3.cpp:264`) gates a draw only on the enclosing *pattern's* `.active`
> (plus burst-index and chance-guard gates the parser synthesizes itself). `RenderStmt.when`
> exists in the runtime (`pattern_ast.h:115`, honored at `render_eval.cpp:70`) but **no v3
> syntax writes it**. Likewise `render_text` takes no alpha (`api.h:86`). Consequence: every
> v3 text draw paints **continuously** for the life of its pattern, and every original
> "text appears for a window, then vanishes" rhythm was **unportable as authored** — the
> ports quietly degraded them. This single missing surface accounts for more felt drift than
> any other cause.

Two more cross-cutting facts:

- **`word` is always SPLIT_WORD.** The v3 parser never sets `Effect.split` (default 0 =
  `SPLIT_WORD`, `pattern_ast.h:59`); there is no surface for `SPLIT_LINE` (whole phrases) or
  `SPLIT_ONCE_ONLY` (the type-out stream). Five of the eight originals used `SPLIT_LINE`.
- **No deterministic theme alternation.** Originals ping-pong primary/alternate on a counter
  (`_alternate = !_alternate`). v3 content is `concept` | `reward` | `runtime` (random per
  fire). The runtime already supports register-driven slots (`Effect.slot_reg`,
  `compiled_visual.cpp:22`) and `Toggle` — but no grammar reaches them.

---

## 1. ACCELERATE

### Intention screenplay (original)

> Start at a lazy 56-frame cut and *keep tightening the screw* to a 12-frame strobe; the
> fast cuts don't just arrive, they **repeat** (the 12f cut fires ~14x back to back) so the
> end state is a sustained strobe, not a fly-by. The whole scene slowly **leans in**: a base
> zoom creeps 0 → 0.4 across the entire run while each cut adds only a small +0.1 pop on
> top. The spiral spins faster in lockstep with the cut rate. Every Nth cut (N rolled once
> per pass: 2/4/8/16) the still is swapped for an **animation burst**. Text is a **line of
> copy stabbed on for the first 8 frames of every other cut**, growing steadily larger as
> the ride accelerates; at top speed it switches to **a fresh single word on every cut**,
> shown the whole cut. Theme flips between concept and reward in four blocks as the speed
> climbs.

Key evidence:
- Cut ramp + repeats: `image_count = 1 + (d^6)/(56^5)` per stage (orig:34-37).
- Zoom: `zoom_origin = .4f * main->progress(); zoom = zoom_origin + .1f * main_image[i]->progress()` (orig:82-83) — **0.1 per-cut pop over a 0→0.4 whole-run creep**.
- Text gating: `_text_on` toggles once per cut (orig:55-58; the 8f text cycler completes then
  goes inactive inside its OneShot, so `main_text[...]->active()` is true only frames 0-7 of
  each cut, orig:91) → **8f stab, every other cut**; `fastest` (<16f cuts) → word per cut.
- Text growth: `render_text(.6f + .2f * main->progress(), ..., zoom, zoom)` (orig:92).
- Animation: every `_animation_mod`-th pull switches to `change_animation` (orig:42-50),
  mod rolled per pass `2 << random(3)` (orig:73-75).
- Spiral: `spiral_speed = 1 + (56 - image_length)/16` (orig:36) — a function of **cut
  length**, not elapsed time.

### v3 (`kAccelerate`) and drift

```
every ramp 56f -> 12f steps 120 ease early -> cut {
  image concept zoom (curve 0 -> 0.5)
  word concept chance 0.5
}
spiral speed (curve 1 -> 4 over accelerate)
```

What the port got right: the cadence ramp itself. `ease early` sampling reproduces the
original's time-at-fast distribution (~25% of runtime at ≤16f cuts) — this is the shipped
§4.10 fix and is fine.

Drifts:
1. **Animation bursts are gone entirely.** No `anim` anywhere in the source. The original's
   "every Nth cut goes live" texture (orig:42-50) is absent. `anim every Nth` exists in v3
   and was simply not used.
2. **Zoom shape inverted.** Original: subtle +0.1 pops on a whole-run 0→0.4 lean-in. v3:
   a violent 0→0.5 zoom on **every** cut, no whole-run creep. The macro-arc (the thing that
   makes it feel like *approach*) is lost; v3 `origin (curve 0 -> 0.4 over accelerate)` +
   `zoom (curve 0 -> 0.1)` would have matched and is authorable **today**.
3. **Text rhythm randomized and flattened.** Original: deterministic 8f stab every other
   cut, a **line** of text, scale growing 0.6→0.8 over the run, theme following the stage's
   alternation. v3: `chance 0.5` (random), full-cut duty, single **word**, constant 0.75,
   concept only. The 8f-stab duty cycle is *not authorable* in v3 (no `when`/`show`
   surface); the line split is not authorable; the growth is authorable (`zoom (curve ... over
   accelerate)`) but wasn't used.
4. **Spiral speed rides time, not cut-rate.** With `ease early` the original's
   speed-follows-cut-length would rise fast early and plateau; v3's linear
   `curve 1 -> 4 over accelerate` lags it. Minor; could ride an eased curve. **[uncertain:
   perceptual significance]**
5. Per-pass re-rolls (anim mod, theme change per wrap, orig:66-76) don't recur in v3 —
   the top-level init re-fires per wrap (Themes/Font/SpiralNew) so theme churn survives, but
   the mod re-roll is moot while (1) stands.

## 2. SLOW_FLASH

### Intention screenplay (original)

> Two-act structure, played twice. **Act 1 (slow, 16×64f):** one image per second-ish,
> every *second* image animates, each zooms 0→0.5 over its life on top of a slow 0→0.25
> lean-in. A strict **call-and-response** each 64f: first half shows the small caption,
> second half swaps it for a **big line of text**. **Act 2 (fast, 32 cuts):** reward-theme
> images strobe at 8f while the zoom **climbs stepwise across the whole act** (index/48);
> a word **blinks** — 8 frames on, 8 frames off — slightly shrinking as the act peaks; the
> caption stays on throughout. Spiral doubles speed (2→4) between acts.

Key evidence:
- Anim every 2nd: `slow_repeat->index() % 2 ? ANIM : NONE` (orig:200-203).
- Call-and-response: caption `frame < 32` (orig:206-208), big text `frame >= 32`,
  `render_text(.8f, .8f, zoom, zoom)` — SPLIT_LINE (orig:210-212, 172).
- Fast zoom climb: `zoom = (fast_repeat->index() + 8.f * fast_loop->progress()) / 48.f`
  (orig:199-200).
- Word blink: `fast_text` = `ActionCycler{16, 8, ...}` changes at frame 8; rendered only
  `fast_text->frame() >= 8` (orig:213-215) → 8f on / 8f off, scale `1 - progress/8`.

### v3 (`kSlowFlash`) and drift

Right: the seq two-phase × loop 2 shape; per-image zoom in slow; whole-phase zoom climb in
fast (`over fast` — good call, the comment even explains why); anim every 2nd; spiral 2/4.

Drifts:
1. **The call-and-response is gone.** v3 slow phase has only `caption concept` painting
   constantly. The big SPLIT_LINE text (the "response") doesn't exist, and the half-cycle
   alternation cannot be written (no `show`, no SPLIT_LINE).
2. **Fast-phase word doesn't blink.** `every 16f { word reward }` paints continuously;
   original is a hard 8f on/off strobe with a shrink creep. Duty gap again.
3. **Fast-phase caption dropped** (original renders small subtext through the whole fast
   act, orig:206-208); v3 fast phase has none — the caption vanishes during the payoff act.
4. Slow-phase base lean-in (`.25f * slow_main->progress()` origin, orig:197-199) lost —
   authorable today via `origin (curve 0 -> 0.25 over slow)`.
5. Slow-phase image theme: original concept (`get_image()`), v3 `concept` — match. Fast:
   original alternate=true, v3 `reward` — match. (No drift; noted for completeness.)

## 3. SUB_TEXT

### Intention screenplay (original)

> A steady 48f pulse of images that **strictly alternates theme** A/B/A/B, every Nth
> (N rolled from 3/5/7 per pass) going animated. Underneath, a **wall of faint repeated
> text** (the subtext, 25% alpha) re-scatters on a cadence that **slows every time the
> visual loops** — 12f on the first pass, 24f, then 48f — an escalation-in-reverse
> (density → stability). On top, a phrase **types itself out one word every 4 frames,
> then goes silent** until the next 96f cycle. Everything rides the image's 0→0.375 zoom.

Key evidence:
- Alternation: `_alternate = !_alternate` per 48f image (orig:104-113).
- Type-out: `text_loop = Seq{ text_reset(4f SPLIT_WORD), Repeat23 × text(4f SPLIT_ONCE_ONLY) }`
  (orig:117-121); `change_text` with `SPLIT_ONCE_ONLY` on an empty queue renders nothing
  (`api.cpp:127-141`) → phrase types out, then blank until reset.
- Cross-loop slowdown: `_sub_speed_multiplier` increments each wrap (orig:88-99); sub0/1/2
  cyclers fire at 12/24/48f gated on the multiplier (orig:123-137).

### v3 (`kSubText`) and drift

```
every 64f { image runtime zoom (curve 0 -> 0.375) anim every 3rd }
every 32f { subtext reward }
spiral speed 4
```

1. **The type-out word stream is gone** — arguably the visual's namesake texture along with
   the wall. Not authorable (needs SPLIT_WORD-then-SPLIT_ONCE_ONLY sequencing; the split
   vocabulary has no v3 surface).
2. **Theme alternation randomized**: `runtime` rolls per pull vs. strict A/B/A/B. The
   conditioning-style pairing (subtext theme follows the image's current theme,
   `change_subtext(_alternate)`) is likewise lost — v3 pins `subtext reward`.
3. **Escalation across wraps lost** (12→24→48f subtext slowdown). No v3 state survives a
   wrap except registers (which do persist — `[uncertain]` whether a counter-based
   emulation would survive the top-level One's wrap; registers are not reset on wrap per
   `CompiledVisual`, so probably yes via an Inc effect — but there is no author surface for
   `inc`).
4. Cadence changed 48f → 64f; anim mod fixed at 3 vs rolled 3/5/7. Minor.
5. Subtext origin no longer rides the image zoom (fixed 0.375 default vs `image_zoom`,
   orig:155). Minor.

## 4. FLASH_TEXT

### Intention screenplay (original)

> A **continuous dissolve chain**: each image fades in over 64f exactly as the previous one
> (now beneath it, full strength) keeps zooming away; the pair-cycle repeats 8 times with
> the **theme flipping every 128f** — image A-theme, then B-theme, text alternating with
> them. **Half the runs** (coin flip per activation) the layers are *animations* instead of
> stills — the moving layer alternates: incoming layer animated for 64f, then it holds as
> the animated base while the next still fades in. A **line of text appears only during the
> second half of each 128f pair** — a reveal that shrinks slightly as it holds — in a
> **fresh font every 64f**. The caption blinks on a 32f-of-64f duty. Spiral is deliberately
> never changed ("too distracting").

Key evidence:
- Coin flip: `_animated{random_chance()}` (orig:222), re-rolled in `reset()` (orig:277-280).
- Anim layer alternation: `!_animated || !image_repeat->index() ? NONE : anim` on `_start`
  vs the mirror on `_end` (orig:257-264).
- Theme flip per 128f unit: `alternate` OneShot action (orig:230-233).
- Text window + creep: `if (image_repeat->index()) render_text(.85f - .05f*progress, .9f - .1f*progress, .75f, .8f - .05f*progress)` (orig:270-272).
- Caption duty: `if (subtext_counter->index()) render_small_subtext(1/5, .25)` (orig:267-269).
- Font churn: `ActionCycler{64, change_font(true)}` (orig:239).

### v3 (`kFlashText`) and drift

Right: the crossfade itself (copy cur→prev, prev zoom 0.4→0.8 under, cur fade-in 0→0.4) is
a faithful port of the dissolve envelope — the flagship §6 mechanism works.

Drifts:
1. **The animated mode is gone** (no `anim` in the pattern). Half of the original's runs
   were a *video* dissolve chain; v3 is always stills. A per-activation coin flip has no v3
   surface (chance is per-fire, not per-pattern).
2. **Bi-thematic ping-pong gone**: original alternates concept/reward every 128f (images
   AND text); v3 pins `image reward` + `word reward` + `caption concept`. FlashText was the
   most explicitly A/B-alternating visual of the eight.
3. **Text: wrong cadence, wrong duty, wrong split, no creep.** Original: one SPLIT_LINE
   phrase per 128f, visible only the second 64f, shrinking as it holds. v3:
   `every 64f { word reward }` — a new single word twice as often, painted 100% of the
   time, constant size.
4. **Font churn lost** (`change_font(true)` per 64f → v3 fonts change only at wrap init).
   The "every phrase in a new face" texture is part of this visual's identity. No v3
   surface for a font effect **[verified: no `font` keyword in `parse_statement`]**.
5. Caption 50% duty lost (constant paint). Spiral: original never re-rolls the spiral —
   v3's top-level init fires `SpiralNew` every pattern (parser hardwires it,
   `pattern_parser_v3.cpp:222`) — the "too distracting" opt-out is not expressible. Minor.

## 5. SIMPLE

### Intention screenplay (original)

> The resting pose. One image per 64f zooming 0→0.5 over its life; every third image is
> pulled from the reward theme, and (on a offset phase) every third showing animates. A
> **line** of text changes every 128f but is only visible during the **middle half** of
> that cycle (frames 32–96) — it surfaces, holds, and sinks — riding the image's zoom.
> Caption always on, refreshed every 32f.

Key evidence:
- Anim/theme interleave: `_image = get_image(_anim_cycle % 3 == 1); if (++_anim_cycle % 3 == 2) change_animation(false)` with double increment (orig:286-292) → reward image every
  3rd, animation every 3rd, phase-shifted.
- Text window: `if (counter->index() == 1 || counter->index() == 2) render_text(.75, .75, .5*image->progress(), .5*image->progress())` (orig:319-321), SPLIT_LINE at 128f (orig:295).

### v3 (`kSimple`) and drift

```
every 64f { image runtime zoom (curve 0 -> 0.5) anim every 3rd }
every 32f { caption concept }
spiral speed 3
```

1. **The big text layer is entirely gone.** Original has BOTH the 128f line-with-window and
   the caption; v3 kept only the caption. Same root cause: line split + window duty are
   unauthorable, so the port dropped the layer rather than paint a phrase permanently.
2. `runtime` vs deterministic every-3rd-reward. Same alternation gap as elsewhere (here the
   original is a 3-cycle, not a toggle).
3. Caption origin 0.5 default vs original 0.25 (orig:318). Cosmetic.

## 6. PARALLEL → v3 `super_parallel` (the second confirmed-suspect)

### Intention screenplay (original)

> Three image lanes staggered a third of a cycle apart, each pulling a fresh image every
> 96f and zooming hard (0→0.875 plus a slow 0→0.125 lean-in) — layered at **1 / 0.5 /
> 0.33** alpha so they read as a stack. But the stack **keeps snapping into focus**: every
> 32 frames, exactly one lane enters a 16-frame **solo window** — all other lanes vanish
> and that lane's image stands alone at full alpha — then the pile dissolves back in.
> Rhythm per 96f: stack(16) → solo-A(16) → stack(16) → solo-B(16) → stack(16) → solo-C(16).
> The animation lives on lane 0, **always** moving, its theme flipping every cycle. A word
> flashes at 50% duty on a 32f cadence, random theme.

Key evidence:
- Solo: `is_single = any_of(single..., active())`; `if (!is_single || single[i]->active())
  ... alpha = is_single ? 1.f : 1.f/(1+i)` (orig:363-373). Lane i's `single` window is
  frames [16,32) of its 96f cycle; offsets 0/32/64 tile the solos every 32f.
- Lane 0 always animated: `anim = i != 0 ? NONE : (_alternate_animation ? ANIM_ALTERNATE : ANIM)` (orig:365-367); `_alternate_animation` toggles per 96f iteration (orig:341-343).
- Text duty: `if (text->frame() < text->length()/2) render_text(...)` (orig:377-379).

### v3 (`kSuperParallel`) and drift

```
every 96f            { image concept -> a zoom (curve 0 -> 0.875) anim every 2nd }
every 96f offset 32f { image concept -> b alpha 0.5 zoom (curve 0 -> 0.875) }
every 96f offset 64f { image reward  -> c alpha 0.33 zoom (curve 0 -> 0.875) }
every 32f { word runtime }
```

1. **The solo/snap rhythm is gone — this is the pattern's soul.** v3's constant alphas
   1/0.5/0.33 produce a permanent undifferentiated pile; the original's defining move (the
   stack repeatedly collapsing to a single full-strength image and re-blooming) is absent.
   The v3 comment even argues the constant alphas exist "so all three actually READ as a
   stack" — treating the symptom the solo windows solved rhythmically. Emulation today
   would need per-lane alpha/`when` exprs over the sibling clocks (`a.frame < 16` etc.);
   alpha-expr emulation is *possible* for images but was not attempted and is illegible.
2. **Lane 0 half-animated instead of always-animated** (`anim every 2nd` vs unconditional
   ANIM with theme toggling per cycle). The constant motion anchor is diluted.
3. Word 50% duty lost (constant paint). Theme `runtime` matches original `random_chance()`
   here — no drift on text theme.
4. 0→0.125 whole-run lean-in origin lost (authorable today).

## 7. ANIMATION (the confirmed anchor)

### Intention screenplay (original)

> The animation **is** the subject: a full-screen moving layer runs continuously, zooming
> 0→0.625 each 64f, its source alternating concept-anim / reward-anim every 64f. Above it,
> a still image **visits**: per 64f cycle it fades **in over 16f** (frames 48–64, zoom
> drifting up), holds full through the cycle boundary, fades **out over 16f** (frames
> 0–16), and then is **fully absent for 32 frames — the animation holds the stage alone**
> before the next visit. The first and last 32f of the whole visual are kept clean (no
> still at all). A big line of text shows only the **first half** of each 64f; the caption
> is always on. 
>
> Envelope of the still, per 64f cycle, phase-aligned to the visit:
> `alpha: 0 →(16f in)→ ~1 →(boundary)→ ~1 →(16f out)→ 0, then 32f OFF` — a **trapezoid
> with a genuine hole**, not a triangle.

Key evidence: orig:423-431 —
```cpp
if (change_counter->frame() < 16 && start_end_timer->index() == 1) {
  auto t = 15 - change_counter->frame();
  api.render_image(_current, std::min(1.f, t / 16.f), .5f, .625f + .125f * frame/16.f);  // fade OUT, f0-15
}
if (change_counter->frame() >= 48 && start_end_timer->index() == 1) {
  auto t = change_counter->frame() - 48;
  api.render_image(_current, std::min(1.f, t / 16.f), .5f, .5f + .125f * t/16.f);        // fade IN, f48-63
}
```
Frames 16–47: **no still drawn at all** (32f hold for the animation). Text window:
`if (!change_counter->index()) render_text(...)` (orig:434-436). Intro/outro:
`start_end_timer->index() == 1` (orig:418-419).

### v3 (`kAnimation`) and drift

```
every 64f { image runtime zoom (curve 0 -> 0.625) anim }
every 64f offset 32f { image reward -> still fade inout zoom (curve 0.5 -> 0.625) }
every 32f { caption runtime }
```

1. **CONFIRMED (user-reported): `fade inout` = triangle over the whole 64f** —
   `alpha = 1 - |2p - 1|` (`pattern_parser_v3.cpp:498`). The still is on screen at nonzero
   alpha ~every frame; the animation never holds alone; the peak is an instant, not a hold.
   Original: 16f in / hold across boundary / 16f out / **32f absent**. The correct envelope
   on the offset lane's clock is: triangle over the *first half* only, zero for the second
   half (the lane starts at +32, so its p∈[0,0.5) spans base frames 48→16 — in, boundary
   hold, out — and p∈[0.5,1) is the absence). Authorable today only as a raw alpha
   `[expr]`: `alpha [max(0, 1 - abs(4 * this.progress - 1))]`.
2. **Big-text half-cycle reveal lost** (again: no line split, no window). v3 keeps only the
   caption.
3. ~~**Anim alternation randomized**~~ (closed): the original strictly alternates ANIM /
   ANIM_ALTERNATE per 64f (change/change_alt seq, orig:398-404), and `image runtime ...
   anim` only re-rolls the theme per pull. The deterministic form is `image alternate ...
   anim` (E4): the synthesized toggle flips every firing, the image pull and the animation
   load both read it, and the draw follows the load via `Registers::anim_slot`. (This entry
   used to point at an `anim_alt` expr on the RenderStmt as the mechanism. That field was
   declared but never parsed, and while it existed the draw ignored the load entirely --
   every `anim` came off the primary streamer. It is gone; the slot the load resolved is
   the single answer to "which animation is on screen".)
4. Clean intro/outro (32f no-still bookends) lost. Minor.

## 8. SUPER_FAST

### Intention screenplay (original)

> A relentless 8f cut engine — but not hard cuts: each cut's **last 4 frames pre-echo the
> next image**, alpha ramping 1/5→4/5, so the strobe smears forward. Each cut has a tiny
> zoom pop (0.0625→0.1875). Every 4th cut, a **word stabs on for exactly that one cut**
> (25% duty, periodic). At random (1/12 per cut, 64f cooldown), the pattern **tears into an
> animation burst** for 64–128f: the animation **zooms in continuously across the burst**
> (0→~1), words go silent, and — crucially — entering a burst **flips the theme**, so each
> burst is a pivot between concept-world and reward-world for all the cuts that follow.

Key evidence:
- Pre-echo: `if (rapid->frame() >= rapid->length() - 4) render_image(_next, next_alpha, ...)`,
  `next_alpha = (5 - 8 + frame)/5` (orig:500, 510-517).
- Cut zoom pop: `image_zoom = .125f * (.5f + rapid->progress())` (orig:501).
- Text: `_text_mod` cycles 0-3; change+render only at `_text_mod == 0` (orig:462-464, 519-521).
- Theme pivot: `_alternate = !_alternate; api.change_animation(_alternate)` on burst entry
  (orig:483-486); all subsequent pulls use `_alternate`.
- Burst zoom crescendo: `anim_progress = (8*(16 - _animation_timer) + rapid->frame())/128`
  (orig:499, 503-506).

### v3 (`kSuperFast`) and drift

The burst FSM shape itself (period 8f, chance 1/12, cooldown 64f→8 ticks, duration
64..128f) is a faithful port — the §4.11 surface works, including `enter { anim runtime }`
one-shot animation pick and index-gating.

Drifts:
1. **Hard cuts — the pre-echo dissolve is gone.** Authorable today (pull into `next`, copy
   to `cur`, draw `next` with `alpha [max(0, (this.progress - 0.5) * 2)]`-style tail ramp)
   but wasn't attempted.
2. **`zoom 0.15` constant** — no per-cut zoom pop. The file's own header comment ("a
   constant zoom is a static magnification... zoom modulators here are curve rides, never
   constants") is violated by its own 8th pattern.
3. **Burst zoom crescendo lost** (`draw cur zoom 0.4 anim` flat vs 0→1 ramp across the
   burst). NOT authorable today: nothing exports burst-elapsed — `BurstCycler::index()` is
   only 0/1, and the burst duration is rolled at runtime. This is the one drift in the
   whole set that genuinely needs a **runtime extension**.
4. **Theme pivot on burst lost**: `image runtime` re-rolls every cut; original holds one
   theme between bursts and flips at each burst. Needs deterministic toggle (see verdict).
5. Words keep firing during bursts (the `every 8f word` lane is outside the burst's
   index-gating); original silences text in bursts. Fixable by moving the word into
   `base { }`. Text duty is `chance 0.25` (random, persists the whole 8f — close) vs
   periodic every-4th — acceptable approximation, flag only.

---

## Systemic drift summary

The "authorable?" column below is **as of the audit**, before E1-E4 shipped. Read it as the
argument for building them, not as the current state of the grammar.

| # | Drift class | Visuals hit | Authorable at audit time? |
|---|---|---|---|
| S1 | Text duty windows / blinks / reveals (on-off rhythm) | ALL EIGHT | **No** — no `when`/`show` surface, no text alpha |
| S2 | SPLIT_LINE phrases (and SubText's type-out stream) | accelerate, slow_flash, flash_text, simple, animation, sub_text | **No** — split unreachable |
| S3 | Hold/trapezoid envelopes with true absence | animation (anchor), super_fast pre-echo tail | Images: yes via raw `[expr]` alpha (illegible); text: no |
| S4 | Solo / ducking of stacked lanes | super_parallel | Images: yes via `[expr]` alpha referencing sibling clocks (very illegible) |
| S5 | Deterministic A/B theme alternation (and burst theme pivot) | sub_text, flash_text, accelerate, animation, super_fast | **No** — runtime has `slot_reg`+`Toggle`, no syntax |
| S6 | Whole-run lean-in under per-cut envelopes (origin creep) | accelerate, slow_flash, super_parallel | **Yes** — `origin (curve ... over PATTERN)`; ports just didn't |
| S7 | Dropped animation modes (accelerate bursts, flash_text coin-flip runs, super_parallel always-anim lane) | accelerate, flash_text, super_parallel | Partly (`anim`, `anim every Nth`); per-activation coin flip: no |
| S8 | Burst-progress-driven params (crescendo) | super_fast | **No** — runtime extension needed |
| S9 | Text shadow riding image zoom; font churn; caption origins | most | **No** shadow/font surface (fields exist in RenderStmt/Effect) |
| S10 | Cross-wrap escalation (sub_text slowdown) | sub_text | No author surface for `inc`; registers do persist across wraps |

The pattern behind the pattern: **wherever the runtime already had the capability but the
grammar had no surface (when-gates, split types, slot_reg, shadow params, font effect), the
ports silently dropped or flattened the behavior instead of flagging it.** The v3 built-ins
are honest about what they kept, but the *feel* casualties cluster exactly on the missing
surfaces, not on missing runtime.

---

## Grammar verdict: (b) small extensions — not a v4

**The v3 model is not the problem.** Every drift above lowers to the existing runtime —
nearly all of it to `RenderStmt` fields that already exist (`when`, `alpha` exprs, `split`,
`slot_reg`, `shadow_*`) and are evaluated by machinery that already runs every frame. The
two-nouns/one-rule spine (pattern clock + modulator) *correctly describes* holds, duty
windows, solos and alternation; what v3 lacks is the **vocabulary** — which is precisely
the "what a lightshow operator would say" layer. A lightshow operator's nouns are:
intensity **envelope** (attack/hold/release), **duty/gate**, **solo/dim**, **chase**
(v3 already has this: `offset`), and **color flip** (alternation). None of these breaks the
model; all of them lower to strings the runtime already evaluates. A v4 that made these
first-class nouns (the q2-response "Signal/Presentation shape" direction) would re-skin the
same lowering at the cost of a rewrite — and the compile-down floor plus modding north star
both argue for the smallest surface that makes the eight authorable *legibly*. Verdict:
**rewrite the built-ins + five parser-only extensions + one small named runtime extension.**
Reserve the v4 question; reopen it only if the extension list keeps growing past this set.

### Proposed extensions (each: syntax → semantics → lowering → floor check)

**E1. `show` — visibility window on any draw (the load-bearing one).**
```
word concept show 0.5..1        # fraction of the enclosing clock
image reward show 0f..8f        # frame-denominated, resolved against the clock's length
word concept show [expr]        # raw condition escape
```
Semantics: the draw paints only while the condition holds; content/effects still fire on
their cadence. Lowering: `RenderStmt.when = "(clk.progress >= A) and (clk.progress < B)"`
(or `clk.frame < N`), ANDed with the existing pattern-active gate — **zero runtime change**
(`when` is already honored at `render_eval.cpp:70`). Restores S1 across all eight; also the
correct primitive for Animation's absence hold on text-like layers. Floor: clean.
Lightshow name: the gate/duty knob.

**E2. `env` — attack/hold/release alpha envelope (supersedes bare `fade inout` where a hold
is meant).**
```
image reward -> still env in 16f hold 16f out 16f     # remainder of the clock = OFF (alpha 0)
image concept env in 1/4 out 1/4                      # fractions allowed; no hold = triangle
```
Semantics: piecewise-linear alpha: rise over `in`, flat 1 over `hold`, fall over `out`,
**0 for the remainder** — trapezoid-with-absence, the exact original Animation shape
(in 16f, hold ~16f spanning the wrap, out 16f, off 32f, phase-set by the lane's `offset`).
Lowering: one alpha `[expr]` of nested `min`/`max` over `this.progress` — compile-time
string sugar, **zero runtime change** (same class as `fade in/out/inout`, §4.3). Floor:
clean. Validation: `in+hold+out <= clock length` is a parse error. Lightshow name: ADSR
minus the S.

**E3. `line` draw verb (+ optional `spell`).**
`line <content>` = the `word` statement with `Effect.split = SPLIT_LINE` — one token of
parser work; the field already exists and `change_text` already implements it. Optional
follow-up `spell <content> every Nf` for SubText's type-out (lowers to the reset/advance
pair using SPLIT_WORD + SPLIT_ONCE_ONLY — both already in the enum, `api.h:24`). Floor:
clean. Restores S2.

**E4. `alternate` content word — deterministic A/B ping-pong.**
```
image alternate          # flips primary/alternate each firing of this statement
anim alternate
```
Lowering: parser synthesizes a hidden scalar register + `Effect{Kind::Toggle}` fired before
the draw's pull, and sets `Effect.slot_reg` to it — **zero runtime change**
(`resolved_slot` already reads `slot_reg`, `compiled_visual.cpp:22-24`; `Toggle` already
exists). Scope the register to the statement so two alternating draws in one pattern can
share phase via an optional `alternate as NAME` / `alternate with NAME` — decide at
implementation; the bare form covers sub_text/flash_text/animation. For super_fast's
burst-pivot, allow the standalone toggle in `enter { }`: `alternate NAME` fires the toggle,
and draws elsewhere say `image with NAME` **[design detail open — smallest form that covers
the burst pivot without reopening §4.7's rejected `set/inc/roll` surface]**. Floor: clean.
Restores S5. Bi-thematic invariant untouched (still the one alternate bool).

**E5. `shadow` params on text draws (+ `font` cadence effect).**
`word concept shadow zoom (curve 0 -> 0.5) shadow origin 0.2` → fills the existing
`RenderStmt.shadow_origin/shadow_zoom` (currently unreachable, default 0). And a standalone
`font` statement (`font` / `font force`) lowering to `Effect{Kind::Font}` — the effect
exists, no surface does. Both zero-runtime. Restores S9 (text depth-parallax riding the
image zoom; flash_text's font churn).

**E6. Burst progress export — the one RUNTIME extension (named per §9 discipline).**
`BurstCycler` gains `burst_frame()`/`burst_length()` (elapsed ticks × period, and the
rolled duration × period); `resolve_ident` exposes them as `NAME.burst_progress` (0 outside
a burst). Cost: two getters + one resolver case; no new node types, no scheduling change.
Unlocks `burst { draw cur zoom (curve 0 -> 1 over rapid.burst) anim }`-style crescendos
(exact syntax: just let `[rapid.burst_progress]` be readable from any expr — no new
modulator kind needed). Restores S8.

Explicitly NOT proposed: a solo keyword (S4 lowers to E1 `show` on the solo lane + alpha
`[expr]`s on siblings; if the rewritten super_parallel source proves unreadable, revisit a
`solo Af..Bf` cadence modifier that compiles to exactly those strings — sugar-only,
zero-runtime, defer until the source exists); per-activation coin flips (flash_text's
`_animated` — accept the drift or approximate with `chance`; a `mode`/named-roll surface is
YAGNI until a second pattern wants it); any third theme; any text register (Ext#4 stays
deferred and none of the above needs it).

---

## Prioritized work list (smallest change that restores each intent)

Ordering: grammar surfaces first where ports were *forced* to degrade, then pure re-authoring.
Items 1-3 and 5 have shipped as grammar surfaces; the re-authoring they unblock is mostly
still outstanding (see the status box at the top). What remains open is: the text lanes of
the six built-ins other than `animation` and `accelerate`, plus items 6, 7 and 8 entirely.

1. ~~**E1 `show`** (parser-only).~~ **Shipped.** Then re-author the text lanes of all eight: slow_flash
   call-and-response (`caption ... show 0..0.5` + big text `show 0.5..1`), simple's middle
   window, flash_text second-half reveal, parallel/animation half-duty, accelerate 8f stabs,
   super_fast in-`base` words. Biggest felt win per line of code in the repo.
2. ~~**E3 `line`** (parser-only, ~10 lines).~~ **Shipped** (`spell` was not). Re-point the five SPLIT_LINE sites; upgrade the
   dropped big-text layers in simple / slow_flash / animation / flash_text from `caption`
   back to real text.
3. ~~**E2 `env`** (parser-only sugar).~~ **Shipped, and the anchor is fixed.** Was: `animation`'s still becomes
   `env in 16f hold 16f out 16f` on the offset lane — restores the 32f animation-alone hold.
   (Interim zero-grammar fix if wanted today: `alpha [max(0, 1 - abs(4 * this.progress - 1))]`.)
4. **Re-author with existing grammar (no extensions needed):**
   - accelerate: add `anim every 4th`, split zoom into `origin (curve 0 -> 0.4 over
     accelerate) zoom (curve 0 -> 0.1)`, ease the spiral curve.
   - slow_flash / super_parallel: add the whole-run origin creeps.
   - super_fast: per-cut `zoom (curve 0.06 -> 0.19)`; pre-echo via `-> next` + `copy` +
     tail-ramp alpha `[expr]`; move `word` into `base { }`.
   - super_parallel: `anim` (unconditional) on lane a; solo windows via `show`/alpha
     `[expr]`s (see E1/S4 note).
5. ~~**E4 `alternate`** (parser-only)~~ — **shipped** (statement-scoped; `alternate as NAME` was not built). Still to apply: sub_text A/B images + subtext-follows-image,
   flash_text 128f theme flip, animation's anim alternation, super_fast burst pivot.
6. **E5 `shadow` + `font`** (parser-only) — **not built**: text depth-parallax everywhere the originals
   passed image zoom as shadow args; flash_text font churn.
7. **E6 burst progress** (runtime, small) — **not built**: super_fast burst zoom crescendo.
8. Leftovers, explicitly deprioritized: sub_text type-out (`spell`, E3 follow-up) and
   cross-wrap slowdown; flash_text per-activation animated mode; animation clean
   intro/outro bookends; flash_text spiral-never-changes opt-out. Each is real but small;
   none blocks the above.

Each numbered item is independently shippable and keeps the compile-down floor intact; only
item 7 touches the runtime, and it is named, bounded, and in the spirit of §9's shipped
extensions.
