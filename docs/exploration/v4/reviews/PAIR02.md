> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 02: Slow Flash and Super Fast

Status: independent source review of production V3 plus one existing native execution test run. This report specifies discriminating V4 expectations; it does not claim an unimplemented V4 scheduler has passed them.

## Verdict

Three composition kinds are sufficient for both recipes, provided scheduling remains structured occurrence data. Slow Flash needs sequential child completion, explicit repetition, and local versus ancestor clocks. Super Fast needs an exclusive selector with child-specific exit rules, controller-relative chance checks, sampled duration, cooldown, and fresh activation. Merely renaming `burst` to `random` would lose behavior.

The controller name used only for debugging can disappear. Neither recipe needs authored image registers. `runtime` content selection is independent fair choice of primary or secondary on a selection event; it is not a scheduling clock and should become `either`.

## Slow Flash: exact nominal duration is 3072 frames

Production source is `pattern slow_flash for 1536f loop 2 seq` containing a 1024f slow parallel scene and 512f fast parallel scene. `parse_pattern` makes the declared 1536f span authoritative and wraps it in a two-count repeat. The compiled recipe is therefore 3072f, not 1536f. Lowering `repeat 2` by dividing a 1536f total would halve the intended time.

An equivalent conceptual V4 tree is a sequential composition with a 1536f occurrence repeated twice, containing two bounded parallel children. Slow contains an image occurrence repeating every 64f, caption every 64f, and continuous spiral. Fast contains image every 8f, word every 16f, and continuous spiral. The fast image zoom reads the 512f fast ancestor rather than its own 8f occurrence.

Expected half-open active intervals and entries:

| Frames | Active child | Image entries | Text entries |
| --- | --- | --- | --- |
| 0..1023 | slow | 0,64,...,960 | caption 0,64,...,960 |
| 1024..1535 | fast | 1024,1032,...,1528 | word 1024,1040,...,1520 |
| 1536..2559 | slow | 1536,1600,...,2496 | caption same frames |
| 2560..3071 | fast | 2560,2568,...,3064 | word 2560,2576,...,3056 |
| 3072 | next complete visual repetition | slow image and caption | no fast draw |

One nominal visual therefore has 32 slow image selections and 128 fast selections. Every second slow selection animates: 16 requests. Fast zoom is `0.8 * local_fast_frame / 512`: at frame 1032 it is 0.0125, not zero; at 1535 it is 0.7984375; at 2560 it resets to zero. Slow image zoom resets every 64f. These observations falsify a naive all-leaf-local curve rewrite.

The generic V4 rule for a sequential parent longer than its children must be explicit. V3's sequence cycler automatically repeats its children while the enclosing timed phase remains active. Slow Flash's child sum equals the occurrence span, so it does not expose this distinction; a 10f sequential parent with two 3f children does. Decide whether that case repeats, holds, idles, or requires an explicit repeat modifier, and test that decision rather than inferring it from this recipe.

## Super Fast: deterministic stress fixtures

The builtin is a 2048f parallel scene: an exclusive burst controller alongside word every 8f at chance 0.25 and spiral. In its rapid child image selection is every 8f. In the held child one animated image is selected once, with zoom over its sampled lifetime. Outside the selector words and spiral continue through both children.

Use an injected always-success exit coin and a fixed 64f hold to establish the state machine independently of the builtin's probabilistic trace:

| Frame | Expected action |
| --- | --- |
| 0 | Initial chance check succeeds; enter held image directly; no rapid image selection |
| 0..63 | Held image only; one selection total; zoom `0.1 + 0.6*f/64` |
| 64 | Hold completes; restart rapid child; first rapid image selection; skip chance check on this return frame |
| 72,80,88,96,104,112,120 | Rapid image entries; cooldown counts eligible 8f boundaries |
| 128 | Cooldown expires; success enters a new held image; no rapid selection at 128 |
| 192 | Return to a freshly restarted rapid child; next interruption earliest256 |

With fixed 128f hold, corresponding entries are hold0, rapid128, hold192, rapid320. A selector that merely chooses a child when the rapid child finishes cannot represent this recipe: the rapid child has no predetermined completion. A selector that reconsiders its choice every frame both changes interruption probability and consumes extra random draws. A selector that randomly chooses between both children can select rapid again, or held again, and fails the forced alternation contract. The builtin therefore uses round robin with no self transition; the proposed true-random policy is a separate supported option.

The actual builtin checks chance1/12 at controller-relative boundaries0,8,16,... when eligible. At initial failure, rapid selects at0; if the next eligible success is8, rapid does not select at8. A switch is applied before either outgoing or incoming child's frame work, and only the selected child advances.

Duration sampling must be uniform over nine values64,72,80,88,96,104,112,120,128, once per entry. Uniform integer sampling of every frame64..128 changes the distribution. A 2048f parent that cuts off a hold entered2016 with sampled128f permits32 rendered hold frames, with final local zoom `0.1 + 0.6*31/128`; it does not compress the full curve into the clipped32f remainder or resume that hold on the next visual repetition.

## Completion, clipping, and generalization traps

1. **Cooldown cannot delay completion.** A child ending before selector cooldown expires must transition at its completion boundary. Cooldown gates probabilistic exits only. The held child cannot accidentally overrun because its completion was checked inside a chance gate.
2. **Transition-frame suppression matters even with cooldown0.** If a held24f child returns to rapid at24 under period8/chance1, the earliest next interruption is32, not24. This prevents an invisible rapid activation and two transitions in one frame.
3. **Clock anchoring matters outside this builtin.** A controller period8 with a held child ending21 has next eligible check24, not29. Super Fast's aligned hold lengths conceal this distinction. Define whether a cooldown counts grid ticks or real elapsed frames for nonaligned entries; V3 authors cooldown in frames but rounds to period ticks.
4. **Inactive children have no side effects.** During a hold there are no rapid image selections, animation loads, alternation flips, or extra chance/source random draws. An AST visualizer inspecting inactive children must not execute them.
5. **No privileged phase node is necessary.** Either selected child can be parallel, sequential, or another selector; entry, restart, sampled lifetime and clipping belong to occurrences. A single image leaf is still an occurrence with retained image and normalized motion; deleting all wrappers is not semantics preserving.
6. **Self transitions restart.** If true-random mode permits selecting the same child, document whether that samples a fresh duration and new image. Recommend yes, with at most one transition per rendered frame; otherwise it is impossible to tell a self transition from continued activation.

## Current evidence and remaining proof

Inspected `builtin_patterns_v3.cpp`, `pattern_parser_v3.cpp::parse_pattern/parse_burst`, `pattern_compiler.cpp`, and `cyclers.cpp::SequenceCycler/RepeatCycler/PhaseCycler/BurstCycler` directly. Ran the existing `build/windows-msvc/tests/Release/phase_execution_test.exe`: passed (`phase execution, local clocks and SuperFast motion passed`). Its source observes phase-owned clocks, inactive branch suppression, fixed24/64/128f holds, sampled tick-aligned durations, nested sequence clipping, and parent-reset behavior.

The builtin-specific tables above are source-derived expectations, not new native trace output. V4 acceptance must compare them against the new runtime using controlled randomness and actual render expressions, alongside a 3072f Slow Flash trace and parent truncation during a sampled hold. Grammar acceptance alone is insufficient.
