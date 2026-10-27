> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 04: Super Parallel and Animation

Independent paired stress review of the experimental V4 AST, its restricted V3
adapter, and the actual production recipes. This is not playback equivalence
certification. No production code changed in this review.

## Verdict

Both recipes fit three composition kinds. In fact, both use only `parallel`.
The essential structure is independent image occurrences with cadence and phase
offset, not extra container kinds or authored image registers. These timing and
storage semantics must survive the consolidation.

The examples validate and lower through `tools/pattern_v4.py` and
`tools/pattern_v4_lower.py`. Sending each lowered source to the local production
compiler endpoint returned `ok: true`, `returncode: 0`, and `LINT OK`, with the
expected 1152f and 1024f scene lengths. That endpoint covers parser/compiler plus
three no-op render samples; it does not establish exhaustive frame equivalence.

## Super Parallel

Actual recipe: three private image lanes, in back/middle/front source order.
Their 96f periods have offsets 0/32/64, opacity 1/.5/.33, and local zoom
0..0.875. Back/middle use primary; front uses secondary. The back image animates
every second authored selection. Words select either theme every 32f and the
spiral runs at speed 3.

The V4 example preserves these values and ordered child edges. Lowering creates
separate nested `pattern` scopes for the image lanes, retaining independent
implicit image storage. A single parallel scope containing three direct image
operations is not an equivalent V3 lowering: they share one implicit current
image. The adapter correctly rejects that shape instead of silently sharing it.

Offsets are phases, not initial delays. `OffsetCycler::advance_to_offset`
pre-advances by `length - offset % length`, with actions suppressed. Therefore
pre-roll must consume neither image picks nor animation loads nor RNG calls.
At startup the front lanes may have meaningful local clock positions without a
selected image yet. Replacing phase offsets with sleep/delay can look similar
after a few frames but changes this initialization and parent-reentry behavior.

Stack order is essential. For three available images, ordinary source-over gives
front weight .33, middle weight .5*(1-.33)=.335, and back weight
(1-.5)*(1-.33)=.335. Sorting children, rendering all at opacity 1, or merging
their held images destroys this recipe even though all three patterns still run.

## Animation

Actual recipe: base image/animation every 64f, either theme, zoom 0..0.625;
secondary visiting still every 64f offset 48f, zoom .5..0.625 and an 8f/16f/8f
envelope; either-theme caption every 32f; spiral speed 3.

The V4 example preserves these independent lanes and the explicit envelope. It
does not substitute a whole-cycle triangle. The production envelope is
`max(0,min(p/rise,(end-p)/fall,1))`. On an established 64-frame cycle, the
8/16/8 envelope has 33 zero-alpha frames, 17 full-alpha frames, and 14 intermediate
frames. After the 48f phase shift, absence is base frames 16..48 inclusive.
By contrast, a unit whole-cycle triangle has only one zero-alpha sample and one
full-alpha sample per cycle. An envelope is thus substantive behavior, even if
its editor label needs explanation.

Two separate media issues remain outside container simplification:

- V3 `runtime` independently resolves still selection and animation load; V4's
  proposed `either` must resolve once for that image occurrence. The adapter
  explicitly retains the V3 mismatch. These examples compiling cannot prove the
  corrected V4 contract is implemented.
- Animation stream state is shared per theme lane. Independent image storage
  does not provide arbitrary independent animation playback. This recipe has one
  animated lane, so it does not stress simultaneous animated streams by itself.

## Executable counterexample and requested repair

The current validator accepts a triangle for any scalar field, including image
zoom. The adapter recognizes a unit triangle inside the zoom loop and emits
`fade inout`, silently changing opacity instead of zoom. Reproduction:

```python
from pattern_v4 import validate
from pattern_v4_examples import examples
from pattern_v4_lower import lower

document = examples()["animation"]
visitor = document["root"]["children"][1]["node"]["children"][0]["node"]
visitor["zoom"] = {"triangle": {"from": 0, "peak": 1, "to": 0}}
validate(document)  # accepted during review
assert "fade inout" in lower(document)  # wrong field changed during review
```

Sent directly to the grammar implementation agent: restrict triangles to opacity
for this experiment, or explicitly support/reject them by field in the adapter.
A valid AST must never silently lower a zoom modifier into an opacity modifier.
The reproduction records the observed pre-repair behavior; a later rejection is
the desired fix, not a failed review.

## Required runtime checks before claiming V4 replacement

Use unique image identities and recorded media-side booleans, not just parser
success. Check first activation, every frame of two scene loops, explicit parent
reset, and parent cutoff/reentry. Record picks/loads/RNG during offset pre-roll;
ordered image identities, opacity and zoom during overlap; animation accent
identity across repeated entries; and equal still/animation source side for each
either-theme occurrence. Measure the visitor's 33/17/14 envelope distribution
after warm-up separately from startup with an initially empty image.

Source evidence: `src/trance/visual/builtin_patterns_v3.cpp`,
`src/trance/visual/cyclers.cpp` (`OffsetCycler`),
`src/trance/visual/pattern_parser_v3.cpp` (envelope construction),
`docs/exploration/v4/CONTENT-AND-CLOCK-AUDIT.md`, and
`docs/exploration/v4/MODEL-AUDIT.md`.
