> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Reviewer 16: native drag and drop

Tested the served composer at `http://127.0.0.1:8766` with Playwright's native `locator.dragTo`, headless Microsoft Edge, desktop viewport 1700 × 1000. The companion `16-drag-drop.test.cjs` uses `require('playwright')`; supply the installed package through `NODE_PATH`. No playback. `loadSource` only sets up fixtures; actual insertion and movement use browser gestures, keyboard, and inspector controls. No `addPiece` calls.

## Reproduced findings

1. **P2: a native palette drop can silently disappear while source input is pending.** Load `pattern pending for 128f {\n}\n`. Fill the source textarea with `pattern pending for 256f {\n}\n`, then immediately drag Word into the existing pattern's empty slot (before the 650 ms source debounce). Final source contains the 256f pattern with no word, while the success toast says “Piece added. Edit its properties in the inspector.” Reproduced twice. `dropSlot` retains the old parent object; `addPiece` enters `mutate`, whose `flushSource` reparses the tree before insertion into that detached parent. Existing source content survives, but the requested new block is lost. Resolve the destination in the current tree after flushing, or flush before capturing a drag destination.
2. **P3: rejected moves announce success.** Drag a pattern into its own nested cadence, or drag a cadence into its own first insertion slot. Source is correctly unchanged, but the final toast says “Block moved. Source order has changed.” `moveNode` emits its rejection and returns false, then the unconditional mutation message overwrites it. Preserve the rejection message and avoid recording a successful mutation for an unchanged tree.

## Verified acceptance

- All 22 palette forms insert through native drag/drop, with model/source textarea synchronization. `audio-stop` appropriately creates an `audio` node. Also tested base, entry setup and burst phase inside a controller, and spiral shape settings inside `look`.
- Sibling moves before/after, earlier/later direction, and same-position move preserve order correctly. Cross-scope movement preserves identity/content and removes the original occurrence.
- Moving collapsed root containers reorders them while preserving hidden children.
- Self/descendant protection prevents a source change; the notification defect above remains.
- Alt+ArrowDown moves the focused block, and the inspector scope menu moves it into another cadence. A child in a document with only one enclosing pattern still has the menu, including document root.
- Selecting a collapsed scope and using palette + inserts into that scope and expands it. A collapsed scope has no interior native drop slot; expand first to target its children.
- Native edge autoscroll moved the independently scrolling canvas from scrollTop 0 to 1422 during a held drag over the lower edge. Palette and canvas remain independently reachable on desktop.
- With no selection, Word + appends a word at document root beside the existing pattern. This is structurally preserved draft behavior and can be compiler-invalid; it is not data loss. The UI explicitly instructs selecting a scope for + insertion.
- No browser page errors during the run. Structural creation/movement acceptance does not claim semantic compiler validation of every arbitrary placement, or playback validation.
