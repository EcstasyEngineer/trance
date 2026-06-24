# v2 grammar — validated direction & next steps

Fresh-eyes synthesis of the two GPT Pro responses (`q1 response.md`, `q2 response.md`),
filtered and re-weighted against (a) the actual code as it stands today, (b) the
git-history forensics on the original repo, and (c) the two things the framing under-served:
"are we actually getting *simpler*" and "animation is fundamentally different."

This is a **direction to validate**, not a build order to execute blindly. Nothing here is
actioned.

> **Update — Phase 0 done on paper.** `phase0-validation.md` works all 8 built-ins against the
> real sources and resolves the terminology. Two canonical renames adopted there and applied
> below: **voice → stream** (in a hypnosis/audio context "voice" reads as spoken audio) and
> **signal → curve** / **phase (time-section) → section**. Verdict: conditional go — expressible,
> blocked only on the animation model (§5) and a replacement validation story.

---

## TL;DR verdict

- **The core is right and the two responses agree on it** — that agreement is the signal.
  Authoring surface = **streams** (content streams; was "voices") + **sections** (named time;
  was "phases") + **curves** (time-varying values; was "signals") + **theme relationships**
  (index, not primary/alternate) + **render shapes** (`focus`/`fade`/`stack`/`cut`). It lowers
  to today's `pattern::Node` AST → Cyclers.
  **Do not rewrite the runtime.** Both responses are emphatic and correct here.
- **The history says we're recovering real intent, not inventing it.** The 8 visuals were
  chosen by early 2015 and never changed for 3+ years; only their *names and mechanics*
  churned. So an intent grammar's job is to *name what the author already meant* — the safest
  possible footing for a redesign.
- **Three things to trim as over-engineered (for now):** the separate **Score IR** layer, the
  **3-tier escape hatch**, and the **5th `field` shape**. Defer all three until something
  concretely demands them.
- **One real gap both responses share:** **animation is modeled as a modifier hanging off the
  image voice.** That works for `simple` and breaks for `animation` and `super_fast`. This
  needs explicit design before committing (see §5).
- **"Simpler" is not free.** Some v2 examples (esp. `accelerate`) trade structural verbosity
  for dense modifier soup. Make readability a measured gate, not an assumption (see §6).

---

## 1. The validated core (high confidence — both responses converge)

These I'd treat as decided, because Q1 and Q2 arrived at them independently and they match the
roadmap and the code:

1. **Voices/lanes replace registers as the author's content unit.** `image img theme 0 every 64`
   instead of "write register `current`, read it in a render expr." (`q1` §1, `q2` §1.2)
2. **Phases replace `seq`/`par`/`one` as the author's time unit; statements in a block are
   parallel by default.** `seq/par/one` stay as IR concepts only. (`q1` "why", `q2` §1.1)
3. **`init` (scoped enter) replaces the `one { every 1: setup; body }` idiom** — and it must be
   *nested* enter semantics, not just frame-0. (both)
