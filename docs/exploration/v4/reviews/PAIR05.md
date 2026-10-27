> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 05: Flash Text and Accelerate

Verdict: the three composition kinds can represent both recipes without authored registers or expression strings. The experimental adapter compiles both current examples. Complete adapter playback equivalence remains unestablished; compilation and the existing production execution tests are distinct evidence.

## Accelerate

The required behavior is a parallel scene containing an accelerating cadence plus a spiral. The cadence contains an image and an independently probabilistic word accent. These outputs must run together; sequentializing them changes the effect.

The explicit V4 AST retains all load-bearing properties:

- 2048-frame duration; 140 ramp segments; 56-to-12 shape and early easing.
- Image zoom is the sum of a scene-wide 0-to-0.4 curve and a local 0-to-0.1 curve. The scene-wide curve is also the zoom baseline. Dropping that baseline changes projection behavior and can expose bars.
- Alternate theme flips with probability 0.5 per selection attempt. Animation uses every fourth authored image selection, with state shared across expanded ramp segments and retained across scene iterations.
- Word visibility covers the first quarter of each cut, independently gated by selection probability 0.5.

The lowerer creates one enclosing image scope around the complete ramp; it does not author 140 independent image statements. The structured `sum` and named curve scope express the necessary additive behavior without accepting arbitrary expression source in the AST. The adapter serializes a restricted expression into V3 internally.

Current production execution test run passed: 140 selections and 35 animated cuts per 2048-frame pass, across four passes. It also checked authored state across nested ramps and non-divisible iteration lengths. This is evidence for the existing execution machinery; it is not a direct rendering trace of the newly lowered AST.

## Flash Text

The V4 example uses an ordered parallel scene with two independently scoped image cadences. Both have period 128; the front starts at offset 64. The back is opaque; the front has a triangular opacity curve. Both zoom from 0 to 0.8 over their image lifetime. Words and captions have independent 64- and 32-frame cadences.

The adapter preserves separate pattern scopes for these lanes. That scope ownership is essential: two cadences sharing an implicit image cell would overwrite one another. It is an implementation ownership requirement, not a reason to expose register names in the user grammar.

Two identical fading lanes are not a valid substitute: source-over blending at their half-opacity overlap leaves 25 percent background visible. The asymmetric layering is intentional. Child order remains semantically significant for parallel visual outputs.

Current production execution test run passed: identity contributions and zoom match the historical recipe from frame 64 through frame 2049, including 1024-frame scene boundaries. The revised first image starts opaque; theme refresh behavior also differs from the former snapshot implementation. Tests cover non-divisible owner durations and retained front-layer state at reentry. These historical differences are already accepted in the current builtin and must not be accidentally reversed by a V4 rewrite.

## Checks performed

- Lowered both AST examples using `tools/pattern_v4_lower.py`; submitted each result to the running authoring server's production compiler endpoint. Both returned `LINT OK` with exit code zero.
- Ran `animation_accent_composition_test.exe`: exit zero.
- Ran `leapfrog_composition_test.exe`: exit zero.
- Inspected the actual builtin definitions and the two generated AST examples, not only prose descriptions.

Compiler endpoint coverage is parsing, compilation, and three no-op render samples. Decoded-media rendering and a complete frame-by-frame comparison of lowered V4 against current V3 are not established by these checks.

## Counterexamples sent to the grammar author

The first adapter revision had three general lowering holes outside the two unchanged examples:

1. An authored root id could be referenced by a curve, while lowering renamed that root to `v4_scene` and retained the old reference.
2. Automatically generated `v4scope_N` names could collide with authored ids.
3. The validator permitted a triangle in any scalar position. Lowering a zoom triangle emitted `fade inout` instead of zoom; a triangle in spiral speed reached an unsupported dictionary lookup. Triangle support must either be correctly generalized or rejected outside audited opacity positions.

These were reported directly for repair and regression validation. A successful example translation does not justify silently accepting unsupported general AST combinations.
