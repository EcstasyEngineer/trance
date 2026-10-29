# Composer implementation and verification

> Historical implementation record. The [current composer](authoring/README.md)
> uses Inherit and a Loop subpatterns checkbox; the three-way policy and sampled duration were removed.

Implemented 2026-10-28. The composer uses the canonical V4 AST and the native
parser/compiler for acceptance. Text lowers to the same AST; it is the default
source view for all eight built-ins. Advanced JSON remains available explicitly.
Unsupported text drafts visibly fall back to JSON while retaining every AST
field, so type changes can expose native incompatibilities without inventing
structure or timing.

## Editing behavior

- Pattern type changes preserve children, their order, timing and chance exits.
  Direct outputs under Sequential/Random produce located structural errors.
  **Give outputs a duration** creates visible Parallel owners only after the
  author selects a positive whole frame duration. Intentional existing owners
  are retained.
- One child-policy select exposes Independent, Loop children and Wait for
  children. Wait explicitly removes owner duration, cadence, phase offset and
  repeat; exact finite timed compositions gate its sum/max duration. Direct
  outputs follow the owner without gating it. Native compilation remains the
  authority for accepted schedules.
- A single bounded history insertion path retains 100 snapshots and the block
  selection. Source application, document loads and block changes share that
  policy; redo is invalidated on a new edit.
- Pending source survives block selection and prevents document replacement,
  block edits and format switching. Import chooses text or JSON from the file
  extension; export uses the active format. Failed source application retains
  the source. Native validation submits the current canonical AST, independently
  of the chosen source spelling. Validation results are invalidated by edits.
- Image visibility is available through Add optional property. Phase offset is
  labeled as pre-roll, with its media-readiness caveat.

## Timeline contract

The timeline is integrated beneath the composition with one proportional frame
axis, a whole-root overview and draggable viewport, time zoom, first-frame pan,
frame inspection and selectable nested lanes. Selecting a bar selects and
reveals its corresponding block. Repeated occurrences retain the same block
identity. At most 600 occurrences are expanded, with an explicit omission note.

`v4-timeline.js` computes a bounded static subset, not a second runtime scheduler.
Constant owner windows, exact sequential completion, finite repetition, nested
Wait sum/max and bounded child looping can produce exact intervals. Direct-leaf Parallel cadence owners used by the existing built-ins expand exact
fixed intervals, initial phase partial windows and scaled ramp intervals. Ramp
construction matches native midpoint easing, positive rounding and backwards
residual-frame adjustment. Phase partial windows explicitly mark suppressed
entry media selection; they do not promise an already captured image. Repeated
windows share a lane. Random selection, sampled duration and more complex nested
phase/cadence schedules remain hatched/unresolved rather than receiving invented
visits. Sampled duration shows
its authored range and a labeled upper-bound axis. Unbounded or otherwise unknown
owner extents have no invented frame labels. No seed is assumed or generated.

The outline retains the authored clock extent while completion and striped
unused tails remain distinct. A root sequence completing early advances the
director; no automatic same-root repeat is drawn. Checker/timeline notes include
frame amounts for exact inactive spans and clipped authored spans. Inactive
schedules do not establish blank pixels: retained media, sibling outputs and
scene transitions remain native renderer concerns. The timeline does not verify
media readiness or display flashing animation.

## Verification

Passed:

- `node tests/authoring_v4_test.cjs`: duplication/scope references, image property
  capabilities, exact early completion, 200-frame inactive/clipped spans, loop
  occurrences, Wait sum/max and rejected ambiguous gates, sampled range honesty, exact fixed cadence/phase refresh boundaries, ramp
  interval scaling/rounding, cadence clipping and seven deterministic built-ins.
  The random/sampled branch in Super Fast remains unresolved.
- `node tests/exploration/v4_static_composer_qa.cjs`: all eight built-ins in text,
  repository-subpath hosting, block insertion/drag, preserved conversion drafts,
  explicit 200-frame wrapping, timeline zoom/pan/selection, Wait and undo, dirty
  source/format safety, 100-snapshot history, and no browser errors or horizontal
  overflow at 1500, 736 and 320 pixels.
- `node tests/exploration/v4_native_browser_qa.cjs`: eight text-view built-ins
  compile through native validation, Wait/Loop and image visibility compile,
  explicit conversion wrapping, nesting/moving, selectors, sampled/chance-exit
  edits, duplicate/delete/undo, JSON import/export and malformed-source retention.

Integrated composer and timeline screenshots at 1500/736/320 pixels were visually
inspected. They are generated beneath ignored `build/composer-qa/`; labels,
controls and schedule notes remain readable without horizontal overflow.

Native parser/runtime/render/pixel verification is reported separately by the
native/product integration work. These composer checks alone do not establish
GPU output, media availability or full player scene-transition behavior.
