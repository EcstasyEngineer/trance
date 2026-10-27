> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Independent review 04: text and audio

Reviewed 2026-10-26 against checked-out `grammar.js`, `app.js`, the production `pattern_parser_v3.cpp`, `render_eval.cpp`, `compiled_visual.cpp`, and `docs/audio.md`. Read repository `CLAUDE.md`. No playback or live audio was started; no shared implementation files were edited.

## Verdict and evidence

**Syntax/field acceptance: yes. Runtime/media acceptance: not established.** `node tests/authoring_review04_test.js` passed **252 real product lint cases** through the local authoring server's `/api/validate`, which invokes the Release product executable. Every generated field edit was serialized, reparsed without structural errors, and serialized again byte-for-byte before submission. Lint covers parser/compiler and three no-op render samples, not audible output, decoded media, exhaustive expressions, or state transitions during playback.

| Family | Exercised | Result |
|---|---|---|
| Word, line, caption, subtext | primary/secondary/runtime selection | Accepted; content select excludes alternate |
| Selection chance | 0, 1, fraction, parenthesized fraction, clear | Accepted; inserted parameters remain before trailing chance |
| All text scalar parameters | zoom/origin/alpha/brightness, constant/ancestor curve/expression, then clear | Accepted and lossless on reparse |
| Other text parameters | all fade directions, fraction/frame/expression show, frame/fraction env, then clear | Accepted |
| Text exclusions | alternate content; register destination | Correctly rejected by real compiler |
| Audio selection | three supported content choices × loop on/off × volume 0/constant/curve/expression/clear | Accepted |
| Audio stop | palette statement has no irrelevant editable fields | Accepted |
| Audio exclusions | alternate, stop volume, chance, loop after volume | Correctly rejected |
| Scope | cadence, nested pattern, base and burst placements for all five families | Accepted |
| Indefinite base | local normalized volume curve vs finite named ancestor | Local curve rejected; ancestor curve accepted |

## Findings sent to parent

1. **Audio inspector misstates timing and omits shared state.** `clockHint` classifies every audio statement as an entry effect, even after editing `volume` to `(curve 0 -> 1)` or `[this.progress]`. Production `parse_audio` emits an entry selection plus per-frame `AudioVolume` for these forms. A constant volume fires once; omitted volume preserves the current gain. One theme-audio channel is shared: a later audio selection replaces it; volume is 0–1 clamped and persists across tracks. Scope exit does not itself issue `AudioStop`. The inspector should state these behaviors, especially when users arrange audio in multiple scopes. This is a UI semantics issue, not a failed syntax edit.

2. **Generic composer offers unsupported audio value forms.** Select audio, open the composer, select Fractional visibility window or Alpha envelope, and apply. Audio only has the `volume` target; these modes generate `audio primary ... volume 0..0.5` or `volume in 8f hold 16f out 8f`, which real parser rejects. The help text says windows belong in show and envelopes in env, but audio offers neither target. Restrict these modes to supported targets or disable Apply until the selection is compatible. The same general mode/target constraint applies to text scalars; choosing a curve for show/env is also invalid. Existing compiler validation correctly exposes this.

3. **Text limitations need a slightly fuller explanation.** Existing text hint accurately says word/line alpha/fade/env do not affect rendering and caption/subtext ignore zoom. `brightness` aliases alpha and is likewise ignored by word/line. Fields remain syntactically supported but their presentation can imply visible control despite the hint. Content selection occurs on entry; text drawing occurs each frame. Word and line share one current text slot, not independent content registers or snapshots. Caption and subtext have their own current streams. Adding source-order and state caveats would make simultaneous or nested text blocks clearer.

## Production semantics checked by source

`parse_draw` selects the correct Text/Subtext/SmallSub effect and line split mode, applies defaults, consumes common parameters, and ANDs probability gating with existing show gates. `render_eval` uses zoom/origin for word and line; alpha/origin for caption and subtext; and shared `when` visibility for every type. Alpha aliases/fade/env are parsed but cannot supply alpha to the main text renderer. Caption forces its selection behavior. These are existing engine boundaries, not missing browser syntax coverage.

Audio has no arbitrary path, per-statement channel selector, probabilistic chance, register, or independent voice. The editor appropriately provides none of these. The optional loop precedes volume; stop is a separate statement. A dynamic volume affects the shared channel while its render statement is active, and can therefore affect audio selected elsewhere. The test suite establishes acceptance and rejection only; it does not establish media selection, hearing level, playback duration, or audio state reset.

No parser/serializer defect was reproduced in the assigned families. Parent owns any shared UI fixes and final combined verification.

## Coordinator follow-up

Parent resolution: audio timing/shared-channel hints and text shared-state/brightness notes were corrected. Value composer modes are now restricted by target (numeric versus show versus env). Reviewer06 independently exercised the repaired mode filtering in browser checks.
