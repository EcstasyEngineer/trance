> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 15: super_fast

Independent authoring review of built-in picker 8 (`super_fast`).

Verdict: PASS for complete structural authoring, native edits and compiler lint.
No defect found in this review's scope. No playback was started.

## Verified source and structure

The `kSuperFast` raw string in `builtin_patterns_v3.cpp`, checked-in
`builtins.json`, `/api/builtins`, browser source and losslessly serialized CST
match exactly. All ten blocks are visible and independently selectable:

| Block | Inspector values checked |
| --- | --- |
| Pattern | `super_fast`, `2048f` |
| Burst controller | clock `rapid`, period `8f`, odds `1/12`, cooldown `64f`, sampled duration `64f..128f` |
| Base branch | unnamed branch |
| Base cadence | `8f` |
| Base image | runtime content, zoom `(curve 0.0625 -> 0.1875)` |
| Held burst branch | unnamed branch |
| Held image | runtime content, zoom `(curve 0.1 -> 0.7)`, `anim` |
| Independent word cadence | `8f` |
| Word | primary content, probability `0.25` |
| Spiral | speed `3` |

Every block's full-header inspector also matches its visible header. No unknown
blocks appear. The held image has one curve directly within its burst phase;
there is no nested rapid cadence resetting that curve. Production parser fields
for burst period/odds/cooldown/duration feed `BurstCycler::Params` in
`pattern_compiler.cpp`. The existing `super_fast` execution fixture in
`phase_execution_test.cpp` explicitly tests continuous held zoom, local rapid-cut
zoom and one animation selection. That execution fixture was inspected, not
rerun as part of this browser review.

## Browser actions and actual compiler checks

Tested the workbench at loopback port 8766 with Playwright and installed Edge.
Used native browser controls for every mutation:

- Inspected all ten block headers and their fields.
- Validated the original source through **Validate with compiler**.
- Changed controller duration to `80f..144f`, odds to `1/9`, and the base cadence
  to `12f`; source reflects all three edits and actual compiler lint passes.
- Undid all three field edits; source matches the original byte for byte.
- Dragged a palette Word into the base branch; deleted it through the inspector;
  original source is exact. Undo restored the added word; undoing the addition
  restored exact source again.
- Dragged the independent word cadence before the burst controller, verified
  changed source order, then undid it to exact original source.
- Validated the restored source through the compiler again.

All three lint runs returned `LINT OK` and current-revision success. The browser
reported no page errors. These checks establish parser/compiler acceptance and
source-preserving authoring operations; they do not establish decoded-media
behavior, sampled randomness, exhaustive rendering or hardware timing.

## Reproduce

Start `python tools/authoring_server.py`, make Playwright available through
`NODE_PATH`, then run:

```text
node tests/exploration/reviewer15_super_fast.cjs
```

The test derives repository paths from its own location. Optional environment
variables: `AUTHORING_URL` and `PLAYWRIGHT_CHANNEL` (default `msedge`).
