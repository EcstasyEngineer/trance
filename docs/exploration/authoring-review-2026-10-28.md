# V4 authoring review and implementation handoff

Baseline review at source `b50fbda68d2222e6558400e5f69a78b60a968ba3`, 2026-10-28.
The follow-up implementation is authorized and included in this working tree.
The current repository had no existing issues; this pass created fresh tracking
for the current requests, rather than copying historical issues from a replaced
repository.

## Implemented follow-up

- One mutually exclusive child policy: Independent, Loop children, or Wait for
  children. Independent remains the default and reports exact inactive/clipped
  spans. Loop handles both whole sequences and individual parallel children.
  Wait uses exact finite child completion as the iteration clock, without a
  calculate-frames action or speculative sampling.
- An integrated proportional timeline, root overview, zoom/pan, frame inspection
  and selectable nested lanes; bounded uncertain schedules are labeled honestly.
- Type changes preserve children. Explicit timed wrapping uses an author-chosen
  duration. History is bounded and preserves the selected node; image visibility
  is offered by the property menu.
- Three Astra reviewers debated the minimum text grammar and converged on the
  feature subset of the eight built-ins, plus the separately requested timing
  policies. SOL implemented the browser/native frontends. Both feed the same AST
  and scheduler; unsupported advanced JSON converts only with an explicit error.
- Scientific notation is preserved and unconsumed expression tails fail. Finite
  subnormal regressions cover numeric portability; no Windows subnormal crash
  was reproduced. Text input limits apply before recursive parsing/formatting.
- Captures are one record; compiler construction owns and moves child subtrees;
  static packaging generates its catalog from canonical built-ins.
- Director keeps the executed terminal frame drawable until the following
  content tick. A live pixel regression fails on the old executable during
  changes between distinct one-frame visuals (captured channel mean zero), and
  passes after the repair (all sampled center-channel means about 189.4). A
  forced same-root repeat already passed in the old executable; the regression
  is specifically the replacement of a completed visual. Ready-media Flash Text
  also passes sampled GPU screenshots. This is not a universal cold-media or
  transparency guarantee.
- Native F2 built-in source/copy and new-pattern source use readable V4 text;
  legacy JSON remains accepted. V4 scope names are restored in progress logging.

Implementation details: [native](native-implementation.md),
[text grammar](text-grammar-implementation.md),
[composer](composer-implementation.md), and the
[Astra implementation review](minimal-v4/implementation-review.md).

The wider typed-value migration and frame-preparation architecture remain
optional design candidates in issue #8. The shipped reductions above address
concrete duplication without introducing a replacement rendering framework.

## Tracking and recommended order

The [tracking hub](https://github.com/EcstasyEngineer/trance/issues/1) links the
seven focused issues. Address the verified numeric bug
[#7](https://github.com/EcstasyEngineer/trance/issues/7) first, then surprising
type conversion [#4](https://github.com/EcstasyEngineer/trance/issues/4).
Timing visibility and completion design are
[#2](https://github.com/EcstasyEngineer/trance/issues/2); review the timeline
concepts in [#3](https://github.com/EcstasyEngineer/trance/issues/3) before
implementation. Continuous Flash Text image coverage is
[#5](https://github.com/EcstasyEngineer/trance/issues/5). Concise V4 text is
[#6](https://github.com/EcstasyEngineer/trance/issues/6), and small code/concept
reductions are [#8](https://github.com/EcstasyEngineer/trance/issues/8).

## Evidence and conclusions

- [Timing audit and checked bounded proof](timing-audit/README.md): native
  parser/compiler/scheduler probes cover nine schedules and two rejections.
  Root sequential cap2000/A600/B600 ends at1200; nested under parallel2000 it
  leaves800 inactive ticks. Cap1000/A400/B400 leaves200 inactive ticks;
  cap1000/A600/B600 clips200 B ticks. Explicit repeat already provides looping.
  A sequence's early completion does not shorten its authored modulation clock.
  Lean checks the mathematical model, not C++ correspondence or the full player.
- [Composer review](composer-elegance-review.md): Parallel -> Sequential/Random
  invokes insertion normalization, inventing128-frame owners. They remain when
  changing back. Preserve drafts and make wrapping an explicit editing action.
- [Grammar review](grammar-elegance-review.md): V4 is JSON AST and arbitrary
  authored expressions are already absent. Eighteen built-in curves use local,
  scene and ancestor clocks; sum, triangle, envelope and non-linear cadence
  easing each have a concrete built-in caller. Concise text should lower to the
  same AST, with simple default curve spelling and optional advanced clauses.
  Scientific notation lowering is a verified independent correctness bug.
- [Runtime review](runtime-elegance-review.md): native Flash Text retains an
  opaque lower synthetic image for1025/1025 sampled frames. Cold media can leave
  initial frames empty. Opposing source-over fades alone dim to75% coverage at
  midpoint; an opaque base plus an overlay avoids that particular gap. Actual
  media pixels and director-presented final-frame behavior remain unverified.

The hosted `v4-app.js`, `v4-grammar.js` and `builtins-v4.json` were fetched and
compared with this checkout. All match after CRLF/LF normalization, and the
parsed catalog is identical. There is no evidence here of stale hosted code.

## Timeline review

[`frame-timeline-concepts.html`](frame-timeline-concepts.html) contains two
isolated interactive designs: **Frame strip** and **Nested lanes**. Each shows a
proportional shared frame axis, overview, zoom, pan and frame inspection.
Scenarios distinguish inactive tails, clipping, explicit repeat and parallel
lanes; inferred duration is clearly labeled proposed. These are schematic
concepts, not an AST interpreter, execution trace or media renderer.

Local browser QA exercised both variants, all scenarios, zoom and pan at320px
and736px, with no horizontal overflow or page errors. The scratch test and
screenshot are under ignored `build/` and are not runtime dependencies.

The user approved implementation after reviewing the concepts. The supplied
text wall and crossfade clarification are incorporated. The implemented text
grammar is documented in [the current text reference](../v4-text-grammar.md).

## Design recommendation

Keep duration, repetition and completion authority as separate decisions. Start
with non-blocking diagnostics and explicit timeline spans; do not require exact
child fit. Consider opt-in inference for finite sequences and finite parallel
groups before adding all/any/leader modes. Direct outputs, random exits,
sampling, cadence and forever children need explicit treatment. Leader choice
must not silently decide whether followers repeat or finish their current work.

Prefer focused cleanup: shared editor capabilities, generated built-in catalog,
one history insertion path, one capture record, owning compiler construction,
and avoiding copied subtrees. A typed render-value representation is a larger
follow-up; repair numeric preservation independently first.

## Verification limits

The supported full Windows Release preset builds successfully. All 11 CTests
pass, including the GPU ThemeBank check, native text fixtures and browser text
checks. Recording-renderer comparisons cover 4,225 ticks for each of eight
JSON -> text -> JSON recipes. Nine authoring-server tests pass. Browser checks
cover type conversion, explicit wrapping, policies, visibility, dirty-source
preservation, history limits and responsive layout. Live pixel boundaries and
live nested progress/source replacement checks pass.

The earlier timing proof remains a checked independent-mode mathematical model,
not a proof of the new policies or C++ correspondence. Headset behavior,
arbitrary decoded media and every cold-loading circumstance remain outside
these bounded checks. The private publication checker must pass again before
the implementation commit and push; issue bodies are scanned independently.
