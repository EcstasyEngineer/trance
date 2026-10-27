> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 09: slow_flash builtin

Verdict: PASS for the assigned authoring path. No actionable bug found. Reviewed 2026-10-26 against the current C++ source and the local authoring server at `127.0.0.1:8766`.

## Checks performed

1. Compared the production `kSlowFlash` raw string in `src/trance/visual/builtin_patterns_v3.cpp` with builtin #2 in `builtins.json`. They are exactly equal after normalizing the C++ file's CRLF endings.
2. Parsed the full source with `grammar.js`, checked the expected hierarchy and values, and serialized it byte-for-byte unchanged. No parse errors or unknown blocks.
3. Selected builtin #2 in headless Microsoft Edge using Playwright. Both the browser document's serialized source and the visible source textarea matched production. Clicked all 13 block headers and inspected their structured controls.
4. Made three meaningful edits through inspector controls, asserting that the complete generated source equaled only the intended replacements.
5. Used the actual Earlier/Later controls to move the fast phase before the slow phase and back. Verified the model order, visible canvas order and source textarea agreed; restoring the order restored the complete edited source exactly.
6. Used the editor's real compiler validation button on the original, edited and reordered sources. All three displayed compiler acceptance and `LINT OK`, with empty diagnostics arrays. No browser page errors.

## Composition preserved

| Owner / statement | Verified value |
| --- | --- |
| Root pattern | `slow_flash for 1536f loop 2 seq` |
| Slow phase | `slow for 1024f`, parallel child schedules |
| Slow image cadence | `every 64f`, primary image, local zoom `(curve 0 -> 0.5)`, `anim every 2nd` |
| Slow caption cadence | `every 64f`, `caption primary` |
| Slow spiral | `speed 2` |
| Fast phase | `fast for 512f`, parallel child schedules |
| Fast image cadence | `every 8f`, secondary image, zoom `(curve 0 -> 0.8 over fast)` |
| Fast word cadence | `every 16f`, `word secondary` |
| Fast spiral | `speed 4` |

The two phase blocks remain children of the sequential root. Each phase has separate image and text cadence owners plus its spiral statement. Every one of those owners and leaf statements has structured inspector fields. The slow image's local curve and fast image's named ancestor curve are preserved as different editable values.

## Edits and compiler evidence

- Root loop count: `2` to `3`.
- Slow image cadence: `64f` to `32f`; the separate caption cadence stays `64f`.
- Fast image zoom endpoint: `0.8` to `0.6`, retaining `over fast`.

Original compilation reports `3072f`, matching `1536f` per iteration times two. The edited and reordered versions report `4608f`, matching three iterations. The phase durations remain `1024f` and `512f`; their sum remains the root's per-iteration `1536f`. Moving the fast phase earlier changes sequence order and preserves its `over fast` binding. All eight builtins also linted successfully on each validation request.

## Reproduction and limits

With the local authoring server running, execute from the repository root:

```powershell
node docs/exploration/authoring/reviews/09-slow-flash.test.cjs
```

The test uses bundled Playwright and installed Microsoft Edge. It edits only an isolated browser draft and starts no playback. Compiler lint checks actual production parsing/compilation and sampled no-op render evaluation; this review does not establish decoded-media appearance, exact runtime cadence delivery, or every-frame execution. Ordering was exercised with the inspector controls, not a native pointer drag.
