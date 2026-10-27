> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 06: Super Fast and Flash Text

Scope: bounded source review of the experimental V4 AST, selector reference,
restricted lowering, current builtins and existing real-compiler leapfrog test.
This is not GPU/pixel validation or a production V4 runtime implementation.

## Super Fast

The three-kind model can represent the current two-phase controller: round-robin
exclusive selection, rapid child with a chance exit checked every 8 frames, and a
single-image child with a sampled 64..128 frame lifetime in steps of 8. Initial
chance eligibility, inactive-child suppression, returning-phase restart and the
64-frame return cooldown are meaningful semantics, rather than redundant labels.
The controller's authored name is unnecessary when nothing references its clock.

Source anchor: `BurstCycler::advance` in `src/trance/visual/cyclers.cpp`.
It resets on completion, skips exit-frame chance checks, decrements cooldown on
period boundaries and samples duration only when the range has multiple values.

Found and reported three executable-reference gaps:

1. The original 4096-frame comparison omitted the enclosing 2048-frame reset in
   both Python implementations. With seed 7, comparing the unbounded trace with
   two 2048-frame activations sharing one RNG first differs at frame 2048. Parent
   is adding explicit owner clipping/reset/reanchored check-grid tests, including
   an owner duration not divisible by the check interval.
2. A degenerate sampled lifetime `{min:64,max:64,step:8}` unnecessarily consumed
   an RNG draw; native burst does not. Parent is correcting this distinction.
3. A validated selector child with duration 2 and repeat 3 was treated as lasting
   2 frames by `trace`, instead of 6. Reproduced with a second duration-2 child:
   transitions were [2,4,6,8]. A full selector contract must account for repeat,
   or explicitly reject unsupported repeated children in this reference harness.

The existing selector tests meaningfully cover RR/random/self, immediate chance,
check-grid boundaries, zero/one probabilities and single-child restart without a
zero-time loop. They do not independently prove full nested scheduling, renderer
ownership, arbitrary parent activation offsets or text/animation channel isolation.

## Flash Text

The current builtin uses private image scopes and two 128-frame cadences, upper
lane offset 64, opaque lower lane and triangular upper opacity. This is expressible
using parallel patterns with cadence/offset properties and two image leaves.
Two equal triangular fades would have incorrect source-over weights; preserve
asymmetric opacity.

Important unresolved V4 boundary contract: the V4 document says offset delays
activation and reset restarts subtree clocks. The real current V3 implementation
retains the upper lane image across owner reentry; the real-compiler test asserts
that its draw stays active and alpha is 1 at the owner wrap. See
`tests/leapfrog_composition_test.cpp`, owner_reentry checks, including spans 1024,
1000 and 70. A strictly delayed/inactive V4 upper lane would instead disappear
for the first 64 frames after every reset. This must be explicitly preserved via
an image-retention/render policy, or classified as an intentional changed boundary
behavior. Successful lowering to V3 currently inherits V3 behavior and cannot
establish the proposed V4 activation semantics.

No production edits made by this reviewer. Reported findings directly to parent
and the grammar author; fixes and final validation belong to their combined pass.
