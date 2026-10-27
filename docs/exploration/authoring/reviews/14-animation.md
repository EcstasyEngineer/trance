> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 14: animation

Verdict: PASS for the exercised composer path. No actionable defect found. Reviewed current C++ source, JSON catalog, CST and the local authoring server on 2026-10-26. No live playback was started.

## Checks and results

| Check | Result |
| --- | --- |
| Built-in 7 maps to `kAnimation` in `builtin_patterns_v3.cpp` | Yes |
| JSON source exactly equals the C++ raw string after CRLF normalization | Yes |
| CST parses with no notes and serializes byte-for-byte | Yes |
| Every block inspected through browser block selection | Yes, all eight |
| Offset and envelope edited through inspector fields | Yes |
| Zoom edited through the curve value composer | Yes |
| Actual parser/compiler accepts baseline, edited and reordered source | Yes, three `LINT OK` responses |
| Native pointer drag changes layer source order | Yes |
| Delete, undo deletion and undo move preserve exact expected source | Yes |
| Browser page errors | None |

The eight inspected blocks expose these authored values:

| Block | Values |
| --- | --- |
| Root pattern | `animation`, `1024f`, parallel, no explicit loop |
| Animation cadence | `64f`, no offset |
| Subject image | `runtime`, zoom `(curve 0 -> 0.625)`, `anim` |
| Still cadence | `64f`, offset `48f` |
| Still image | `secondary -> still`, envelope `in 8f hold 16f out 8f`, zoom `(curve 0.5 -> 0.625)`, no animation suffix |
| Caption cadence | `32f` |
| Caption | `runtime` |
| Spiral | speed `3` |

The inspector's shared-animation warning is present in the reviewed implementation: animation selection is shared across draws. The still layer's destination remains `still`; its lack of `anim` is preserved through all edits. The C++ comment explains the envelope's intentional 8/16/8 shape: 32 frames of envelope plus 32 frames absent fit a 64-frame turn. This review checks that representation and compiler acceptance; it does not establish rendered frame delivery.

## Browser mutations

Changed the still cadence offset `48f` to `40f`, its envelope to `in 6f hold 20f out 6f`, and its zoom to `(curve 0.45 -> 0.7)`. The first two changes used actual inspector fields; the third used Target parameter / Curve / Start / End / Apply composed value. The entire serialized source matched exactly those three replacements. The product compiler accepted the edited pattern at `1024f`.

Used Playwright's native `dragTo` pointer path on the existing spiral header and the root's first insertion slot. Root order changed from `every, every, every, spiral` to `spiral, every, every, every`. Exact serialization matched moving that CST node, with no header or child changes. The reordered source also passed actual compiler validation. Deleted the selected spiral through the inspector, asserted the exact source without it, then undid deletion and the move. Both restored the exact corresponding earlier source; the final source textarea equaled the edited document.

## Reproduction and limits

With the authoring server running and Playwright available through `NODE_PATH`, run from the repository root:

```powershell
node docs/exploration/authoring/reviews/14-animation.test.cjs
```

The test derives repository paths from its own directory and defaults to installed Microsoft Edge. `PLAYWRIGHT_CHANNEL` and `AUTHORING_URL` can override the browser channel and server URL. It operates on an isolated browser draft. Lint acceptance covers grammar, compilation and the product lint's limited no-op render samples. Decoding, GPU output, actual envelope appearance, animation frames and live caption timing remain unverified.
