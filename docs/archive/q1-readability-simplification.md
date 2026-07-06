# GPT Pro — Q1: Simplify the visual-pattern grammar for human readability

## Your role
You are a programming-language / DSL designer. I want a **design-synthesis** pass:
propose, critique, and converge — don't just validate what's already here. Push back
on my framing where it's wrong.

## What the system is
`trance` is an audiovisual engine. A **visual** is a *pattern*: a timing schedule + a
set of effects + a render description, authored as text in a small DSL. A pattern is
parsed into a normalized AST, compiled to a tree of `Cycler` objects (the timing
model), and played back frame by frame. There are 8 built-in patterns; users can also
ship their own patterns as text inside a session file.

Recent history (important context): the engine used to have 8 hand-written C++ visual
classes. Those were replaced by data — first the **schedule** and an imperative
**state machine**, then (just now) the **render** became data too (`render { }`
blocks). So a pattern is now *fully* data. **Byte-for-byte identity with the original
C++ visuals has been deliberately ABANDONED** — the target is ~90% of the *felt*
behaviour, not pixel identity. That single relaxation is what makes simplification
possible.

## The problem
The grammar works but is **verbose and low-level**. A pattern today is:
- a tree of timing combinators: `seq` (children in order), `par` (children together,
  repeating), `one` (children together, each fires once), `repeat N`, `offset K`;
- with leaves `every N [@K]` / `timer N` carrying comma-separated **effects**
  (`image`, `text`, `anim`, `spiral`, `upload`, `themes`, …);
- plus an imperative **scalar-register state machine** (`set` / `inc` / `toggle` /
  `roll` / `pulse` / `when` / `copy`) that exists *only* to reproduce the hidden member
  variables of the old C++ classes (a random modulus, a parity counter, a toggle, a
  ramp, a captured coin-flip);
- plus `generate VAR from A to B { … }` compile-time unrolling (ACCELERATE expands to
  ~45 near-identical segments);
- plus a `render { … }` block of draw statements whose numeric params are `[expr]`
  arithmetic over live cycler state and registers.

You often cannot answer **"what does this pattern actually do?"** by reading it.

## The goal (in the system owner's words)
> "Define a v2 grammar that accomplishes a ~90% similar pattern for all 8, affords
> infinitely more, and is described in less than half the grammar."

Each of the 8 built-ins should read in ~**3–6 lines**. The vocabulary should **shrink**
while the affordance space (more themes, arbitrary pairings, new cadences) **grows**.

## The question
**Given the 8 current pattern grammars, how would you simplify them so they are far
more human-readable while still producing most of the same results?**

## What I want back
1. A proposed **simplified grammar** (EBNF-ish), with the reasoning for each choice.
2. **All 8 built-ins re-expressed** in your grammar, shown next to the current version,
   so the readability delta is visible.
3. An explicit list of **where your simplification loses fidelity** vs today — be
   specific. The breakages are the real spec.
4. A sketch of **how your grammar lowers** to the existing AST / Cycler model (or what
   in the AST must change). Timing semantics today: `seq` length = sum of children,
   `par` length = LCM, `one` length = max, `repeat` = N×child, `offset` = phase shift.
5. A note on **compatibility**: stored user patterns are raw DSL text with no version
   field, reparsed at runtime — so any replacement needs a migration / dual-parser /
   versioning story. Don't solve it fully; just flag the constraint your design implies.

## Specific things worth your attention
- The imperative state machine is the single biggest complexity sink. Can most of it
  collapse into a few **declarative modifiers** (e.g. "every 3rd", "alternating",
  "ramp", "random(a..b)")? Where can't it?
- `generate` unrolling vs a `ramp(from,to,over)` modifier for the common "speed up over
  time" case.
- The slot selector is currently a **binary** primary/alternate (only two themes are
  live at once). Is a small **theme index** the right generalization?
- There is an existing thesis for this in **`docs/roadmap-grammar-v2.md`** (lanes +
  render-shapes, declarative modifiers, theme-index). **Pressure-test and improve it —
  do not merely agree with it.**

## Files to attach (I will attach these myself)
- `src/trance/visual/builtin_patterns.cpp` — the 8 patterns as DSL text (the subject).
- `src/trance/visual/pattern_parser.h` — the authoritative current grammar (EBNF in the
  header comment).
- `src/trance/visual/pattern_ast.h` — the normalized AST the grammar parses into.
- `src/trance/visual/render_eval.h` — the render-block model (render is now data).
- `docs/roadmap-grammar-v2.md` — the existing v2 thesis to pressure-test.
- `docs/authoring-visual-patterns.md` — the current authoring surface / worked examples.
- `docs/visuals.md` — the as-built reference for how a pattern becomes pixels.
