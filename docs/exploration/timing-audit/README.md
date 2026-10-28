# V4 timing audit and bounded proof

Audited 2026-10-28 at source commit `b50fbda68d2222e6558400e5f69a78b60a968ba3`.
Exploration only: production code and existing runtime semantics were not changed.

## Findings

The implicit loop assumption does **not** match current V4. A sequential iteration
without cadence/ramp ends when its last child completes, or its duration cap is
reached, whichever comes first. A parallel occurrence keeps its own duration;
completed finite children remain inactive. Explicit `repeat` or `cadence` can
restart the body. Root completion also invokes the director's next-pattern
selection, which must be distinguished from an internal child loop.

All intervals below are zero-based content ticks `[begin,end)`, after `advance()`
and before the next tick. These are scheduler/clock observations, not captured GPU
pixels or wall-clock/frame-rate measurements.

| Authored composition | Observed one-traversal schedule |
| --- | --- |
| Root sequential cap 2000, A600 + B600 | A `[0,600)`, B `[600,1200)`; **root completes at 1200** |
| Parallel owner 2000, nested sequential cap 2000, A600 + B600 | A `[0,600)`, B `[600,1200)`, inactive `[1200,2000)` |
| Root sequential cap 1000, A600 + B600 | A `[0,600)`, B `[600,1000)`; last 200 B ticks clipped |
| Root sequential cap 2000, A600 + B600, repeat 2 | A/B twice; completes at **2400**, not 4000 |
| Root sequential cap 2000, cadence 1200, A600 + B600 | A600, B600, A600, B200; completes at 2000 |
| Parallel cap 2000, A600 and B600 | Both `[0,600)`, neither `[600,2000)` |
| Parallel owner 2000, sequential cap 1200 repeat forever, A600 + B600 | A600, B600, A600, B200; completes at owner's 2000 |
| Parallel owner 1000, sequential cap 1000, A400 + B400 | A `[0,400)`, B `[400,800)`, inactive `[800,1000)` |
| Parallel owner 2000, nested sequential **duration omitted**, A600 + B600 | Inherits 2000-frame clock span, but completes at 1200; inactive tail |

The final A tick is frame 599, B starts at global tick 600 with its local frame 0,
and B's final local frame is 599. There is no extra boundary tick. The scheduler
keeps the final child active for its last executed tick, then its owner disables
it on the next tick.

Advancing the same completed root directly resets it first: A re-enters at tick
1200 in the first case. In the product, `Director::update()` notices completion
and calls `change_visual(elapsed_length())`; selection policy can pick another
visual. This is not evidence of an 800-frame leftover loop inside a 2000-frame
sequence.

An additional consequence: the 2000-frame sequence's occurrence clock still has
`length() == 2000` when it completes at position 1200. A curve reading that clock
ends at progress `1199/2000`, not at the end of its 0-to-1 domain. Effective
lifetime and authored clock span are distinct and should be visible in authoring.

## Grammar and execution evidence

- [`pattern_parser_v4.cpp`](../../../src/trance/visual/pattern_parser_v4.cpp):
  lines 359-381 parse integer/sample duration and explicit repeat; line 370 uses
  inherited span if duration is omitted. Lines 457-464 require output ownership
  by parallel and finite explicitly timed children under sequential. Lines
  502-503 require the root's explicit finite duration. There is no inferred root
  duration or completion-policy field.
- [`pattern_compiler.cpp`](../../../src/trance/visual/pattern_compiler.cpp):
  lines 23-38 compile every native occurrence through `OccurrenceCycler`.
- [`cyclers.cpp`](../../../src/trance/visual/cyclers.cpp): lines 203-220 skip
  complete parallel children and advance sequential children in order. Lines
  249-270 implement external root reset, cadence boundaries, the sequence-end
  condition and explicit iteration counts.
- [`director.cpp`](../../../src/trance/director.cpp): lines 199-206 connect
  root completion to product pattern selection using the actual elapsed length.
- [`v4_runtime_test.cpp`](../../../tests/v4_runtime_test.cpp): lines 48-55 already
  check inactive finite tails and external root restart; lines 59-65 already check
  that a long sequence does not silently loop, and repeat uses actual lifetime.
