# V4 development record

The released language is documented in the [V4 grammar](../../spec-grammar-v4.md).
The [native runtime](../../engine-today.md) explains its implementation.
This directory preserves design investigations and executable comparison evidence.

V4 is an AST-first native frontend and occurrence scheduler. It does not generate
V3 text or invoke the historical adapter. All eight built-ins use V4. The player,
F2 and main composer accept V4 JSON exclusively.

## Evidence and historical reviews

- [Native compiler contract](NATIVE-COMPILER.md)
- [Original grammar proposal](GRAMMAR-V4.md)
- [Structural schema](pattern-v4.schema.json) and [eight example ASTs](examples)
- [Model audit](MODEL-AUDIT.md) and [content/clock audit](CONTENT-AND-CLOCK-AUDIT.md)
- [Parallel identity audit](PARALLEL-IDENTITY-AUDIT.md)

Eight paired reviews examined each built-in twice:
[01](reviews/PAIR01.md), [02](reviews/PAIR02.md), [03](reviews/PAIR03.md),
[04](reviews/PAIR04.md), [05](reviews/PAIR05.md), [06](reviews/PAIR06.md),
[07](reviews/PAIR07.md), [08](reviews/PAIR08.md).

The native recording-renderer comparison covers 27,912 frames across the eight
recipes, including loops and child transitions. Selector reference tests examine
selection, local time, sampled duration and random-call behavior. Native runtime
tests add all nine composition nesting pairs, finite-child completion, repetitions,
sampling, offsets and clipping.

These establish bounded execution behavior. They do not prove arbitrary nesting,
media decoding, final GPU pixels, refresh timing or physical-headset correctness.
The current validation commands and limits are in
[validation coverage](../../architecture-maturity.md).

The prototype Python validator and V3 lowering adapter remain comparison tools.
Native compilation is authoritative for executable acceptance and resource bounds.
Reports in this directory describe the state when written; references to
unsupported native nesting or V3 compatibility in those reports are historical.
