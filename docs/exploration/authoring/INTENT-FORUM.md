> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Intent grammar forum

The question changed from “can the current syntax be manipulated visually?” to
“does this vocabulary explain the effects people want to create?” Three active
investigators were redirected from further coverage testing to independent
hypotheses, direct debate and concrete simplifications. No additional review
agents were started after that redirection.

## Read the evidence

- [Every construct and modifier mapped to the eight built-ins](BUILTIN-GRAMMAR-MAP.md):
  actual uses, absent features and plain-language decompositions, with exact source.
- [Complete concept audit](GRAMMAR-CONCEPT-AUDIT.md): actual execution, lifetimes,
  overloaded meanings, ignored combinations and expression vocabulary.
- [Competing designs and recommendations](INTENT-GRAMMAR-RECOMMENDATION.md): what
  can be simplified compatibly and what needs a deliberate language/runtime change.

## Hypotheses, challenges and decisions

| Hypothesis | Challenge from the other investigators | Decision |
| --- | --- | --- |
| Animation accents should be their own authoring concept | An independent animation lane would promise independent playback state that the runtime lacks. The existing ordinal counts scheduled selection attempts, including chance misses. | Separate motion/accent controls attached to the image stream. Preserve shared-stream and attempt-count semantics visibly. |
| Envelopes are a one-off flag | Animation's visitor requires a fade-in, full-opacity hold, fade-out and genuinely absent tail. A whole-cycle triangular fade loses both hold and absence. | Keep the general behavior; explain it as an opacity visit, alongside a separate binary visibility gate. The spelling `env` is an authoring issue, not evidence that the behavior is unnecessary. |
| Registers should disappear | The dissolve needs simultaneous outgoing/current images, and three-layer composition needs three held selections. Deleting identities would make layers overwrite each other. | Move saved-image handles to Advanced. A future dissolve block can own its snapshot handoff; preserve explicit handles for advanced compositions. |
| All probabilities should share one control | Side-switch probability, image replacement probability, text appearance probability and burst interruption odds have different state/visibility effects. | Give them distinct behavior-facing names. Do not silently normalize their runtime meanings while claiming compatibility. |
| Rewrite the entire grammar around typed media blocks now | This needs a source migration and a single authoritative lowering path. A browser-only second language could repeat the same semantic drift. | Keep a coherent replacement surface as a concrete proposal; implement the small compatible improvements and confirmed defect first. |
| Ramp compilation should give every generated segment fresh hidden media state | Segment-local clocks are useful; resetting logical alternation/accent identity on every expanded cut is inconsistent with the ordinary cadence and built-in intent. | Unanimous agreement to give hidden policies authored source-position/operation-role identity, retaining segment-local clocks and image registers. |

## The confirmed execution defect

The production parser/compiler/effect/render recorder showed that `accelerate`'s
`anim every 4th` produced zero animated cuts in each of its first three 2048-frame
scene iterations, then animated all 140 cuts in the fourth. Each expanded ramp
segment had its own counter. A normal eight-cut cadence correctly animated two
cuts. Deterministic ramp alternation likewise selected one side for the whole
first scene and the other side for the whole second scene.

The narrow repair makes a hidden policy belong to its authored operation rather
than each generated ramp segment. Different statements and different operation
roles retain separate state. Segment clocks, image storage, random call order,
animation load frequency and syntax are preserved. This is an intentional
behavior correction for ramp-authored media policies, not a source-format rewrite.

The regression target is `animation_accent_composition_test`. Its acceptance is
every fourth cut (35 of 140 per Accelerate scene), alternating sides per ramp cut,
unchanged ordinary-cadence behavior and consistent nested-ramp policy identity.
The supported Release build and all nine CTests passed after the repair,
including this recorder. It observes 35 animated cuts per Accelerate scene and
per-cut ramp alternation. Recorder checks do not establish decoded animation or
physical display output.

## Open decisions

- Should motion frequency eventually count successful media changes instead of
  scheduled attempts? That would change image-chance behavior.
- Should runtime-selected still and animation content resolve the same theme
  side once? They currently can resolve independently.
- Should conflicting opacity clauses warn, or become an explicit composition?
  `alpha`, `fade` and `env` currently replace one another; `show` clauses AND.
- Should a new typed surface hide ordinary lane storage and own a dissolve's
  outgoing/current snapshots? It must still expose accurate clocks and shared
  animation/text limitations.

These deserve explicit contracts and evidence. Consensus on the narrow repair
does not establish that every possible grammar redesign is universally better.

## Follow-up: one subject per lane and leapfrog dissolves

A subsequent independent design/execution/model review tested whether two
offset cadences could replace Flash Text's explicit copy/draw handoff. The
[production execution comparison](LEAPFROG-EXECUTION.md) confirms an opaque lower
lane plus triangular-opacity upper lane matches the original image contributions
and zoom from frame 64 through 2049, including aligned scene boundaries. Two
identical fades instead expose 25% background at midpoint.

Existing nested pattern scopes provide distinct implicit current images, so the
alternative needs no explicit register names. Cadences alone share storage.
The editor now offers an independent-image-lane recipe and a two-lane dissolve
example. The [one-draw review](ONE-DRAW-RULE.md) supports one image subject per
image lane, while generic scenes and timing containers remain unrestricted.

The alternative starts opaque rather than fading from empty and refreshes both
images on theme changes rather than freezing an outgoing snapshot. It therefore
does not silently replace the original built-in. Those are documented behavioral
choices, alongside partial-owner and interruption re-entry semantics.

## Product correction: replace the builtin, remove authored handles

The subsequent user instruction resolves the earlier compatibility choice: Flash
Text itself now uses the two scoped lanes. The separate dissolve button is
removed. Super Parallel and Animation also use independent image scopes.
Authored image destinations, standalone draw and copy have been removed from the
parser and editor; old custom sources using them require migration. Internal
retained image storage remains an engine detail.

Super Fast uses an exclusive default/interrupt controller. Accelerate deliberately
uses a ramp cadence: acceleration and random interruption are distinct behaviors.
The editor distinguishes parallel/sequence scopes, repeating/ramped schedules,
controllers and exclusive phases with names, role badges and a color legend.
Optional entry setup has no builtin users and is removed from the palette;
advanced source support remains.
