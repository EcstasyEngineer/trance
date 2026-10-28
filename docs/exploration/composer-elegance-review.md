# Composer transformation and simplicity review

Review date: 2026-10-28. Production code was inspected without modification.
This is a design investigation, not an implementation or a GPU acceptance report.

## Verified findings

| Question | Result |
| --- | --- |
| Does changing pattern type silently create an extra pattern? | Yes: Parallel to Sequential or Random wraps each direct output in a 128-frame Parallel child. |
| Does Sequential to Parallel create that wrapper? | No. Existing wrappers survive the return to Parallel, which can make the insertion appear associated with that direction. |
| Does the wrapper preserve the original duration? | No. Its 128 frames are an editor default, independent of its former owner's duration. |
| Is a wrapper currently necessary for outputs in Sequential/Random? | Yes, under the current native grammar. Only Parallel can directly contain output leaves. |
| Is there a current timeline or time-axis zoom implementation to extend? | No. The present composer is a nested block editor. The former prototype is a retirement notice. |
| Is Flash Text the image crossfade built-in? | Yes. Parallel is a separate fixed-opacity overlay recipe. |

## 1. Type conversion invents structure and timing

The inspector's Pattern type handler calls `placed()` for every child after
changing `kind` ([v4-app.js:32](authoring/v4-app.js)). `placed()` also serves
palette insertion and drag/move. It combines three distinct decisions:

1. Delete a child's chance exit outside Random.
2. Wrap a direct output in Parallel with `duration: 128` outside Parallel.
3. Assign `duration: 128` to an unbounded child outside Parallel.

The helper lives at [v4-app.js:16](authoring/v4-app.js). Thus a type change also
edits unrelated lifetime/exit data. The current README says changing kind
preserves authored children, but the handler does not preserve their structure.

### Browser reproduction performed

The existing static Playwright harness was reused in memory; no test or
production file was edited. After its two direct-image insertion checks:

1. Start with Parallel, duration 256, and two direct image outputs.
2. Select the root and change Pattern type to Sequential.
3. Read live source: both outputs now sit under separate Parallel children,
   each with duration 128.
4. Change back to Parallel: both 128-frame children remain.
5. Undo twice: the exact original AST returns.

The extra wrappers are therefore observable editor behavior, not an assumption
from reading source. Returning to Parallel does not recover the original
256-frame direct-image lifetimes: the finite children now finish at 128 frames.
This last timing consequence follows from the current finite-child semantics;
the conversion reproduction itself inspected AST changes, not native pixels.

### Why simply deleting all wrappers is incorrect

The native parser explicitly rejects direct output leaves outside Parallel
([pattern_parser_v4.cpp:455](../../src/trance/visual/pattern_parser_v4.cpp)).
Sequential children need finite duration; Random needs a finite duration or
chance exit ([pattern_parser_v4.cpp:462](../../src/trance/visual/pattern_parser_v4.cpp)).
A real authored wrapper may own cadence, offset, repeat, scope ID or local curve
progress. Flattening it can change image selection and motion. The AST carries
no reliable marker distinguishing an automatic convenience wrapper from an
authored lifetime.

### Smallest correct remedy

Remove `placed()` normalization from the type-change handler. Change kind and
its required selection policy, preserve the child array and timing, and surface
the existing validation result when direct outputs need timed children.
Drafts already support structural editing before successful compilation.

Offer an explicit local action, **Give output a duration**, if convenient
conversion is desired. It can add a timed Parallel owner with a duration the
author sees and chooses. This is an editor operation, not a fourth grammar type.
Keep insertion/move convenience separate from type conversion. Do not silently
unwrap existing children on return to Parallel.

Acceptance checks:

- Parallel to Sequential changes no child structure, duration, output property
  or source ordering; unsupported direct leaves produce a located warning.
- The timed-output action creates exactly one visible owner with the chosen
  duration and compiles through the native parser.
- Sequential to Parallel adds no nodes and retains intentional child schedules.
- Round-trip kind changes do not silently erase chance-exit configuration;
  explicitly resolve incompatible data rather than dropping it as a side effect.
- Undo restores the exact original AST; the existing static/native browser
  suites continue to pass after their conversion expectations are updated.

## 2. Current timeline evidence and the smallest useful design boundary

The current [index.html](authoring/index.html) loads only `v4-grammar.js` and
`v4-app.js`; their rendering path creates nested blocks and textual frame
summaries. No frame-proportional axis, parallel lanes, viewport range, playhead
or time zoom is implemented. `zoom` inspector controls refer to image motion.

[visual-authoring-prototype.html](visual-authoring-prototype.html) now contains
only a retirement notice. The timeline described in
[19-27-visual-authoring.md](19-27-visual-authoring.md) is historical work and
cannot be treated as an existing V4 timing implementation.

