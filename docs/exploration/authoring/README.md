# V4 visual composer

Open the [hosted composer](https://ecstasyengineer.github.io/trance/) to edit all
eight built-ins, compose your own visual and export a V4 text `.pattern` file or
JSON AST. Text is the default view; unsupported advanced forms stay in JSON.
The hosted page does not require a running player. It performs structural checks;
use the native player for authoritative compilation and playback.

For local native validation, build the Release executable and run
`python tools/authoring_server.py` from the repository root. Open
`http://127.0.0.1:8766/`. **Validate with compiler** invokes the native compiler
without starting playback. The UI labels distinguish this mode from hosted
structural validation.

## Editing

Drag **Pattern** from the palette, then choose **Parallel**, **Sequential** or
**Random** in its inspector. One AST node represents a pattern; its `kind` selects
composition behavior. Mode selectors appear above the controls they enable.
All three modes can contain any other mode. Changing kind preserves the authored
children; validation reports combinations that need a different lifetime or
selection policy.

Image, word, line, caption, subtext and spiral are outputs. A parallel pattern
accepts image outputs directly: dropping an image there does not create a
sequential wrapper. Outputs inside sequential or random composition receive a
visible timed parallel wrapper, giving them a duration for sequencing or selection.
This wrapper is a lifetime convenience, not an image-storage requirement.

Drag headers to move or reorder pieces. Select a block to edit, duplicate or
delete it; undo and redo preserve the AST. Import/export reads and writes the same
semantic AST used by the native player. Full JSON remains editable, and invalid
drafts stay visible rather than being replaced with the last valid document.

Timing controls include Inherit or Fixed frames, repetition, cadence, ramps
and phase offset. Random exposes round-robin/random order, initial child,
cooldown and optional self-selection; child edges can exit by probability on a
specified check interval. There are no default/interrupt phases or public
controller clock names. Motion uses constants, curves, triangles and sums with
local, scene or ancestor scopes.

**Subpattern behavior** contains a **Loop subpatterns** checkbox. When checked,
it cycles a sequence or restarts individual parallel subpatterns to their owner's
boundary. When unchecked, subpatterns complete independently; diagnostics show
inactive and clipped spans. **Inherit** uses the enclosing pattern's frame window,
or finite timed subpatterns to determine the root length. There is no separate
Wait flag or sampled-duration option. Choosing a type never adds wrappers: use the explicit wrapping
action and choose its frame count when validation calls for timed outputs.

The timeline shares one proportional content-frame axis across nested lanes.
Use the whole-pattern overview, zoom, pan and frame inspector; selecting a bar
selects its block. Random-controlled internals are labeled unresolved.
It is a bounded schedule view, not animated media playback or a pixel guarantee.

## Media and built-ins

Every image owns its held capture, including direct siblings in one parallel
pattern. `either` chooses one theme side for the image and its animation.
`alternate` switches remembered sides. Same-side animation players and text
channels remain shared playback resources.

Parallel has two image lanes, Super Parallel three, and Flash Text two offset
lanes forming a dissolve. Flash Text is the actual built-in; there is no separate
dissolve-example button. The local server reads built-ins from the executable;
the hosted site uses the generated built-in JSON shipped with the page.

The composer and player use V4 directly. V3 text is rejected. Retired parser,
scheduler, composer and translation prototypes and their tests have been deleted. See the [V4 grammar](../../spec-grammar-v4.md),
[runtime](../../engine-today.md) and [validation coverage](../../architecture-maturity.md).

## Checks

```console
node tests/exploration/v4_native_browser_qa.cjs
python tests/authoring_server_test.py
python tests/qa_v4_native.py
```

Browser QA requires Playwright and an installed browser. Native execution checks
record output calls; decoded media, GPU pixels and hardware timing remain
separate playback checks. Hosted structural acceptance does not replace native
compiler bounds or runtime validation.