4. **Signals are first-class** — `ramp`/`map`/`pick`/`chance`/`jitter` — and one named signal
   drives cadence *and* spiral speed *and* zoom *and* anim probability. This is the keystone:
   it deletes `generate`-unrolling (accelerate's 45 segments → one ramp) and most of the
   scalar-register state machine. (both; `q2` §1.3 is the sharper write-up)
5. **`pick`/`choice` (sample once) vs `jitter`/`random` (resample) is a required distinction** —
   without it you re-invent registers to say "picked once." (both)
6. **Theme *index*, not primary/alternate** — and this is a real ThemeBank/API runtime project,
   not parser work. Grammar accepts `theme N`; compiler rejects `N>2` until the bank supports it.
   (both, emphatically)
7. **Render shapes, not render expressions, as the surface** — `focus`/`fade`/`stack`/`cut`
   lower to today's `RenderStmt` list via the existing `render_eval`. (both)
8. **Keep v1 forever; version v2; don't auto-migrate stored patterns.** Built-ins are
   compile-time so they can move freely; custom patterns stay v1. (both)

**The current AST survives as the lowering target.** Everything above desugars into
`pattern::Node` + the data-driven render we just built. That means v2 is a *front-end*, not a
rewrite — and it can be validated one built-in at a time, exactly like the render-as-data port.

---

## 2. Why the intent bet is grounded (the history check)

This is the piece neither response had, and it changes how much to trust the direction:

- The original author locked the set of 8 felt-effects by **early 2015 and never changed it for
  3+ years** — zero abandoned visuals. So the *intents are real and stable*; they are not
  artifacts. (My earlier "costume" framing was wrong at the set level.)
- What churned was the **abstraction** (imperative `*Program` → Cycler API port in a 7-commit
  burst, Jan 2017) and the **naming** (`Rapid`→`SuperFast`, the `Parallel`/`Simple` muddle).
- So the author's journey was **imperative → cyclers → [stopped]**. We continued it
  (cyclers → data). **Intent is the next station on the same line, not a leap.**
- The naming churn is the tell that the author had clear *visceral* effects but a muddy
  *conceptual* model — which is exactly the gap an intent grammar fills. `q2`'s renaming of
  `simple`/`PARALLEL` to "steady focus, every-third accent" is doing the job the author never
  finished.

Implication: **trust the intent identities; be skeptical of the exact parameters.** The 2017–18
tail of the original history is hand-tuning cadences/zooms by eye — those numbers are
disposable, which is *why* dropping byte-identity is safe.

---

## 3. Q1 vs Q2 — what differs, and how I'd resolve it

| Dimension | Q1 (readability) | Q2 (intent) | My call |
|---|---|---|---|
| Surface unit name | `lane` | `voice` | **`stream`** (revised in `phase0-validation.md` §1 — "voice" reads as spoken audio for this audience); `lane` stays the IR term. |
| Layers | Surface → Frame IR → Cyclers (3) | Surface → **Score IR** → Frame IR → Cyclers (4) | **Start with 3 (Q1).** Add Score IR only when the overlay needs it (§4). |
| Framing | "lane language" | "visual score language" (phases/voices/signals/theme-rel/shape) | **Q2's object model + Q1's concrete grammar/lowering.** They're complementary, not competing. |
| `intent "..."` annotation | absent | present | **Adopt it** — cheapest "self-documenting" win regardless of grammar density. |
| Escape hatch | "keep raw `render{}`/v1 as advanced" | 3 tiers (native gesture / mechanics / render.raw) | **One hatch:** a raw v1 `mechanics { }` block. Defer the elaborate tiers. |
| Shapes | 4 (`focus/fade/stack/cut`) | 5 (adds `field`) | **4.** Fold `field` into `focus` until a pattern proves it needs its own. |

Net: Q2 is the better *mental model*; Q1 is the better *first implementation plan*. The synthesis
uses Q2's ontology and Q1's "lower straight to the existing AST, don't build new IR yet" pragmatism.

---

## 4. Over-engineering to cut (for now)

Each of these is defensible long-term but violates YAGNI for a solo, no-CI, validate-by-eye project:

- **Score IR as a separate compiled layer (Q2 §7–8).** Its only hard justification is overlay
  narration. If the overlay isn't the immediate priority, lower the surface *directly* to the
  Frame IR and skip it. Re-introduce it the day the overlay needs source-span highlighting.
- **The 3-tier escape hatch (Q2 §6).** `native gesture ... exports ... described_as` is
  speculative ceremony. Ship one escape: an inline v1 block. That already covers
  `super_fast`'s FSM if you decide to keep it exact.
- **`field` shape (Q2 §1.5).** Fifth shape with no pattern that strictly needs it. Make
  subtext-texture a `focus` parameter first.
- **`feel <mood>` compiler semantics.** Keep mood words as metadata only (Q2 already concedes this).
- **Theme K>2 runtime, built ahead of need.** Accept the syntax, reject `N>2` at compile, and
  scope the ThemeBank/loader redesign as its *own* project with its own go/no-go.
- **Non-divisible cadences inside a fixed `cycle` (Q1 §4.1).** Q1 itself says "reject first."
  Agreed — keep the integer-schedule model clean; don't build masked-phase machinery yet.

---

## 5. The animation problem (the gap both responses glossed)

You flagged this and you're right: **both responses treat animation as a modifier on the image
voice** (`image ... anim every 3rd`, `with anim ...`). That is faithful to the *engine* — at the
API level animation is "render this slot's content as its animated form instead of a still"
(`render_animation_or_image(Anim type, image, …)` in `api.h`; there is no standalone animation
draw). So modeling it as an image-voice modifier is *mechanically* correct.

But it's *intentionally* wrong for two of the eight:

- **`animation`** — here the animation **is the subject**, not an accent on an image lane. The
  pattern is "animation-dominant with backup/current fade windows." A modifier hanging off an
  image voice can't say that.
- **`super_fast`** — the animation is a **burst overlay decoupled from the cut stream** (the FSM
  fires anim independently of the rapid image cadence). Coupling it to the image lane's cadence
  loses the decoupling that *is* the effect.

The other two anim users are fine as modifiers: **`simple`** (every-3rd accent on the image) and
**`super_parallel`** (anim on lane 0 only, alt-toggle per lap).

**Recommendation — make animation a first-class voice that *binds* to a slot but has its own
cadence/gating/theme:**

```
voice cuts:  image theme 1 -> current every 8
voice flash: anim  of cuts theme alternating(0,1) burst(chance 1/8, dur 8..16, cooldown 16)
```

i.e. an `anim` voice references which slot's animated form to show, but decides *independently*
when and with which theme. The render shape then decides compositing (does the animated form
replace the still, overlay it, etc.). For `simple` you can still sugar it as `image ... anim
every 3rd`; that's just the special case where the anim voice shares the image voice's cadence.

**This must be designed explicitly before committing to the grammar**, with all four anim patterns
(`simple`, `animation`, `super_parallel`, `super_fast`) as the test set. If the model can express
all four cleanly, animation is solved; if not, the breakages are the spec. Do not let it stay a
hand-wave — it's the one place both external reviews were weakest.

---

## 6. "Is it actually simpler?" — make it a gate, not a hope

Your instinct that "simpler" wasn't fully captured is correct. `super_parallel` genuinely
collapses (30 lines → 6) — that's the win. But `accelerate`'s v2 (Q1) is dense:

```
image img theme bands(len: 56..48=1, 47..36=0, 35..24=1, 23..12=0) every len
      anim every pick(2,4,8)th; upload every len @half while len > 24; spiral rate ramp(1 -> 3.75 by len)
```

That trades *structural* verbosity for *modifier* density. Fewer lines, but not obviously fewer
*concepts to hold*. So I'd make readability measurable rather than assumed:

- **One-sentence test:** a reader who doesn't know the pattern can state its intent in one
  sentence after reading it. The `intent "…"` annotation makes this nearly free and should be
  mandatory on built-ins.
- **Concept-count cap:** if a single voice line carries more than ~3 modifiers, that's a smell —
  push the complexity into a named `signal` (which is exactly what signals are for).
- **Validate "simpler" on the hard cases, not the easy one.** `super_parallel` proves the upside;
  judge the grammar on `accelerate`, `sub_text`, and `animation`, which are where density hides.

---

## 7. New affordances this unlocks (the wishlist upside)

Worth stating so the case isn't only "smaller" — the same redesign *grows* the space:

- **3+ themes** (theme-index over K live themes) — impossible today (binary slot).
- **Cross-theme pairing / associative conditioning** — `image theme 0` with `text theme 1` on the
  same beat becomes first-class (`same(voice)`, `other(voice)`).
- **New cadences** — `jitter(2..8)` (true per-fire variation, not just a captured constant),
  `chance(p)`, `burst(...)` as a general primitive.
- **Shared signals** — one `tension` ramp driving cadence + zoom + spiral + anim-rate together
  (currently each is hand-coded separately).
- **Legible introspection** — if/when Score IR lands, the F1 overlay can narrate intent instead
  of cycler internals.

---

## 8. Recommended sequence (de-risked, validate-by-eye between each)

Front-loads high-value/low-risk, defers expensive/uncertain. Each phase ships something you can
watch run.

- **Phase 0 — paper validation (no code).** Hand-write all 8 built-ins in the proposed voice/
  shape/signal surface (Q2's intent versions are a strong start). Check: one-sentence test per
  pattern; where each *doesn't* fit; resolve the animation model (§5) against all 4 anim patterns.
  Output: a frozen mini-spec + an explicit fidelity-loss list. This is the cheapest possible
  go/no-go and it directly tests "simpler."
- **Phase 1 — signals, into the *current* grammar.** Implement `ramp`/`map`/`pick`/`chance` as a
  signal concept that lowers to existing registers/`divide`/`roll`/`pulse`. This alone kills
  `generate`-unrolling and most of the state machine — and it needs no new surface syntax, so
  it's the lowest-risk high-value step. Validate: `accelerate` and `sub_text` get dramatically
  shorter with identical-enough output.
- **Phase 2 — render shapes.** Implement `focus`/`fade`/`stack`/`cut` as functions emitting
  current `RenderStmt` lists; reuse `render_eval` wholesale. Validate all 8 render through shapes.
  This is the riskiest *fidelity* bet — do it early so breakages surface early.
- **Phase 3 — theme-index (`ThemeRef`).** Generalize `Slot`→`ThemeRef`; accept `theme N`; reject
  `N>2`. Scope the ThemeBank K>2 runtime as a *separate* project (don't build it speculatively).
- **Phase 4 — the intent surface grammar.** Voices + phases + `init`, lowering to Frame IR using
  signals (P1) + shapes (P2) + themes (P3). Add the v2 marker; move built-ins to v2; custom stays
  v1. Mandatory `intent "…"` per pattern.
- **Phase 5 — (optional) Score IR + overlay narration.** Only if/when the overlay is a priority.

Note: this order means you get ~70% of the felt simplification (signals + shapes) **before**
writing a single line of new grammar — which is the right way to de-risk a language change you
can only validate by eye.

---

## 9. Decisions to make before building

1. **Animation model** (§5) — first-class voice vs image modifier. Blocking; needs your call.
2. **Score IR — in or deferred?** I recommend deferred; confirm the overlay isn't a near-term
   priority.
3. **How much fidelity loss is acceptable, per pattern?** Phase-0's loss list needs your sign-off
   — especially `super_fast` (loses the FSM) and `accelerate` (loses exact dwell + zoom wobble).
4. **`super_fast` FSM — kill or keep behind the escape hatch?** Both responses say kill it under
   the 90% rule; the escape hatch exists if you want it exact.
5. **Is "simpler" or "more expressive" the primary win?** They mostly align, but where they
   conflict (accelerate's modifier density), which wins decides the grammar's character.

---

## Files this synthesis draws on (already in the repo)

- `gpt-pro/q1 response.md`, `gpt-pro/q2 response.md` — the two reviews.
- `src/trance/visual/builtin_patterns.cpp` — the 8 patterns (schedule + render blocks) as they
  stand post-port; the subject of any v2 re-expression.
- `src/trance/visual/pattern_ast.h` — the AST that becomes the Frame IR / lowering target.
- `src/trance/visual/pattern_parser.h` — current grammar (the thing being raised above).
- `src/trance/visual/render_eval.h` / `.cpp` — render-as-data; render shapes lower into this.
- `src/trance/visual/api.h` — `VisualControl`/`VisualRender`: the real primitive vocabulary,
  and the reason animation is mechanically an image-render mode (§5).
- `src/trance/visual/cyclers.h`, `pattern_compiler.h` — the runtime the surface still lowers to.
- `docs/roadmap-grammar-v2.md`, `docs/roadmap-f1-overlay.md` — prior thesis + the overlay/Score-IR tie-in.
