> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 10: sub_text builtin

Verdict: PASS for the assigned composer path. No actionable bug found. Reviewed 2026-10-26 against current production source and the authoring server at `127.0.0.1:8766`. No live playback was started.

## Checks and evidence

1. Extracted `kSubText` from `src/trance/visual/builtin_patterns_v3.cpp` and compared it with builtin #3 in `builtins.json`: exact equality after normalizing C++ CRLF line endings. The browser-loaded source also matched exactly.
2. Parsed and serialized the whole composition with `grammar.js`: no errors and identical source. All six blocks were concrete, editable nodes: pattern, image cadence, image, subtext cadence, subtext, spiral.
3. Opened the actual composer with Playwright and installed Microsoft Edge. Selected every block and compared **all** structured inspector field values, including empty optional fields, against independently parsed grammar fields.
4. Edited five meaningful values through actual inspector controls and change events. Asserted that full source equaled only the five intended replacements; unrelated source bytes remained identical.
5. Used Playwright's actual `dragTo` interaction to drag a Caption palette piece into the second insertion slot of the subtext cadence. Asserted the added node belonged to that cadence, then deleted it through the inspector Delete button. The source returned exactly to the edited composition.
6. Used the visible Validate with compiler button for baseline, edited, caption-added and caption-deleted drafts. All four runs reported `Compiler accepted this source`, the authored pattern at `1024f`, and `LINT OK`; diagnostics lists were empty. No browser page errors.

## Complete composition and inspector

| Block | Preserved fields |
| --- | --- |
| Pattern | name `sub_text`, duration `1024f`, sequential `false`, optional loop empty |
| Image cadence | `64f`, optional local clock and offset empty |
| Image | content `runtime`, zoom `(curve 0 -> 0.375)`, animation `anim every 3rd` |
| Text cadence | `32f`, optional local clock and offset empty |
| Subtext | content `secondary` |
| Spiral | speed `4` |

Image controls also expose origin, alpha, brightness, fade, show, envelope, selection chance and destination. Subtext exposes zoom, origin, alpha, brightness, fade, show, envelope and chance. Empty values survived inspection. The inspector explicitly states that caption/subtext use alpha and origin rather than zoom. The two cadences remain siblings under the parallel root, and the spiral remains the final sibling; neither cadence was flattened or moved into the other during editing.

## Edits exercised

| Field | Before | After |
| --- | --- | --- |
| Image cadence duration | `64f` | `48f` |
| Image zoom curve | `(curve 0 -> 0.375)` | `(curve 0 -> 0.6)` |
| Image animation mode | `anim every 3rd` | `anim every 4th` |
| Subtext cadence duration | `32f` | `24f` |
| Spiral speed | `4` | `3` |

The caption drag introduced `caption primary` alongside `subtext secondary` inside the text cadence. Compilation succeeded before deletion and after restoration. This demonstrates genuine composition editing rather than only preset switching or source-text replacement.

## Reproduction and limits

Run from the repository root while the authoring server is running:

```powershell
node docs/exploration/authoring/reviews/10-sub-text.test.cjs
```

The test uses bundled Playwright and installed Edge. It edits an isolated browser draft, writes no production implementation, and starts no playback. Compiler acceptance covers the product parser/compiler and three no-op render samples. Actual media decoding, visual layer appearance, animation cadence at runtime and exhaustive expression behavior are not established by this review.
