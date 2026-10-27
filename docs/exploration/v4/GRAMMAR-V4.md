> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Experimental V4: three composition kinds

Status: native V4 compilation is implemented. The production player and main
composer read the canonical JSON AST directly. The eight builtins use this
compiler; V3 text sources are rejected. JSON is the interchange
form for the visual editor and automation, not something end users must type.
See [native execution details and limits](NATIVE-COMPILER.md).

The three composition kinds are **parallel**, **sequential**, and **random**.
Every kind can contain every other kind. Image, word, caption, subtext, line and
spiral are output leaves, rather than additional composition kinds. Each image
operation owns its held image privately; a second image never silently replaces
the first one's storage. Internal operation identity is automatic, not a register
name the author must manage.

## AST contract

```text
Document = {version: 4, root: Pattern}
Pattern  = {kind: parallel | sequential | random, children: [Edge, ...],
            duration?: Frames | Sample, repeat?: Count | forever,
            cadence?: Frames | {ramp: {from, to, steps, ease}},
            offset?: Frames, id?: Identifier, select?: Selection}
Edge     = {node: Pattern | Output, exit?: {probability, check_every: Frames}}
Sample   = {min: Frames, max: Frames, step: Frames}
Selection= {order: round_robin | random, allow_self: Boolean,
            first: ChildIndex, cooldown?: Frames}
Output   = {op: image | word | caption | subtext | line | spiral, ...}
Scalar   = Number | {curve: {from, to, ease?: linear | early | late,
                            scope?: local | scene | AncestorId}}
                  | {sum: [Scalar, Scalar, ...]}
                  | {triangle: {from, peak, to}}
Probability = NumberInZeroToOne | {numerator: Integer, denominator: PositiveInteger}
```