- The released [V4 specification](../../spec-grammar-v4.md#timing) describes
  explicit repetition and parent clipping. The exact early-end rule is clearer in
  [the detailed grammar](../v4/GRAMMAR-V4.md#ast-contract).

`duration` is currently a positive whole number of content frames or an aligned
sampled frame range. It is not authored in seconds. Omitting duration under a
parallel owner inherits a clock span; it does **not** derive a clock from child
lengths. Root omission is rejected, as is omission on a sequential child of a
sequential owner, even when that child's own finite children have a known sum.
The probe confirms both rejections. All accepted probe patterns have zero native
warnings, including clipped and inactive-tail examples.

## Smallest useful design direction

First expose the current schedule: authored duration/cap, effective lifetime,
child spans, repetitions, clipping, and inactive tails. Warn only where the
result is exact; use ranges/unknown for sampled or chance-controlled schedules.
Do not call intentional clipping an invalid pattern. An enclosing node can have
its own direct outputs, so a completed-child tail does not always mean a blank
screen.

The least disruptive duration inference would be explicit opt-in for a finite
sequence: derive its **iteration clock span** from finite children, including
their repeat counts. This differs from the current early completion and would
make parent-relative curves complete naturally. Require an explicit policy for
sampled durations, cadence, random exits and forever children before inferring.
Preserve current authored-duration behavior for existing AST documents.

| Completion policy | Basic bounded interpretation | Main decision needed |
| --- | --- | --- |
| Explicit duration | Current parallel duration cap; sequential may finish early | Retain exact semantics or add an explicit hold option |
| Child cycles | Existing sequential `repeat: N`; repeat each actual traversal | Distinguish repetition from cadence restarts/clipping |
| Sequential inferred | Sum of finite effective child lifetimes | Clock span and sampled-duration policy |
| Parallel all | Maximum finite child lifetime | Reject or explicitly bound forever children; direct outputs have no intrinsic duration |
| Parallel any | Minimum finite child lifetime, then clip siblings | Make truncation visible |
| Leader | Selected child's completion ends the group | Followers independently need once/loop/hold behavior |

The leader/follower analogy is useful, but should not silently determine whether
followers loop. Those are two separate choices. New all/any/leader grammar is a
design option, not implemented behavior or required for explicit repetition.

## Verification

The standalone [`timing_probe.cpp`](timing_probe.cpp) links the **production**
parser, compiler and scheduler. It checks nine exact schedules and two duration
omission rejections; no timing implementation is copied into the probe.

```powershell
cmake -S docs/exploration/timing-audit -B build/timing-audit -G 'Visual Studio 18 2026' -A x64
cmake --build build/timing-audit --config Release
./build/timing-audit/Release/timing_probe.exe
```

For other dependency layouts set `-DTRANCE_DEP_INCLUDE=...` to the directory
containing `nlohmann/json.hpp`. Recorded output is in
[`native-output.txt`](native-output.txt).

The existing `v4_parser_test`, `v4_runtime_test` and `v4_native_execution_test`
targets were rebuilt from the audited source and all three passed CTest.

[`Timing.lean`](Timing.lean) was **actually checked** with portable Lean 4.10.0
(commit `c375e19f6b65`), downloaded from its
[official release](https://github.com/leanprover/lean4/releases/tag/v4.10.0).
The exact downloaded Windows ZIP SHA-256 was
`993dff2fdccb9c078f88ff903861e3aa80e94e6e991f950fcc5658538796da97`.
Run `lean Timing.lean` from this directory using the pinned toolchain.

The proof establishes for a small finite two-child mathematical model that the
number of consumed ticks after `n` owner steps is
`min(n, min(cap, A+B))`; completion is absorbing, child spans have the expected
boundaries, and all ticks at or beyond 1200 are absent for cap2000/A600/B600.
It uses no `sorry`, custom axioms or disabled kernel checks. Standard Lean axiom
reports are preserved in [`lean-output.txt`](lean-output.txt).

This is **not a proof of C++/Lean correspondence**. It excludes cadence, offsets,
sampling, random selection, overflow, rendering and director selection. The native
probe independently checks representative C++ behavior. A full equivalence proof
would require an operational semantics and a verified correspondence layer, far
beyond the bounded question answered here.
