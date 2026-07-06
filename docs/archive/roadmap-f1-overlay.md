# Roadmap — F1 debug overlay upgrade

> **SUPERSEDED — v3 shipped instead; kept for history.**

> **Status: design notes / opinions, not a spec.** Forward-looking. Describes what the
> overlay shows today and how the now-existing pattern grammar lets it become both
> *more legible* and *less code*. Pairs with [roadmap-grammar-v2.md](roadmap-grammar-v2.md).

## What F1 shows today

(`Director::draw_debug_overlay`, `director.cpp`.) Top to bottom:

- `visual :` — the pattern name (built-in type name or `name [custom]`).
- `now : section <PHASE>  pos/len  bar` — the deepest active *labelled* cycler.
- `themes : *pri 'name'   *alt 'name'` — the two live themes; `*` = currently sourced
  on screen by an active image lane.
- `layers :` — how many image layers were composited this frame, with their alphas.
- `spiral :` — spiral type / width / phase.
- the **entrainment bed** (audio layers) — carrier/binaural/pulse/dB per layer.
- the **raw cycler tree** — `Action/OneShot/Parallel/Sequence/Repeat/Offset` with
  pos/len, phase labels, image-slot tags, inactive nodes collapsed.

## The problem

Two things, both flagged by the user:

1. **The cycler tree is the *compiled* form, not the *human* form.** `Parallel 47/96
   [img 2]` is the execution structure (the evaluated AST), not the pattern you'd
   write. It's legible to someone who knows the cycler internals and opaque to
   everyone else — "I like the cycler visualization but I don't fully understand what
   it's doing."
2. **The annotations driving the narration are a Framing-A artifact.** To make the
   overlay narrate, v1 bolted `set_phase` / `set_image_slot` / `image_label` onto the
   `Cycler` base class and made `index()` virtual — a *proto-AST smeared across the
   runtime tree.* The user disliked this at the time ("it was only a proto-AST"), and
   it was the right instinct: now that a real AST (`pattern::Node`) and the DSL source
   exist, that runtime annotation layer is redundant.

## The upgrade the grammar now affords

> **Show the pattern's actual DSL source in the overlay, with live highlights for the
> active source spans** — instead of (or above) the abstract combinator tree.

The DSL is the human form you wrote; highlighting active grammar spans is legible in a
way the combinator tree never will be. This directly answers "what does SUPER_PARALLEL
actually do?" — the answer is its (short, in v2) grammar, on screen, with the current
beat marked.

Mechanics: the compiled `Cycler` tree and the `pattern::Node` tree are parallel
structures. Keep a `Cycler → Node` map at compile time (the compiler already records
`id → Cycler` for *some* nodes; generalise it), walk the active set of the live cycler
tree each frame, and map back to source spans to highlight.

This requires parser/compiler plumbing that does not exist yet:

- keep owned source text with the parsed pattern, not just `name`, `weight`, `render`,
  and `root`;
- attach exact source spans to `pattern::Node` and effect/modifier records;
- preserve origin metadata for generated/desugared nodes, so an expanded node can map
  back to the authored `generate`, lane, or shorthand that produced it;
- map `Cycler* → Node/origin` for every compiled node, not only nodes with explicit
  ids.

The highlight model must be plural. `par`, named lanes, stack renders, and
SUPER_PARALLEL-style interleaving can have multiple active lanes/effects at once. Keep
the singular `now : section` line if it remains useful, but the source view should
support multiple simultaneous highlights, ideally grouped by lane/effect.

## What this lets us delete

Once the overlay derives its narration from the Node tree + a live active/source-origin
map, the runtime annotation machinery on `Cycler` can go:

- `set_phase` / `phase()` / `_phase`
- `set_image_slot` / `image_slot()` / `image_label()` / the `ImageSlotHint` members
- the virtual `index()` (if nothing else needs it)

That's the simplification the user wanted: the overlay gets *better* and `Cycler` gets
*smaller* at the same time — the annotations move from the runtime object to the
grammar where they belong.

## What to keep as-is

The runtime *state* lines are genuinely useful and have no grammar equivalent — keep
them: `themes` (which live themes + which are on screen), `layers` (composite depth),
`spiral`, and the entrainment bed. These are observations of live evaluation, not
structure.

## Dependencies / sequencing

- This pairs naturally with [grammar v2](roadmap-grammar-v2.md): if v2 makes each
  pattern 3–6 lines, "show the source with live highlights" becomes trivially
  readable. Doing the overlay against the *v1* grammar would work but show denser
  source.
- The same "show the grammar" idea should fix the **Creator tooltip** problem (the
  worthless SUPER_PARALLEL tooltip that started all this) — the editor can show the
  pattern grammar instead of a hand-written, rotting description.

## Next actions

1. Extend the parser AST with owned source text, node/effect spans, and origin metadata
   for generated/desugared nodes.
2. Build a complete `Cycler → Node/origin` mapping in the compiler (generalise the
   existing id map).
3. Prototype the source view with multi-highlight by lane/effect; keep the cycler tree
   behind a toggle while it proves out.
4. Once stable, delete the `Cycler` annotation members.
5. Reuse the source view in Creator to replace the static tooltips.
