> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Three-kind composition audit

Status: source-backed design audit, not a claim that a V4 runtime exists. The
three public container kinds can be sufficient. Replacing `burst` with the word
`random` alone is insufficient: transition and occurrence semantics must survive.

## Findings against current implementation

| Assumption | Verdict | Evidence / consequence |
| --- | --- | --- |
| Parallel and sequential are different composition kinds | Yes | `parse_pattern` currently represents both as `Node::Type::Phase`, with `sequence` selecting a child `Seq`. Exposing distinct kinds removes a hidden behavioral switch. |
| Every composition kind may contain every other | Coherent with ownership rules | Compiler recursion already permits nested schedules, but the V3 parser restricts burst phase structure and some statically bounded expressions. V4 should validate timing requirements, not forbid nested kinds. |
| Burst is a random pattern | Only with transition properties | `BurstCycler::advance` is an interruption state machine, not a random child picked whenever the old child finishes. |
| Burst's `rapid` clock name is necessary | No for builtins | Super Fast never refers to `rapid`; removing its public label changes no builtin expression. Internal occurrence identity remains necessary. |
| Named clocks are all unnecessary | No | Accelerate reads whole-scene `accelerate.progress`; Slow Flash's fast image zoom reads `over fast`. Removing every ancestor reference changes their motion. Optional names are useful only where referenced. |
| Interrupt chance is per frame | No | Super Fast checks `1/12` only at eligible 8-frame controller boundaries, outside the held phase and cooldown, including the initial boundary. |
| Default/interrupt phases must remain special node kinds | No | They can be ordinary child patterns with different exit properties. The active child alone advances and renders. |
| All short or single-child wrappers are redundant | No | An occurrence can own a private held image, a local motion clock, sampled duration, phase offset or entry event even when it has one visible child. |
| A random selector can replace an image content selector | No | Selecting an active subtree and selecting one of two themes for an image/text event are different operations. `runtime` presently means the latter. |

Primary evidence:
[builtin recipes](../../../src/trance/visual/builtin_patterns_v3.cpp),
[AST](../../../src/trance/visual/pattern_ast.h),
[parser: parse_pattern and parse_burst](../../../src/trance/visual/pattern_parser_v3.cpp),
[compiler](../../../src/trance/visual/pattern_compiler.cpp),
[scheduler: PhaseCycler and BurstCycler](../../../src/trance/visual/cyclers.cpp),
[V3 ownership contract](../../spec-grammar-v3.md).

## Smallest coherent AST

Exactly three **composition kinds**: `parallel`, `sequential`, `random`.
Images, words, captions, spirals and audio are typed output operations, not more
composition kinds. Timing is structured data attached to occurrences/child edges,
not new user-visible container kinds.

An occurrence needs an internal identity, optional human label, ordered children,
entry operations, local duration, repeat policy, optional phase offset and owned
held-image storage. A repeated occurrence has a fresh local clock; an operation's
alternation/animation count retains authored identity across those occurrences.
Those two lifetimes must not be conflated again.

- `parallel`: activate all children in source order; source order determines image
  stacking. Each child advances while its parent is active.
- `sequential`: activate one child, moving to the next on completion. Finite parent
  duration clips the sequence. Repetition must be explicit and bounded/validated.
- `random`: activate one child. On a transition, choose the next child using
  `round-robin` or `random`, with an explicit `allow-self` option. This is a selector
  family; the UI must show its actual policy rather than color every selector as
  nondeterministic. A selected child can itself be any of the three kinds.

Child exit properties need only two mechanisms: completion of a bounded duration,
or a scalar probability tested at an explicit controller-relative interval. A
selector-wide cooldown gates probability checks after a transition; it must not
prevent a finite child's scheduled completion. Activation restarts the selected
subtree; inactive subtrees neither advance, render nor consume random draws.

Probability is a bounded scalar for this version. General expressions are not
needed to represent the eight recipes. The AST should preserve `1/N` rational
chance accurately rather than silently converting it to an integer percentage.

### Super Fast without privileged phases

One selector with two ordinary children is enough:

