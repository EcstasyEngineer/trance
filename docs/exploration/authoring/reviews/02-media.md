> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 02: media grammar and modifier editing

Independent acceptance completed 2026-10-26, using current `grammar.js`, authoritative `src/trance/visual/pattern_parser_v3.cpp`, and the built Release product. This review did not alter production code or launch playback.

## Result

Pass for the tested media grammar after three editor defects were reported and corrected by the grammar backend agent. `node tests/authoring_review02_test.js` passes **20 real product lint cases**, **19 byte-preserving source roundtrips**, a compound structured-field edit, and regression checks for the reported defects. Temporary pattern files are linted with the actual Release `trance.exe --lint --pattern=...`, in an isolated temporary working directory.

## Coverage and evidence

- Image content: primary, secondary, runtime, alternate, and alternate with parenthesized probability. All five tested with explicit destination, alpha, `anim every 2nd`, final image-selection chance, and a subsequent draw.
- Standalone animation: all five content forms above; standalone runtime animation with trailing chance is correctly rejected by product lint.
- Scoped image registers: image writes, child reads of `scene.saved`, copying into child-local `cur`, child writes back into `scene.back`, and outer draw of `back` all pass real lint. A nonexistent `absent.cur` scope is correctly rejected.
- Snapshot copying and drawing are separate preserved statements. Draw plus plain `anim` passes; draw plus `anim every 2nd` is correctly rejected by the product.
- Modifier ordering: a combined image with curve zoom, ancestor clock, expression origin, alpha, brightness, fade, frame visibility window, envelope, image-animation ordinal and final chance passes. Reordered and repeated ordinary modifiers pass. An ordinary modifier placed after the animation suffix is correctly rejected.
- Editing destination, alpha and visibility on an existing alternate image preserves inline alternation chance, image-animation suffix and final selection chance, reparses cleanly, and passes real lint.

## Defects found and verified fixed

1. Changing content from `alternate chance 0.4` to `primary` formerly retained the inline chance before ordinary parameters, producing invalid source. Backend now removes only the alternation chance, preserving a separate final selection chance. Verified image and standalone animation switches.
2. An alternate image with only inline alternation chance formerly had no independent empty final selection-chance control. Backend now exposes `alternateChance` and `chance` independently. Regression adds selection probability without overwriting alternation probability.
3. Draw-register animation help formerly advertised `anim every Nth`, unsupported by the product's draw grammar. Backend now describes draw animation as plain `anim`.

Minor wording follow-up was sent: an existing selection-chance control still used generic label `chance` whereas its empty form used the clearer `Selection probability`.

## Limits

This proves source preservation, relevant field edits and real parser/compiler acceptance or rejection. It does not prove actual media decoding, visible animation output, runtime snapshot behavior across theme changes, or browser pointer drag-and-drop behavior; those require the other UI/runtime acceptance reviews. Product source confirms a draw register does not trigger a new media selection, while standalone anim changes the shared animation streamer. The editor should keep that distinction visible.
