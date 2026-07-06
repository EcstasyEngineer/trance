# GPT Pro — Q2: Make the grammar express *intent*, not per-frame mechanics

## Your role
DSL / language designer. This is an open **design-synthesis** problem — propose freely.
Don't anchor on my vocabulary; if my framing is wrong, say so. This is the deeper of
two questions (Q1 is about surface readability; this is about the model itself).

## What the system is (short)
`trance` is an audiovisual engine. A **visual** is a *pattern* authored as text, parsed
to an AST, compiled to a `Cycler` timing tree, and played frame by frame. A pattern is
now fully data: a timing schedule, an imperative register state machine, and a
`render { }` block. Byte-identity with the original (now-deleted) C++ visuals has been
abandoned; the target is ~90% of the *felt* behaviour.

## The core problem — the disconnect I want you to attack
The grammar **and the AST underneath it are per-frame *mechanism*.** Every leaf is
`every N : <effects>` — "every N frames, fire these low-level actions." The actions are
per-frame modifiers: pull an image, change the text, rotate the spiral by a rate, fire
an animation, toggle a register. (See `src/trance/visual/api.h` — `VisualControl` is the
whole primitive vocabulary; `cyclers.h` is the timing model.)

That is **objectively correct for a low-level engine** — frame-accurate, total control.
**But it encodes HOW, never WHAT or WHY.** Reading a pattern tells you the per-frame
choreography; it does not tell you the *intent* — the experience the pattern is trying
to create. I believe the grammar should be **about intent**, with the per-frame schedule
as a *lowering target* underneath it, not the authoring surface.

Concretely, the 8 built-ins each *have* an intent that the current grammar hides:
- `slow_flash` — "build slowly, then flash fast" (two phases, escalating).
- `accelerate` — "image cuts that speed up over time."
- `super_parallel` — "overload: 3 simultaneous theme streams, interleaved."
- `super_fast` — "rapid cuts with occasional animation bursts."
- `sub_text` — "one theme with escalating subtext density."
- (and slow/flash/simple/animation variants.)

An author should be able to *say that*, and have it lower to a frame schedule — instead
of hand-authoring the frames and hoping the intent emerges.

## The question
**How would you design a grammar that expresses pattern *intent* rather than per-frame
mechanics — sitting above the current per-frame AST as a lowering target?**

Sub-questions I care about:
1. What is the **intent vocabulary**? (phases? tension/escalation curves? "streams" or
   "lanes" of content? density/cadence as intent-level knobs? conditioning-style
   pairings like "theme-A image with theme-B text"?)
2. How do the **8 built-ins map** onto that vocabulary? Where does a built-in's intent
   *not* fit cleanly — and is that a sign the built-in is incidental rather than
   intentional?
3. How does intent **lower** to the existing per-frame AST (schedule + effects +
   render block) and then to Cyclers? Give a concrete lowering sketch for one pattern.
4. Where does intent **break down** — what genuinely needs a low-level, per-frame
   escape hatch, and how do you expose it without it becoming the default?
5. Relationship to Q1: is "intent grammar" the same thing as "readable grammar" at a
   higher altitude, or are they two distinct layers (intent surface → readable
   mid-level IR → per-frame AST → Cyclers)? Argue for a layer count and name them.

## What I want back
- A proposed **intent-level model + grammar** (concepts first, then syntax).
- The **8 built-ins expressed as intent** (even if approximate).
- A **lowering sketch** intent → current AST for at least one non-trivial pattern.
- An honest **boundary analysis**: what intent can and cannot capture here.
- A recommended **layering** (how many languages/IRs, and why) — including whether the
  current per-frame AST survives as the IR or should itself change.

## Worth your attention
- `docs/roadmap-grammar-v2.md` already gestures at a "lanes + render-shapes" reframe and
  declarative modifiers. Treat it as one input, not the answer.
- `docs/roadmap-f1-overlay.md` — the debug overlay wants to *show* what a pattern is
  doing; an intent grammar and a legible overlay are arguably the same problem.
- The render half is already data (`render_eval.h` / `render { }` blocks) — consider
  whether render shapes are themselves an intent primitive.

## Files to attach (I will attach these myself)
- `src/trance/visual/pattern_ast.h` — the per-frame AST (the thing to sit *above*).
- `src/trance/visual/pattern_parser.h` — current grammar (EBNF in header comment).
- `src/trance/visual/builtin_patterns.cpp` — the 8 patterns whose intent is hidden.
- `src/trance/visual/cyclers.h` — the timing primitives everything lowers to.
- `src/trance/visual/api.h` — `VisualControl` / `VisualRender`: the entire primitive
  capability vocabulary the engine actually has.
- `src/trance/visual/pattern_compiler.h` — how the AST lowers to Cyclers today.
- `src/trance/visual/render_eval.h` — the data-driven render model.
- `docs/roadmap-grammar-v2.md` — prior thesis (one input).
- `docs/roadmap-f1-overlay.md` — the legibility/intent overlay angle.
- `docs/visuals.md` — as-built reference (how a pattern becomes pixels).
