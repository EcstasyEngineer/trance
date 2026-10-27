> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 03: Simple and Sub Text

Scope: independent source review of built-ins 5 and 3 against the proposed three-kind V4 composition model. No V4 implementation or behavioral equivalence is claimed by this document. The paired assignment is Simple + Sub Text; the shared model audit's suggested assignment table is illustrative, not this review's actual assignment.

## Verdict

Parallel / sequential / random are sufficient **composition kinds** for both recipes. Neither recipe needs a selector container at all. Both need repeating, independently timed image and text occurrences. Deleting `every` as a public block is possible only if its repeat interval, occurrence duration, and reset behavior become structured timing on an ordinary child pattern. Deleting those semantics would break both recipes.

`runtime` is an either-theme content choice, not a clock or a random pattern. Both reviewed recipes use it, so its use is not restricted to Super Fast. Rename it `either`; resolve that choice once for an image occurrence and share the side between still fallback and animation.

## Minimal conceptual trees

These are AST sketches, not proposed parser syntax:

```
parallel duration=2048f                         # Simple
  parallel occurrence=64f repeat=until-parent
    image theme=either zoom=0..0.5 animate=every-third-authored-entry
  parallel occurrence=32f repeat=until-parent
    caption theme=primary
  spiral speed=3

parallel duration=1024f                         # Sub Text
  parallel occurrence=64f repeat=until-parent
    image theme=either zoom=0..0.375 animate=every-third-authored-entry
  parallel occurrence=32f repeat=until-parent
    subtext theme=secondary
  spiral speed=4
```

Single-subject child wrappers own occurrence clocks even when the editor can present them compactly. The spiral is a continuous output operation; turning it into an entry-only mutation would lose its speed over time. Text refresh occurs on entry but remains drawable until the next update; it is not a one-frame draw.

## Required traces and counterexamples

Frames below are zero-based frames immediately after one scheduler advance and before rendering. An occurrence spans `[start, start + duration)`. Existing source-derived expectations must become executable V4 traces; this review has not executed a new runtime.

| Case | Required observation | Simplification that fails |
| --- | --- | --- |
| Independent image/text schedules | Image selections at 0,64,128,...; text updates at 0,32,64,... | A single sequential scene gives text and image alternating time instead of concurrent streams. |
| Simple complete scene | 32 image selections and 64 caption update attempts in frames 0..2047 | Treating 2048 as an inclusive endpoint produces a spurious selection at 2048 before root reset. |
| Sub Text complete scene | 16 image selections and 32 subtext updates in frames 0..1023 | Advancing image timing on text events doubles image selection rate. |
| Image-local motion | At scene frame 32, Simple zoom is 0.25 and Sub Text zoom is 0.1875; at frame64 both reset to zero | Binding curves to the scene clock instead of the current 64f image occurrence erases each zoom pulse. |
| Every-third animation | Image entries 3,6,9,... animate: first starts frame128, next frame320 | Making `every third` mean third rendered frame changes animation from an image accent into a flicker. |
| Simple scene reset | Existing authored counter is retained: first scene has 10 animated entries; next scene's frame0 is authored entry33 and animates | Clearing accent state at every root reset silently changes the existing authored-state lifetime. |
| Sub Text scene reset | Existing authored counter is retained: scene one has 5 animated entries; scene two frame64 is authored entry18 and animates | Resetting count per scene moves next accent to frame128. |
| Parent cutoff at 100f | Image entries only0,64; text entries only0,32,64,96; all outputs disappear when parent stops | Letting a repeated child finish its current occurrence leaks output beyond the owner's lifetime. |
| Repeated render passes | Second eye/pass does not select images, update text, choose theme, or increment every-third count | Evaluating event logic in draw traversal makes behavior depend on eye/pass count. |
| Theme independence | Caption remains primary and subtext remains secondary regardless of image's either-theme result | Moving image theme choice to the enclosing parallel pattern unintentionally couples unrelated outputs. |

At frame63 the local zoom is `63/64 * maximum`, not the endpoint maximum. V4 must choose explicitly whether to preserve this sampling convention; a normalized curve that reaches its endpoint on the last visible frame is a small but real behavior change.

## Shared resources remain a composability limit

The existing API has one live slot per text category: caption uses small text, subtext uses its own collection, and word/line share the main text slot. These two built-ins individually do not collide, but placing two copies of Simple in parallel does not create two independent captions. Placing two Sub Text trees in parallel likewise overwrites one shared subtext collection. Neither a new container label nor an implicit clock creates separate text state.

Current animation loading also writes one `Registers::anim_slot`, and decoder state is shared per theme lane. Two simultaneous independently scheduled animated images can therefore influence one another despite isolated still-image storage. The three-kind model is coherent, but arbitrary subtree composition requires either per-output runtime resources or an explicit validator error for unsupported simultaneous resource ownership. Do not advertise universal independent animation/text lanes on the strength of grammar nesting alone.

## Adjacent modifier stress: failed image selection

Neither reviewed builtin has trailing image `chance`, but the proposed AST supports that modifier and these are convenient minimal counterexamples. In current code a chance miss gates the image replacement effect. It does **not** gate the appended animation-load effect or the every-Nth pulse, and the current occurrence clock still resets. Thus a missed selection may retain the last image, restart its zoom, increment the accent counter, and load another animation. Moving all image-related work into one atomic entry event is cleaner, but it is an intentional semantic correction rather than exact V3 parity.

V4 should define replacement probability separately from `alternate` flip probability and selector switching probability. For a deterministic success/fail/success sequence with animate-every-third, specify whether the third attempt or third successful replacement animates. The model audit proposes retaining authored **attempt** counts; test that deliberately. A probability-zero image on an initially empty lane must have defined blank/fallback behavior and must not accidentally spin a selector or synthesize repeated picks.

## Validation implementation guidance

Record ordered entry events `(frame, authored-node-id, output-kind, chosen-side)` plus per-frame `(active-image-id, animation-gate, alpha, origin, zoom, text-owner)`. Inject deterministic theme choices so the still/animation-side agreement test is reproducible. Compare both recipes through two complete scenes and the partial-parent case, including a second render pass each frame. Treat resource-collision probes as separate acceptance tests from the ordinary builtin traces. Compilation alone cannot establish any of these properties.

Source evidence: `src/trance/visual/builtin_patterns_v3.cpp` (Simple, Sub Text); `pattern_parser_v3.cpp::parse_draw`, `parse_anim`; `compiled_visual.cpp::run_effect`, `resolved_slot`; `cyclers.cpp::ActionCycler::advance`; `api.cpp::change_small_subtext`, `change_subtext`; `tests/animation_accent_composition_test.cpp` (authored counter lifetime and multiple-pass fixture).
