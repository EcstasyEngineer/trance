> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Parallel identity and catalog audit

The user's recollection is correct: Parallel originally had two simultaneous
image subjects. This is an old recipe change, not a Flash Text / Parallel swap
in the composer or V4 migration.

## Exact historical change

At `1d5b64781b4fbc7b664f5757c8756b64a46d5a44`, the parent of
`ae7d94c40898ea9c873a716e6bd34ef61fb4417e`:

- `src/trance/director.cpp` maps enum `PARALLEL` to `ParallelVisual`, and
  `SUPER_PARALLEL` to `SuperParallelVisual`.
- `src/trance/visual/visual.cpp:279` defines the two-image `ParallelVisual`.
  Its primary image changes every 64 frames. Its secondary image also changes
  every 64 frames, with a 32-frame offset (`:289-295`). The render draws the
  primary at full opacity and the secondary at 0.5 opacity, each zooming from
  0 to 0.5 across its own cycle (`:323` contains the second draw).
- `SuperParallelVisual` starts at `:333` and builds three image lanes.
- `FlashTextVisual` starts at `:222` and independently draws outgoing/incoming
  images with a crossfade; this is a different two-image behavior.

Commit `ae7d94c40898ea9c873a716e6bd34ef61fb4417e`, dated
**2017-12-07 21:28:26 UTC**, changes the Parallel implementation to one image:
it deletes the secondary image capture, offset schedule and 0.5-opacity draw,
renames the class to `SimpleVisual`, and changes the director's `PARALLEL`
dispatch to `SimpleVisual` (`src/trance/director.cpp:362-363` at that commit).
It also renames the three-image class to `ParallelVisual` while dispatching it
from `SUPER_PARALLEL` (`:365-366`). The serialized enum names stay unchanged.

The June 19, 2026 data-driven port `2e6f087` explicitly labels the recipe
`SIMPLE (enum 5, the enum name is PARALLEL)`. The June 25, 2026 V3 cutover
`2cc3285` likewise maps ID 5 to `kSimple`, with one image. The preserved
September 24, 2026 checkout, rewritten root and initial V4 sources carry that
one-image recipe forward. It did not originate in this visual editor.

## All eight catalog identities

This table records identity and structural behavior, not pixel equivalence to
every historic version. It describes V4 immediately before this correction.

| ID / public name | Old hardcoded identity | V4 identity / shape | Identity discrepancy |
| --- | --- | --- | --- |
| 1 Accelerate | AccelerateVisual | accelerate; one image lane with ramped cadence and word | None; cadence/content have evolved. |
| 2 Slow Flash | SlowFlashVisual | slow_flash; slow and fast image sections in sequence | None; one image subject per section. |
| 3 Sub Text | SubTextVisual | sub_text; one image plus independently scheduled subtext | None. |
| 4 Flash Text | FlashTextVisual | flash_text; two offset image lanes, word and caption | None; register handoff replaced by lanes during this work, with documented startup/theme-refresh differences. |
| 5 Parallel | ParallelVisual before Dec 7, 2017; SimpleVisual afterward | simple; one image plus caption | Public name retains the old two-image intention while the recipe retains the later single-image simplification. |
| 6 Super Parallel | SuperParallelVisual before Dec 7, 2017; renamed ParallelVisual afterward | super_parallel; three staggered image lanes plus word | Public identity is stable; class rename is misleading. Historical temporary single-lane reveal behavior is not represented by the current constant-opacity stack. |
| 7 Animation | AnimationVisual | animation; animated base plus visiting still image | None; envelope approximates the historical reveal. |
| 8 Super Fast | SuperFastVisual | super_fast; exclusive rapid/held states plus word | None; FSM/burst translated into random composition with round-robin selection. |

`src/common/trance.proto` and `src/common/session_json.cpp` retain IDs 4/5/6 as
Flash Text / Parallel / Super Parallel. `tools/authoring_server.py::builtins_v4`
reads those numbered cases directly from `builtin_patterns_v4.cpp`, using the
same eight names. The checked-in browser catalog has those same IDs. There is
no evidence of any ID or dropdown cross-wiring among the eight recipes.

## Recommended correction

Restore Parallel's two-image meaning under existing ID 5; do not exchange it
with Flash Text. Use independent primary and secondary image subjects, both
64-frame cadence and 0-to-0.5 zoom, secondary offset 32 and opacity 0.5, with
animation accents every third selection. Give the AST the clear ID `parallel`
instead of `simple`. Leave Flash Text as the separate two-image dissolve and
Super Parallel as the three-image stack. Preserve current caption/spiral unless
a broader restoration of historical text choreography is explicitly desired.

Validate image-subject counts (2/2/3 for Flash Text / Parallel / Super Parallel),
selected theme side, offset selection events, opacity and zoom through at least
two complete scene loops, and equality between production and editor catalogs.
Compilation alone cannot detect this semantic loss.

## Evidence scope

This was a read-only comparison of preserved local Git objects, current
production source and browser catalog generation. No remote history was fetched
or modified. Commit identifiers identify inspected historical objects; no claim
is made that rewritten upstream history still exposes them.