| Property | Rapid child | Held child |
| --- | --- | --- |
| Kind | parallel | parallel |
| Work | image occurrence every 8f | one animated image occurrence |
| Lifetime | until chance exit or enclosing cutoff | sample 64..128f in 8f steps at entry |
| Exit | chance 1/12 at controller 8f boundaries | completion |
| Image motion | 0.0625 to 0.1875 per cut | 0.1 to 0.7 over sampled lifetime |

Selector properties: start at child 0, round-robin, no self-transition, 64f
probability-check cooldown after a transition, initially eligible. The held child
lasts at least 64f, so cooldown while it runs is harmless; its completion returns
to the rapid child without introducing a special return target. On return, the
rapid child's first eligible chance check is 64f later. The exit boundary itself
does not immediately perform another transition.

This reproduces Super Fast's existing controller behavior if check intervals stay
anchored to the controller clock. Re-anchoring checks to every child's entry can
differ for nonaligned durations, so that rule must be explicit. Sampled durations
are drawn once per activation and clip at the parent boundary. Do not draw random
values during offset pre-roll or inactive-branch inspection.

For random order, choose uniformly from eligible child indices. With self disabled,
exclude the current child; with only one child, keep it until explicit completion
and restart once, rather than spinning in a zero-time transition loop. A self
transition means an actual restart and must be visible in validation, rather than
silently treating it as no change.

## Stress map for all eight recipes

| Recipe | Structural mapping | Load-bearing behavior to preserve |
| --- | --- | --- |
| Accelerate | parallel scene; sequential sampled cut occurrences | 140 durations fill 2048f; image and word share cut clock; animation every fourth authored selection; scene origin and local zoom coexist. |
| Slow Flash | repeated sequential pair of parallel scenes | 1024f slow then 512f fast, twice; fast zoom follows the 512f parent rather than each 8f cut. |
| Sub Text | parallel scene | independent image64f and subtext32f; image every third selection animates; either-theme selection. |
| Flash Text | parallel scene containing two parallel image lanes | 128f lifetime and 64f phase offset; opaque bottom and triangle top; source-over order; private images. |
| Simple | parallel scene | image64f, caption32f, independent choices and clocks; third-selection animation. |
| Super Parallel | parallel scene containing three parallel image lanes | offsets0/32/64 in96f; opacity1/.5/.33; three held images and stable stack order. |
| Animation | parallel scene with motion and visiting image lanes | offset48f; real 33-frame absence in64f visitor cycle; envelope not interchangeable with triangle fade; shared runtime animated streams remain a separate resource question. |
| Super Fast | parallel scene containing round-robin selector | one active image phase; probability1/12 every8f, sampled64..128f hold,64f cooldown, restart-on-return; words and spiral continue beside selector. |

## Required discriminating validation

1. Compare old/new operation entry frames, active images, clock-derived zoom and
   alpha across at least two complete scenes for every recipe. Merely accepting
   the new syntax does not establish equivalent behavior.
2. Assert inactive selector children perform zero picks, animation loads and RNG
   calls. Test deterministic injected chance sequences, including initial success,
   shortest/longest hold, cooldown expiry and parent cutoff during a hold.
3. Test every nesting pair of the three kinds, including random-in-random. Test
   self enabled/disabled, one/zero children, all child exits on the same boundary,
   zero duration, sampled maximum overflow and nonaligned check intervals.
4. Verify held-image independence with two and three overlapping lanes; verify
   schedule reset does not reset animation accent identity or alternation state.
5. Check content selection independently: fixed primary/secondary, alternating,
   either theme; a single animated image occurrence must resolve one side for
   both still fallback and animation, without forcing unrelated text to that side.

## Eight paired review assignments

Eight genuinely separate reviewer tasks can each receive two recipes. This balanced
assignment covers each recipe twice, with independent emphasis and no duplicate
pair. Shuffle reviewer labels at dispatch; the coverage matters more than drawing
pairs with replacement and accidentally omitting a recipe.

| Reviewer | Pair |
| --- | --- |
| A | Accelerate; Flash Text |
| B | Slow Flash; Super Fast |
| C | Sub Text; Super Parallel |
| D | Simple; Animation |
| E | Accelerate; Super Fast |
| F | Slow Flash; Animation |
| G | Sub Text; Flash Text |
| H | Simple; Super Parallel |

Each reviewer should try to break the model, propose simpler representations,
identify allowed intentional differences, and report a concrete failing trace or
an executable validation. Agreement is not established by copying another review.
