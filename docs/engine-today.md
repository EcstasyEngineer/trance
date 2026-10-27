# Visual runtime

The released visual language is the [V4 JSON AST](spec-grammar-v4.md). Built-ins,
custom session patterns, CLI source loading, F2 editing and the composer use the
same native frontend. It does not lower through V3 text. Retired parsers,
schedulers and their comparison tests have been deleted.

## Compilation and execution

`pattern_parser_v4.cpp` validates the AST and produces occurrence nodes, content
selection effects and ordered render statements. `compiler.cpp` constructs the
runtime schedule; `cyclers.cpp` advances V4 occurrences. `CompiledVisual` applies
effects through `VisualControl` and evaluates drawing through `VisualRender`.

One occurrence scheduler handles parallel, sequential and random composition.
Parallel advances unfinished children together. Sequential advances one finite
child after another. Random advances one selected child, with completion or
chance exits driving transitions. Every composition can nest every other mode.
Inactive branches do not consume content selections or random values.

Timing properties belong to occurrences: duration, sampled duration, repetition,
cadence, ramp and phase offset. A bounded parent clips its descendants. A completed
child stays inactive until an owner restart. Explicit repetition controls loops;
an enclosing duration does not silently repeat a completed sequence.

## Clocks and persistent state

Content frames advance at the program's configured frame rate. Local progress
refers to an occurrence's active cadence interval or ramp segment. Scene progress
refers to the root. Curves can read an explicit ancestor's occurrence progress
without changing scheduling ownership. Total elapsed program duration remains
distinct from the progress of one repeated iteration.

Phase offsets pre-roll clocks with selection effects suppressed. They do not
preload media. Cadence resets local motion, but authored animation counters,
alternate-side state and captured images have longer lifetimes. Rendering another
desktop or eye pass must not select media or advance these counters.

## Outputs and resources

Each image output owns a private captured selection, even when several images
are direct children of one parallel pattern. Authors do not name registers or
write copy/draw operations. A stale capture can refresh from its current theme
generation. Flash Text uses two offset lanes rather than a copied outgoing image.

`either` resolves the theme side once per image selection and uses it for both
still fallback and animation. Rendering follows that image's captured side.
Animation players remain shared per theme side; multiple same-side outputs do
not obtain independent decoders. Text channels and the dedicated theme-audio slot
also remain shared resources. The V4 language has six visual outputs and no
audio output operation; playlist audio and synthesized beds belong to session
and program configuration.

Internal render expressions and generated state keys are implementation details.
They are not another authoring language or a compatibility contract.

## Validation

The native compiler rejects duplicate members, unknown fields, invalid scalar
scopes and impossible or excessive timing. See [grammar bounds](spec-grammar-v4.md)
and [test coverage](architecture-maturity.md). CPU recording tests compare
schedule activity, selection, stacking, opacity and zoom; decoded frames, device
timing and physical headset output require separate playback validation.
