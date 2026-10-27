> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 12: parallel builtin (source pattern `simple`)

Verdict: PASS for the assigned authoring path. No actionable bug found. Reviewed 2026-10-26 using the local server at `127.0.0.1:8766`, bundled Playwright and headless Microsoft Edge.

## Checks performed

1. Verified builtin #5's public name/key is `parallel`, its `sourcePatternName` is `simple`, and its complete JSON source equals the production C++ `kSimple` raw string after CRLF normalization. Verified the C++ ID-5 dispatch returns `kSimple`.
2. Parsed and serialized the complete source unchanged with no parser errors. Verified all six blocks and the parallel hierarchy: root pattern, separate image and caption cadence owners, their leaf statements, and the root spiral.
3. Loaded builtin #5 in the browser. Both the editor model source and visible source textarea exactly matched production. The picker displayed `5. parallel`; the composition title and root inspector retained `simple`. No silent source renaming occurred.
4. Clicked all six block headers and inspected every available structured control, asserting all original populated values and schedule options.
5. Changed root duration, caption cadence and animation frequency through real inspector inputs. Asserted the entire resulting source equaled exactly those three replacements.
6. Used native HTML drag/drop via Playwright's pointer-driven `dragTo` to add a Word palette block after the caption inside its existing cadence. Asserted the new nested child order was caption/word, the image cadence remained separate, and visible source equaled the model. Removed the selected addition through Delete, undid the removal, then undid the insertion, verifying exact source restoration at every step.
7. Invoked the real compiler validation button for original, edited and extended compositions. Every request returned `LINT OK` and empty diagnostic arrays; the original compiled to `2048f`, and both changed versions compiled to `1024f`. No browser page errors occurred.

## Preserved values and inspector coverage

| Block | Original values | Other available fields inspected |
| --- | --- | --- |
| Root pattern | `simple`, `2048f`, sequence false, no explicit loop | None |
| Image cadence | `64f`, no offset or clock name | None |
| Image | `runtime`, zoom `(curve 0 -> 0.5)`, `anim every 3rd` | origin, alpha, brightness, fade, show, env, chance, target |
| Caption cadence | `32f`, no offset or clock name | None |
| Caption | `primary` | zoom, origin, alpha, brightness, fade, show, env, chance |
| Spiral | speed `3` | None |

The inspector explicitly explains that caption/subtext apply alpha/origin rather than zoom. Displaying optional syntax fields therefore does not establish runtime application of each parameter. The local image zoom curve remains owned by its image cadence; the root contains the image cadence, caption cadence and spiral as parallel siblings.

## Edits and drag operation

- Root duration: `2048f` to `1024f`.
- Caption cadence: `32f` to `16f`; image cadence remains `64f`.
- Image animation: `anim every 3rd` to `anim every 4th`; zoom remains unchanged.
- Dragged `word primary` into the caption cadence after `caption primary`; compiler accepted the extended composition. Delete/Undo restored the expected exact sources.

## Reproduction and limits

With the authoring server running, execute from the repository root using Node with Playwright available:

```powershell
node docs/exploration/authoring/reviews/12-parallel.test.cjs
```

The test uses the bundled Playwright module and installed Microsoft Edge. It changes only an isolated browser draft and starts no playback. The review establishes source preservation, inspector editing, native palette insertion, removal/undo, and actual compiler acceptance. It does not establish decoded-media appearance, frame-by-frame cadence delivery, or runtime animation selection.
