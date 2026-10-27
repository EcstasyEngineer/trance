> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 08: Sub Text and Slow Flash

Independent final paired review of production recipes, generated V4 examples,
the semantic validator, shape schema, and restricted V3 adapter. This review
changes no production code.

## Verdict

Both effects fit the three composition kinds. Sub Text needs parallel image,
subtext and spiral outputs. Slow Flash needs a sequential slow/fast pair whose
children are parallel compositions. Neither requires a random selector or a
named controller. Timing properties and explicit curve relationships remain
necessary even when cadence is no longer a separate container kind.

Both generated examples validated and their lowered sources passed the actual
Release executable's compiler lint during this review. This establishes parser
and compiler acceptance, not a native V4 execution trace or pixel equivalence.

| Property | Sub Text | Slow Flash |
| --- | --- | --- |
| Root | parallel, 1024 frames | sequential, 1536 frames per iteration, repeat 2 |
| Image selections | every 64 frames, 16 attempts per occurrence | 16 slow selections every 64 frames, then 64 fast selections every 8 frames |
| Animation accent | every third authored selection attempt | every second slow selection attempt |
| Image source | either theme side | slow primary, fast secondary |
| Image zoom | local 0 to .375 | slow local 0 to .5; fast ancestor 0 to .8 |
| Text | secondary subtext every 32 frames | slow primary caption every 64; fast secondary word every 16 |
| Spiral | speed 4 | slow 2; fast 4 |

Counts in this table are source-derived schedule counts for uninterrupted
occurrences, rather than measurements from a decoded-media renderer.

## Load-bearing boundaries

- Slow Flash totals **3072 frames**, not 1536. Its sequence changes from slow to
  fast at frames 1024 and 2560; the second slow occurrence starts at 1536.
- The fast section's explicit ancestor ID is useful: each eight-frame image
  reads progress across the entire 512-frame fast section. Replacing that scope
  with local progress creates a rapid per-image zoom restart and changes the
  effect. Removing unused controller names does not justify removing this ID.
- Sub Text's animation counter must persist across root resets. Sixteen image
  selections per scene yield 5, 5, then 6 animated selections across the first
  three scenes. Resetting the counter each scene incorrectly produces 5 every
  time. Slow Flash's 16 slow selections are divisible by two, so that recipe
  alone cannot reveal an erroneous counter reset.
- Sequence inactivity is essential: the slow caption, image and spiral must
  stop when fast starts. The current V3 render gates use enclosing active
  pattern clocks. The V4 contract explicitly requires inactive branches to do
  no selection, loading or RNG work; structural validation cannot establish it.
- Sub Text deliberately resolves one theme side for both still fallback and
  animation. Its lowerer uses an existing fair alternate flip, preserving the
  fair-choice distribution while changing seeded RNG consumption relative to
  two independent runtime-theme rolls. This is already documented as a V4
  behavior correction, not an exact random-content migration.

## Counterexample dispatched for repair

The adapter initially accepted this valid V4 shape:

```text
parallel duration 32
  parallel duration 8
    image primary
```

V4 says the child completes after eight frames and remains inactive. The
adapter emitted ordinary nested V3 patterns. `PhaseCycler::advance` advances
every child on every frame, and restarts a completed phase before advancing it.
That mapping therefore reselects at frames 0, 8, 16 and 24 instead of selecting
once and becoming inactive. Product compiler lint accepts this semantic error.

Dispatched to the grammar implementer and root agent: the restricted adapter
must reject shorter finite children in parallel bodies, comparing their total
repeat span with the enclosing body's actual occurrence span. A cadence body
must use its cadence span rather than the full ancestor duration. Alternatively,
a native scheduler must implement the stated completion behavior. Neither of
these two builtin translations needs the unsupported shorter-child behavior.

The existing short-sequence guard correctly rejects implicit sequence restart
when the children sum to less than the sequential duration. Longer sequences
can be clipped by a shorter enclosing duration; Slow Flash fills its sequential
duration exactly. These boundaries must remain separate from repetition.

## Remaining evidence limits

The JSON schema explicitly validates shape only; the Python validator is the
semantic authority for ancestor references, sequence finiteness, envelope fit
and unsupported output controls. Legal nesting of all three kinds is not proof
that every nesting combination can lower to the legacy scheduler. In particular,
generic random composition and sequential cadence remain native-runtime work.

This review does not claim runtime verification of arbitrary clipped sequences,
sampled holds, concurrent animation decoders, GPU output, or live theme refresh.
The two examples preserve the required schedule structure; the production V4
scheduler and media-resource ownership are separate implementation work.