The machine-readable shape is [pattern-v4.schema.json](pattern-v4.schema.json).
The authoritative semantic validator is
[pattern_v4.py](https://github.com/EcstasyEngineer/trance/blob/3fd6dccffda4dfa7e44967b13239f954b42b5dd6/tools/pattern_v4.py); it additionally checks IDs,
ancestor references, local envelope fit, sampled-duration alignment, legal
combinations, unsupported no-op output properties and finite sequential children.
Unknown fields fail instead of becoming invisible flags. All durations in this
initial experiment are integer reference frames; beat synchronization, audio,
distortion and appearance configuration are not silently declared migrated.

Children of parallel execute together, in source stacking order. Sequential
activates one finite child at a time; on completion it advances, and after its
last child it is complete. A long enclosing duration does not implicitly restart
the sequence. Repetition is explicit: duration is **one iteration**, and repeat
is the iteration count. Slow Flash is 1536 frames repeated twice, totaling 3072.
An enclosing occurrence clips its descendants, including a partially completed
sequence, cadence or sampled hold.

Cadence repeats the node's body at each interval, restarting its occurrence
clock. Offset is a **phase shift**, not an inactive delay. The initial local
cadence position is `(period-offset) modulo period`; advancing to that phase
suppresses entry effects and RNG. Rendering remains active: before the first
selection a new image is empty, but a retained image can render immediately
after a parent reset, using the shifted local clock. This boundary behavior is
load-bearing for Flash Text's upper lane and Animation's visitor. Offset does
not clear stored content or guarantee an invisible pre-entry interval.
A ramp samples `steps` durations using the from/to shape and easing,
then scales/rounds them to exactly fill the explicit node duration, with at least
one frame per step. These properties own clocks; they are not new container
kinds. A nonrepeating child can finish before its parent and remain inactive.

Each child activation resets local elapsed time and selected subtree clocks.
Authored image animation counters and alternate-theme state persist across
re-entry and root repetition. Those identities must not be reminted by ramp
expansion. Parent duration and root program lifetime remain distinct from an
authored selection policy's lifetime.

## Exclusive selection without special phases

Random activates one child at a time. Its policy is visible: random uniformly
chooses among eligible child indices, while round robin visits the next child in
source order. `first` chooses the initial child. `allow_self` applies only to
random order and means a real restart when the current child is selected again;
the validator rejects true with round robin. The editor should hide that option
for round robin. With a single child, a completion/exit restarts it once; it must
never spin through zero-time transitions. With random order and self disabled,
an empty eligible set likewise restarts the sole child once.

A child exits when its finite lifetime completes, or when its edge's probability
succeeds on a `check_every` boundary. Checks are anchored to the selector's local
clock, not each child's entry. A selector-wide cooldown after transitions gates
chance checks only; completion always wins. There is no chance check on a frame
that already transitioned. Initial eligibility is immediate. Inactive branches
do not advance clocks, select content, load animation or consume random values.
Sample a duration once per activation from its aligned inclusive range.

Super Fast needs no default/interrupt node kinds or public controller clock name:

```text
random(order=round_robin, first=0, cooldown=64)
  parallel: rapid image every 8f; exit chance 1/12 checked every 8f
  parallel: one animated image; duration sample 64..128f step 8
```

The minimum hold is already as long as the cooldown. Hold completion therefore
returns to rapid, starts its 64f cooldown, skips a second transition on that same
frame, and resumes checks on the controller grid. This is the exact controller
contract, rather than a random duration substituted for interruptions.

## Content and modulation

All media leaves except spiral require `source`: primary, secondary, either or
alternate. Either makes a fresh fair theme-side choice per selection, not a
clock choice. Alternate starts with a remembered primary side and flips before
the first selection, so its first ordinary selection is secondary. Optional
`flip_probability` controls flipping only. `selection_probability` independently
controls replacement/selection; the exact endpoints 0 and 1 mean never and always.
Image replacement misses retain the held image, reset the occurrence's motion
clock and advance its authored animation counter. Text misses suppress that
occurrence's text selection and rendering. These distinctions preserve the
current builtins' meaningful behavior without an arbitrary expression language.

Image `animation: {every: N}` requests animation on every Nth selection attempt;
N=1 means always. Resolve the image occurrence's theme side **once** and reuse it
for still fallback and animation selection. That deliberately fixes V3's two
independent runtime-theme rolls. The native compiler implements this contract; the Python validator alone does
not execute it. Existing shared animation streams and text
channels remain an engine limitation; composable clocks alone do not isolate
arbitrary concurrent playback resources.

Image motion supports zoom and zoom_baseline; image/caption/subtext support
opacity or an envelope; spiral supports speed. Visibility is a half-open local
fractional interval. Unsupported text zoom/opacity combinations fail rather than
becoming ignored flags. Envelopes specify integer in/hold/out frames; their sum
must fit the known occurrence duration, and the remaining tail is absent.
Triangle explicitly represents a fade-in/out, preserving Flash Text without an
expression string. Multiple opacity-producing modifiers cannot overwrite one
another: choose one opacity shape or envelope.

Curves read their local occurrence unless they explicitly reference the scene or
an ancestor ID. IDs are optional and required only where a curve needs that
particular ancestor. Accelerate's zoom is a sum of scene curve 0→.4 and local
curve 0→.1; its baseline is the scene curve 0→.4. Slow Flash's fast zoom reads the
512f fast-section clock while images change every 8f. Removing these relationships
would simplify the syntax by breaking the effect.

## Eight concrete translations and limits

The [examples directory](examples) contains all eight ASTs, including leaf motion,
content choices, opacity, animation frequency, text probabilities and timing.
[pattern_v4_examples.py](https://github.com/EcstasyEngineer/trance/blob/3fd6dccffda4dfa7e44967b13239f954b42b5dd6/tools/pattern_v4_examples.py) generates those
canonical files. [MODEL-AUDIT.md](MODEL-AUDIT.md) and
[CONTENT-AND-CLOCK-AUDIT.md](CONTENT-AND-CLOCK-AUDIT.md) record independent source
audits and deliberate differences.

The earlier [restricted V3 adapter](https://github.com/EcstasyEngineer/trance/blob/3fd6dccffda4dfa7e44967b13239f954b42b5dd6/tools/pattern_v4_lower.py) is
retained as historical comparison evidence. Native V4 no longer depends on it:
generic selectors, nesting, completed-child tails, private images and coherent
image/animation theme choice are implemented directly. The common renderer and
its shared animation/text/audio resources remain in use.

Run the contract checks with:

```console
python -m unittest discover -s tests -p test_pattern_v4.py -v
python tools/pattern_v4.py docs/exploration/v4/examples/super_fast.json --format
```

Passing the validator establishes valid structure, not pixel equivalence,
animation decoder behavior, GPU rendering or theme refresh correctness. Paired
reviews and reference scheduler traces are separate evidence.
