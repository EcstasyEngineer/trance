> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 08: accelerate builtin

Verdict: PASS for the assigned authoring path. No actionable bug found. Reviewed 2026-10-26 against the current production source and the running local authoring server at `127.0.0.1:8766`.

## Checks performed

1. Compared the `kAccelerate` raw string in `src/trance/visual/builtin_patterns_v3.cpp` with builtin #1 in `builtins.json`: exact equality after normalizing the C++ file's CRLF line endings.
2. Parsed and serialized the full composition with `grammar.js`: no parse errors and byte-identical source round trip. The root contains the ramp and spiral; the ramp contains image and word statements. No opaque/unknown nodes.
3. Opened the editor in headless Microsoft Edge through Playwright and selected actual builtin #1. Verified the browser source exactly matched the production builtin. Selected all five blocks and inspected their structured fields.
4. Changed three meaningful parameters through actual inspector inputs and change events, then asserted the resulting full source exactly equaled the three intended replacements.
5. Submitted both baseline and edited compositions to the repository's actual compiler lint API; edited validation used the editor's Validate button. Both accepted with `LINT OK`, no warning/error diagnostics. No browser page errors.

## Preserved structured values

| Block | Field | Original value |
| --- | --- | --- |
| Pattern | name / duration | `accelerate` / `2048f` |
| Ramp | endpoints | `56f` to `12f` |
| Ramp | steps / easing / local clock | `140` / `early` / `cut` |
| Image | content / alternation probability | `alternate` / `0.5` |
| Image | zoom expression | `[0.4 * accelerate.progress + 0.1 * this.progress]` |
| Image | origin curve | `(curve 0 -> 0.4 over accelerate)` |
| Image | animation mode | `anim every 4th` |
| Word | content / visibility / selection probability | `primary` / `0..0.25` / `0.5` |
| Spiral | speed curve | `(curve 1 -> 4 over accelerate)` |

The image expression retains both the named whole-pattern clock and local occurrence clock. Alternation probability is exposed as `alternateChance`, separately from the optional final image selection probability `chance`; word chance is also separate. The inspector exposes the complete expression/curve strings as editable modulator values rather than dropping their clock bindings.

## Browser edits and compiler result

- Ramp `steps`: `140` → `128`.
- Image alternation probability: `0.5` → `0.3`.
- Word visibility: `0..0.25` → `0..0.2`.

All other bytes, including easing, local ramp name, compound zoom expression, origin curve, animation mode, word chance, spiral curve and root duration, remained identical. Both baseline and edited pattern compiled at `2048f`. Baseline lint returned code 0 with `LINT OK`; edited UI displayed `Compiler accepted this source` and `LINT OK`. The compiler also linted all eight builtins successfully on each request.

## Reproduction and limits

From repository root with the authoring server running on port 8766:

```powershell
node docs/exploration/authoring/reviews/08-accelerate.test.cjs
```

The test uses the bundled Playwright module and installed Edge channel. It modifies an isolated browser draft only and leaves production files unchanged. No live playback was started. Compiler acceptance covers the product parser/compiler and its no-op render samples; it does not establish decoded-media appearance, frame-identical playback, or exhaustive expression behavior.