The lean route is one timeline panel with a shared frame axis:

- Finite parent extent is an outline; children are time-proportional bars.
- Sequential children share a lane; simultaneous children occupy stacked lanes.
- Show clipped tails and empty parent tails explicitly.
- A compact overview with a draggable viewport controls the same expanded axis;
  zoom does not introduce another scheduling model.
- Display sampled/chance future intervals as uncertain ranges unless an actual
  native trace with seed and sampled outcomes supplies exact boundaries.

Create competing visual mockups before committing to this UI. A timeline that
duplicates the whole V4 scheduler in JavaScript would add more concepts and
maintenance than its bars require. First define the interval/trace contract and
which deterministic AST subset can be laid out honestly.

## 3. Flash Text crossfade identity and boundaries

The native built-in is case 4 in
[builtin_patterns_v4.cpp:259](../../src/trance/visual/builtin_patterns_v4.cpp).
It has a 1024-frame Parallel root and two secondary-image lanes:

| Lane | Cadence | Phase offset | Opacity |
| --- | --- | --- | --- |
| Lower image | 128 | 0 | Omitted, renderer default 1 |
| Upper image | 128 | 64 | Triangle 0 to 1 to 0 |

Words run every 64 frames, captions every 32, and the spiral is another sibling.
Both image lanes have their own local 0 to 0.8 zoom. This is not the separate
Parallel built-in's constant half-opacity overlay.

The offset is temporal phase pre-roll, not a declared inactive start delay.
Pre-roll suppresses media selection. Therefore a newly entered shifted image
can initially be empty until its first live selection. The opaque lower lane
is intended to cover that initial gap. The current native recording test
checks the upper lane at frames 64 (alpha 0) and 96 (alpha 0.5)
([v4_native_execution_test.cpp:200](../../tests/v4_native_execution_test.cpp)).

The requested criterion should be recorded as **an available full image at the
first and last authored frames, with no leading or trailing black introduced by
the dissolve**. An opaque lower lane supports that design when its capture is
valid. Source structure and draw-call tests alone do not establish actual media
availability, composited pixels, the final frame or scene-transition behavior.
Those remain explicit native-render acceptance work.

Two simultaneous opposing fades are not automatically coverage-preserving under
ordinary source-over compositing. With an opaque black background, lower alpha
`1-t` and upper alpha `t`, the lower image's surviving weight is `(1-t)^2`, so
the total image weight is `1-t+t^2`, only 0.75 halfway through. Keeping the lower
image opaque and fading the upper image is the smaller coverage-preserving
two-layer construction. Confirm the actual renderer's blend mode before adopting
this algebra as its pixel result.

## 4. Other bounded elegance reductions

### Share one undo-history insertion helper

`save()` truncates history to 100 snapshots, while `change()` pushes directly
without that cap ([v4-app.js:12](authoring/v4-app.js)). A single snapshot-recording
helper used by both removes divergent behavior and a few duplicated decisions.
Acceptance: source apply, built-in changes and more than 100 block edits all
obey one documented undo policy; redo invalidation remains consistent.

### Fix the small capability list before designing a generic schema engine

Native image output supports `visibility`, but `G.properties(image)` omits it
([v4-grammar.js:65](authoring/v4-grammar.js)); the default value already exists.
Consequently **Add optional property** cannot expose a supported image control,
although advanced JSON can. Add that missing capability directly. If capability
duplication is consolidated later, use a small static descriptor table for
labels/defaults/allowed fields rather than a new abstraction or language.
Acceptance: an image can add visibility through the inspector and the emitted
source compiles natively; unsupported word/line opacity remains unavailable.

### Name timing controls by actual context

The single absent-duration label is **Inherit / child completion**
([v4-app.js:25](authoring/v4-app.js)). Those words conflate two behaviors, and
the same option appears on roots even though roots require finite duration.
Use context-aware help beside the existing duration mode rather than adding
another completion concept before its runtime semantics are agreed. Similarly,
`offset` should say **Phase offset**, with the pre-roll/initial-capture caveat.
Acceptance: root/inherited/finite-child examples are described accurately and
the phase label never promises a start delay or preloaded image.

## Checks performed and limits

- `node tests/authoring_v4_test.cjs`: passed.
- `node tests/exploration/v4_static_composer_qa.cjs`: passed using the available
  Playwright dependency and installed Edge.
- The same static harness plus in-memory conversion assertions: passed;
  confirmed both directions and exact undo restoration.
- Reviewed native parser rules, Flash Text built-in and existing native
  draw-recording assertions. Native executable/GPU pixels were not rerun in this
  composer investigation.
- No production changes, commits, pushes or issue mutations were performed.
