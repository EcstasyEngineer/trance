> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 11: flash_text

Verdict: PASS for the assigned authoring path. No actionable bug found. Reviewed 2026-10-26 against current production source and the running local authoring server. No playback was started.

The checks cover production/fallback/API source identity, lossless CST roundtrip, every nested block and inspector, three native field edits, native layer reorder, exact undo restoration, and four real product compiler lint runs. GPU rendering, decoded images, and visible crossfade quality remain untested.

## Source and structure

The test compares the complete `kFlashText` raw string in `src/trance/visual/builtin_patterns_v3.cpp` with built-in 4 in `builtins.json` and `/api/builtins`. All agree byte-for-byte after C++ file newline normalization. `grammar.js` parses with zero structural errors and serializes the original source exactly.

The full tree contains eleven nodes. The root `flash_text` spans 1024f. Its `life` child spans 128f and loops eight times. Inside `life`, named cadence `every 64f -> beat` contains these statements in order:

1. `copy cur -> prev`
2. `draw prev zoom (curve 0.4 -> 0.8)`
3. `image secondary -> cur fade in zoom (curve 0 -> 0.4)`

The other `life` child selects `word secondary` every 64f. The root also selects `caption primary` every 32f and renders `spiral speed 2`. The browser loaded the same full source and exposed inspector fields for every node, including copy source/destination, draw register/zoom, image content/target/fade/zoom, clocks, text content, and spiral speed.

## Browser changes and source order

Using actual inspector controls with Tab to commit, changed `life` loop 8 to 6, outgoing zoom endpoint 0.8 to 0.7, and caption cadence 32f to 24f. The emitted source exactly matched those three replacements. The product compiler accepted it with `LINT OK` and no warnings.

Selected the incoming image block and clicked its native Earlier control. Both the visual block order and emitted source became `copy`, `image`, `draw`. Thus the copy remains before selection, but the draw layers reverse: the incoming image now appears earlier in the render statement list and the outgoing draw appears later. The editor did not silently canonicalize this back to the built-in order. The reordered recipe compiled successfully. Undo restored the edited source exactly; three further undos restored the original built-in source exactly. Baseline and final restoration also compiled successfully. No browser page errors occurred.

## Runtime interpretation and limits

`pattern_parser_v3.cpp` appends copy and image selection to the ordered effect list, while `draw` and `image` append render statements in encountered order. `CompiledVisual` executes effects in list order and passes the ordered render block to `eval_render`. This supports two separate conclusions: copy must precede incoming selection to preserve the outgoing register value, and the original outgoing draw precedes incoming fade-in in the render order. Draw placement among effects is not a per-frame image selection command; selection/copy occur when their action fires, while drawing reads registers each rendered frame.

`run_effect` copies the image value and lane provenance, then removes the destination generation stamp. `refresh_stale_registers` skips such snapshots, preserving outgoing content across theme refresh rather than refreshing `prev` into the incoming image. A copied empty register remains empty: on a cold start before `cur` has been selected there is no outgoing image to dissolve. A snapshot also reflects whatever image was actually captured; it does not establish decoded-media readiness or a visually distinct next selection. Text uses its live selection path, so this image recipe does not establish a text-on-text dissolve.

These runtime conclusions come from source inspection. The four validation passes ran the real Release product parser/compiler and three no-op render samples through `/api/validate`; they did not prove GPU compositing, live media availability, or temporal playback behavior.

## Reproduce

With the authoring server on port 8766, run from the repository root with Playwright available through `NODE_PATH`:

```text
node docs/exploration/authoring/reviews/11-flash-text.test.cjs
```

The test uses headless Microsoft Edge, prints every inspected node and lint result, and closes its browser. It changes only its isolated browser document.
