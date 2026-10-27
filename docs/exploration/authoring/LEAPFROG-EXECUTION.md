> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Leapfrog execution evidence

The user's two staggered cadence hypothesis works for the continuous still-image
dissolve after startup, provided the two layers have **different opacity rules**.
An opaque bottom lane and triangular-opacity upper lane, each selecting every
128 frames and staggered by 64, can replace the outgoing-image copy handoff.
Two identical triangular fades cannot: half opacity over half opacity gives
0.25 bottom + 0.5 top + 0.25 background under source-over blending.

The production Flash Text builtin now uses the scoped two-lane recipe. Its name,
word/caption cadences and spiral are preserved. The initial opaque image and live
theme refresh on both lanes are intentional replacement behavior. The public
grammar no longer exposes image registers, copy or draw-register statements;
references below describe the historical comparison.

## Reproduction

`tests/leapfrog_composition_test.cpp` uses the production parser, compiler,
`CompiledVisual` effects and render evaluator. Each image selection gets a unique
CPU image identity. The recorder captures image identity, alpha, zoom, animation
dispatch and draw order. Its software compositor applies source-over in actual
draw order, removes zero-weight identities, and compares contributing identities
and weights rather than summing alpha or assuming two draw lists are equivalent.

The standalone source examples are
`tests/exploration/leapfrog/asymmetric-offset.v3` and
`tests/exploration/leapfrog/symmetric-counterexample.v3`.

This checks one uniformly covered pixel with opaque still-image sentinels. It
does not establish texture-space projection equivalence, GPU output, decoding,
display timing, animation snapshots or independent animation streams.

## What the current grammar actually means

- Every cadence does introduce a clock; it does **not** introduce image storage
  scope. Two sibling cadences with `image secondary` both write `cur`. Their
  draws therefore show the most recently selected identity. This aliasing is
  independent of whether each block contains only one image statement.
- Two explicit destinations fix that aliasing. Separate nested named patterns
  can instead provide separate default `cur` storage scopes. A visual editor
  can hide those internal identities but its lowering must preserve them.
- `offset 64 f` pre-rolls the child clock without firing effects. It is not an
  activation delay: the upper render statement exists from frame zero, its
  initial clock is halfway through its cycle, and its image is initially empty.
- A cadence reset resets clocks, not retained images. This matters on reentry,
  partial owner spans and interrupted phases. An apparent first-entry solution
  does not establish a correct arbitrary reentry policy.
- The image recipe does not snapshot animation. `copy` is an image capture;
  these probes intentionally assert that no animation dispatch is requested.

## One draw per container

The useful proposal is one **direct image-producing lane** per lane container,
with parallel composition expressed through child lanes. It is not a proven
engine invariant and should not prohibit a scene from containing several child
lanes, text, a spiral, audio or scheduling controls. A lexical rule limiting
`draw` alone would miss `image`, which emits a draw, and would not prevent `cur`
aliasing. Counting all descendant draws would prohibit ordinary parallel scenes.

The existing recipe's two direct image draws are an implementation of a dissolve,
not proof that every user-facing lane needs arbitrary register machinery. A
simpler authoring model can own the required storage and explicit startup/reset
contract without exposing `copy`, `cur` or `prev` to ordinary users.

## Validation scope

The executable checks initial entry, frames 64/128, a 1024-frame scene boundary,
two full scene loops,1000/70-frame non-divisible owners, explicit reset, retained
selection identity, zoom continuity, omitted-destination collisions, and an
identical second output pass with selection and load counts unchanged. Results
and exact-equivalence limits are recorded below.

The Release target built and ran successfully. Across 2050 frames, the
asymmetric recipe matched the historical Flash Text recipe's selected image
identities, source-over contributions and zoom for every contributing image
from frame 64 through 2049, including owner repeats at 1024 and 2048. Both recipes
select exactly once each 64 frames and retain those selections between picks.
Exactly 64 frames differ: the old recipe fades from empty over the first 64
frames, while the opaque base displays the first image immediately. This is an
intentional startup policy change, not exact equivalence from frame 0.

Replacing explicit destinations with separate 1024-frame `back` and `front`
patterns and their implicit `cur` storage produced **identical full ordered
draw lists, alpha, zoom and pick counts** for all 2050 frames. Explicit register
names therefore are unnecessary for this scoped two-lane spelling today.

At frame 96, the symmetric triangle recipe gives coverage 0.75; the asymmetric
recipe gives coverage 1. At 1024 reentry the upper image is the image selected
at 960, fully opaque and at the expected midpoint zoom, so continuous aligned
loops are correct. With 1000- and 70-frame owners, reentry still resets that
upper clock to midpoint while retaining its old capture: the clock jump is a
real composition boundary requiring an explicit policy. A 200-frame burst
returns at 264 with its prior upper capture fully opaque after 64 inactive
frames. Explicit `reset()` has the same retained-state property.

The executable tests these facts without drawing physical flashes. It also
confirms a second render pass does not select, load, advance motion or change
the recorded output. These results establish an attractive still-image
composition, not a universal replacement for arbitrary snapshot lifetimes.

