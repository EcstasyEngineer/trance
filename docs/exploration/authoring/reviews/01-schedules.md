> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Schedule syntax and editing review

Scope: independent inspection of pattern_parser_v3.cpp, spec-grammar-v3.md, and the authoring grammar.js model. Review performed with Node assertions against the current JavaScript API; no production implementation edited.

## Passed assertions

- One-line nested pattern/every/ramp bodies parse as three separate children; statement boundaries do not depend on newlines.
- Source with comments, nested schedules, seq, loop, beat lengths and named clocks serializes byte-for-byte before edits.
- Changing cadence length from beats 2 to beats 3 and offset from locked to 0f reparses without structural errors.
- Parser implementation confirms declared pattern duration owns each iteration, loop repeats the entire pattern, and seq orders child schedules rather than direct entry effects.
- Parser implementation accepts the fixed-frame, beats and locked schedule forms. The beat period must be supplied by runtime configuration; the structural editor cannot establish its current value.
- Ramp endpoints describe relative sample shape: normalized integer segment lengths fill their enclosing span. They must not be displayed as literal first/last durations. Every ramp segment opens its own clock/register scope.
- Offset is a cadence phase shift, with suppressed-action pre-roll; the editor should not describe it as a media preload or a general startup delay.

## Reproduced bugs and repair requests

1. Adding a missing ramp ease after an existing clock target appends it after the target: `every ramp 24f -> 4f steps 4 -> cut ease early { ... }`. Both the structural model and C++ grammar require ease before the target. Insert this optional clause before `-> cut`.
2. Toggling sequence uses a global word regex and deletes a legal pattern name `seq`: `pattern seq for 128f { image primary }` becomes `pattern for 128f seq { image primary }`. Remove only the modifier token after the duration; preserve the pattern name and comments.

Both bugs were sent directly to grammar_backend and parent for repair. A structural parse passing is not proof of runtime semantics: unsigned lengths, nonzero loop counts, bounded ramp spans and actual clock resolution remain the C++ parser/compiler authority.

## Repair verification

Backend author repaired both findings. Independent re-run confirmed adding ramp ease reparses successfully, and toggling sequence preserves the legal `seq` pattern name. Product `trance.exe --lint --pattern=tests/exploration/authoring-review01.pattern` passed all eight built-ins and the new nested schedule fixture (256f total: 128f per iteration, loop 2). This lint samples compilation/render expressions; it does not prove every playback transition.
