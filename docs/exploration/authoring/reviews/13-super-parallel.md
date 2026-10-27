> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Independent review 13: super_parallel

Reviewed built-in 6 in the local grammar composer using Playwright and installed
Edge, with a 1600 by 1100 viewport. Read `CLAUDE.md` first. This review changed
only this report; browser drafts were restored, and playback was never started.

## Results

| Check | Result and evidence |
| --- | --- |
| Production source equality | Pass. Extracted `kSuperParallel` directly from `src/trance/visual/builtin_patterns_v3.cpp`; strict string equality with fallback `builtins.json`, the source textarea after picker selection, and `TranceGrammar.serialize(TranceComposer.getDocument())`. Whitespace and the leading newline were included. |
| Complete structured document | Pass. No CST errors; all ten blocks present: one pattern, four cadences, three images, one word, one spiral. No unknown block. |
| Owner duration | `pattern super_parallel for 1152f` retained. |
| Image lanes | All three are `every 96f`: first has implicit offset zero, second `offset 32f`, third `offset 64f`. All are immediate children of the parallel pattern. |
| Registers and selection | First image is primary into `a`; second primary into `b`; third secondary into `c`. The first alone retains `anim every 2nd`. |
| Alpha and motion | First alpha is omitted; `render_eval.cpp` supplies its default of 1.0. Second and third retain explicit 0.5 and 0.33. All retain `zoom (curve 0 -> 0.875)`. |
| Other blocks | `every 32f { word runtime }` and `spiral speed 3` retained. |
| Inspector controls | Selected the second cadence and changed offset from `32f` to `40f`; selected its image and changed destination from `b` to `middle`, alpha from `0.5` to `0.6`. Used ordinary input fill and Tab blur, without source editing or model mutation. Source and serialized CST contained all three changes. |
| Edited candidate compiler | Validate button returned `Compiler validated · current revision`. |
| Native lane reorder | Playwright `dragTo` moved the existing third cadence header to the first insertion slot directly under the pattern. Generated source placed lane `c` before lane `a`, then `b`; word and spiral followed. Validate accepted the reordered candidate. |
| Undo | One undo restored the reorder exactly. Three earlier undos had restored the independent offset/register/alpha edits exactly. Both comparisons included all original whitespace. |
| Restored compiler | Captured `/api/validate` response after clicking Validate: `ok: true`, `returncode: 0`, empty stderr and diagnostics; stdout included `ok program 'authoring' pattern 'authoring' (1152f)` and `LINT OK`. |
| Browser exceptions | No `pageerror` events during the review. |

## Interpretation and limits

No concrete defect was reproduced in this scope. Offsets remain schedule
properties while image alpha and destination remain image properties. Native
reordering actually changes emitted source order, which can change layer
composition; successful compiler validation does not establish equivalent
rendered output after such a move.

The product parser's cadence handling wraps nonzero offsets in an `Off` node;
the first lane requires no explicit offset clause. The blank alpha control on
the first lane represents an omitted optional value, not evidence of transparency.

Validation used the server's supported Release executable through the real
`--lint` path. The response reports parser/compiler plus three no-op render
samples. This establishes accepted source and lowering, not media decoding,
every-frame execution, visible compositing, animation independence, or hardware
timing. No playback/runtime image claim is made.

## Reproduction

1. Start `python tools/authoring_server.py` from the repository root and open
   `http://127.0.0.1:8766/`.
2. Select `6. super_parallel`; compare the complete source with
   `kSuperParallel` and the fallback JSON.
3. Inspect all four cadences and their children, then spiral; select the second
   image lane and exercise offset, destination, and alpha as listed above.
4. Validate, undo the three edits, drag the third existing lane before the
   first lane, validate, undo, and compare the restored source exactly.

Automation can use `require('playwright')` through `NODE_PATH` and installed
Edge. In this runner the channel lookup required the installed machine-wide
Edge executable rather than its assumed per-user location; this was a browser
launch environment issue, not a composer failure.
