> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Reviewer 05: burst controllers and branches

Independent review of checked-out `grammar.js`, `app.js`, production
`pattern_parser_v3.cpp`, and `tools/authoring_server.py`. Read `CLAUDE.md`.
No playback and no implementation changes by this reviewer.

## Verdict

Burst source roundtrips, branch construction, inspector option edits and product
validation passed. One reproduced clock-picker defect was reported to the parent
and independently verified fixed in the live browser during this review.

## Executed evidence

Run `node tests/authoring_review05_test.js` from the repository root.
Result: **34 successful expected-result comparisons against the actual Release
product lint**, including accepted and deliberately rejected sources; **20 exact
lossless roundtrips**. The test creates isolated temporary pattern files, invokes
`trance.exe --lint --pattern=...`, and removes its own temporary directory.

Verified accepted cases:

- Required period with default chance/cooldown/duration; all named branches and
  controller; reordered options; chance `1/1`, zero cooldown, fixed and ranged
  duration, zero duration, and duration not divisible by period.
- Nested controllers beneath base and held branches; nested cadence clocks;
  curves reading the controller, the held phase and the containing pattern.
- Fixed-duration frame envelopes and sampled-duration fractional envelopes.
- Inspector `fields`/`setField` replacement of every burst option and controller
  name, fixed-to-range and range-to-fixed duration changes, optional-field
  removal and re-addition, preserving other branch headers and source comments.
- `G.create('base')`, `G.create('enter')` and `G.create('burst-phase')` produce the
  intended contextual kinds. Base and held branch clock names remain editable;
  entry exposes no clock-name property.

Verified product rejections:

- Missing/zero period, zero chance denominator, numerator other than 1,
  decreasing duration range, duplicate branches and a named entry branch.
- Timed children in entry, normalized local base curves, frame envelopes and
  ramp cadences under sampled holds, and normalized curves over a controller
  without a fixed enclosing span.

The browser CST is intentionally structural and may preserve invalid source
without an immediate parser error; the real compiler rejects these cases.

## Live browser verification

Used a separate background in-app browser tab at `http://127.0.0.1:8766/`.
Loaded an empty controller named `controller` under a 128-frame pattern, with an
existing source comment. Selected the controller and added **Base phase**,
**Burst entry setup** and **Burst phase** using the palette buttons; selected the
held branch and added an image. The visible block tree had `base`, `enter`, and
`burst -> held`, and the generated source preserved the existing comment.

Expanded the image curve builder and selected Curve. Its clock menu contained
the pattern name, `controller` and `held`. Applying the controller clock emitted
`image runtime -> cur zoom (curve 0 -> 1 over controller)`. The browser's
**Validate with compiler** action returned **LINT OK** and **Compiler validated
current revision**. No player was started. Palette buttons and drag/drop share
`addPiece`; this reviewer exercised the buttons, not a native drag gesture.

## Reproduced finding and fix verification

Originally `app.js` `modulatorBuilder` recognized a named ancestor using
`/->\s*(\w+)\s*$/`. For `burst -> controller period 8f duration 16f`, this
returned no name even though `G.fields` correctly exposed `target=controller`.
Consequently a valid controller clock was missing from the curve menu while
`burst -> held` appeared. Raw parameter editing still worked and product lint
accepted `over controller`.

Reported this to the parent immediately. The parent changed the shared clock
binding logic. The live browser procedure above, performed after reloading the
updated application, independently confirmed the controller option and successful
curve compilation. No remaining burst-specific defect was reproduced.

Coverage limits: lint supplies parser/compiler checks and three no-op render
samples. It does not prove randomized burst timing, playback, decoded media,
or full runtime re-entry behavior. No claim of native drag-gesture coverage is
made by this review.
