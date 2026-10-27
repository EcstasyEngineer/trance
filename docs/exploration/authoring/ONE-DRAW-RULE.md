> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# One subject per image lane, unrestricted composition

The proposed rule is useful as an **authoring convention for a dedicated image lane**. It is not a valid universal restriction on every existing container. A scene groups subjects; a cadence schedules entry events and gives values a local clock. Neither means a single image by itself.

This conclusion comes from the actual eight definitions in `src/trance/visual/builtin_patterns_v3.cpp`, the parser's image-scope qualification, and the existing execution tests. It does not assume that today's syntax is the simplest possible syntax.

## Audit of all eight recipes

“Direct subjects” below means immediate image/text/spiral draw statements in one body, excluding subjects inside nested schedule bodies. `image` already both selects and draws. Standalone `anim` is an effect, not a draw. `look`, `warp`, `drunk` and audio are configuration/drivers/effects, not additional independent image subjects.

| Pattern | Subject composition | Would a universal direct-draw cap reject it? | Image storage today |
|---|---|---|---|
| Accelerate | A ramp body contains an image and a word; scene spiral is independent. | Yes: image plus word in one cut. | Implicit current image per expanded segment; authored animation policy is shared across segments. |
| Slow Flash | Sequential slow/fast sections, each with image cadence, text cadence and spiral. | No immediate-body violation, but each section still composes several subjects. | One implicit current image in each section. |
| Sub Text | Image cadence, subtext cadence and spiral together. | No immediate-body violation. | One implicit current image. |
| Flash Text | Two independent 128f image lanes, top offset 64f; words, captions and spiral continue in parallel. | No immediate-body violation. | Private current image in each nested lane. |
| Simple / Parallel | Image cadence, caption cadence and spiral together. | No immediate-body violation. | One implicit current image. |
| Super Parallel | Three independent image lanes, word cadence and spiral together. | No immediate-body violation. | Private current image in each nested lane. |
| Animation | Independent animated-image and visiting-still lanes, captions and spiral. | No immediate-body violation. | Private current image in each nested lane. |
| Super Fast | Exclusive rapid/hold branches; independent words and spiral continue. | No immediate-body violation. | Shared current image is appropriate because subject branches are mutually exclusive. |

Flash Text previously copied and separately drew an outgoing snapshot. The actual builtin now uses two independent scoped lanes; Super Parallel and Animation also use scoped lanes. No builtin requires authored image-register names, copy, or draw-register statements. The public grammar removes those forms; internal image state remains necessary to retain selections.

## Why two cadences alone do not establish two independent images

The parser's `qualify_reg` gives an unqualified image handle to the nearest **pattern** scope. An ordinary `every` opens a clock scope, not an image scope. Two sibling cadences both writing their implicit `cur` overwrite the same retained image, even if their clocks have different names or offsets. This is a real storage collision, rather than a fundamental need for users to name registers.

Existing nested `pattern` scopes already solve storage separation:

```text
pattern scene for 1024f {
  pattern back for 1024f {
    every 128f { image secondary zoom (curve 0 -> 0.8) }
  }
  pattern front for 1024f {
    every 128f offset 64f {
      image secondary fade inout zoom (curve 0 -> 0.8)
    }
  }
}
```

Each inner pattern owns a different implicit current image. No authored register names, copy, or draw-register statements are needed. This is now the actual Flash Text recipe. The first image starts opaque and both lanes follow live theme refreshes; those intentional changes replace the previous startup fade and frozen outgoing snapshot. Its blend and image zoom match the historical recipe from frame 64 through the tested aligned repeats.

The two lanes cannot simply be identical triangular fades: source-over alpha gives a combined opacity of `1 - (1 - a) * (1 - b)`. At the midpoint, two 0.5 layers leave 25% of the background showing. A permanently opaque lower lane with a triangular upper lane avoids that hole: the lower image changes while the upper is opaque; the upper changes while it is transparent. Fixed layer order matters. A separate execution investigation validates its actual samples and boundaries.

`offset` also suppresses media-selection effects during pre-roll. A nonzero starting clock does not preload an initial image. The first partial cycle and a later scene re-entry need explicit contracts. Live images refresh on theme-generation changes. Historical copied snapshots stayed frozen. The Flash Text replacement intentionally adopts the live-lane policy.

## Implemented authoring recipe

`layer-authoring.js` adds an **Independent image lane** recipe, lowered to existing nested pattern/cadence syntax. It supplies a unique human-readable pattern name, implicit private image storage, one image subject and editable local timing. It inherits a statically known nearest owner duration, including beat-relative forms. When inserted inside an interruption or a variable ramp, it explicitly reports an editable 128f default rather than pretending to know the local duration.

This is an editor recipe, not a ninth grammar primitive or a new parser restriction. Generic containers continue accepting existing compositions. A user can inspect and modify all generated syntax. Authored image registers, snapshot-copy and draw-register statements have been removed from the public grammar. Existing custom scripts using those forms require migration to independent scoped image lanes.

The useful rule is: **start a new independent image lane for each simultaneously retained image; attach timing, motion and opacity to that lane.** Scenes group lanes, and schedule containers may coordinate several subjects. Text selection and animation playback still have shared runtime resources; an image wrapper does not create private text or animation playback channels.

## Verification and limits

- A structural pass over all eight actual builtin source strings found an immediate multi-subject body in Accelerate, as listed above. Flash Text now groups two independent child lanes.
- `tests/authoring_layers_test.js` checks frame/beat/locked duration inheritance, short owners, explicit uncertain-lifetime defaults, generated-name uniqueness, one-subject output, source reparsing and invalid target rejection.
- Existing `phase_execution_test.cpp` functions `composition_order_and_scope` and `offset_capture_and_owner_clipping` establish separate nested image handles, shared sibling-cadence handles, ordered draws, and suppressed offset initialization.
- Real compiler lint through the local authoring API accepted two generated independent lanes inside each of `16f`, `2048f`, `beats 2` and `locked` owners (8-frame beat context). This confirms generated syntax and timing forms, not image independence at playback.
- The separate leapfrog execution investigation establishes selected image identity, opacity, zoom, startup and aligned wrap samples. Compiler acceptance does not substitute for those checks or representative rendered/media inspection.

Do not impose a global one-draw validation error or silently rewrite user compositions. The editor recipe reduces the need for exposed handles without imposing a direct-draw cap on general containers.
