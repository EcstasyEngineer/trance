# Minimal V4 text: necessity challenge

Review of the eight literal native sources in `src/trance/visual/builtin_patterns_v4.cpp`, the native V4 parser and current grammar, plus the two authoring review reports. This is a proposed text subset, not a replacement schedule implementation.

## Decision

Use explicit `parallel`, `sequence` and `cycle` blocks. Keep every authored composition wrapper: even a one-output wrapper owns a clock, activation and capture lifecycle. Do not hide it behind a timed-output shortcut. Text lowers to the existing JSON AST; native validation and execution remain authoritative. Outside-subset ASTs remain usable as JSON, but text formatting must report an unsupported form rather than silently discard fields.

`cycle` has precisely the selector used by Super Fast: round robin, no self selection, first child zero, optional numeric cooldown. Do not expose unused random selection policies. A rational probability is a literal in its probability position, never arithmetic division.

## Necessity census

| Construct | Actual caller |
| --- | --- |
| Parallel blocks, integer duration, numeric cadence | All eight |
| Sequence and finite repeat | Slow Flash |
| Cycle, cooldown, chance exit, sampled duration | Super Fast |
| Cadence ramp with early easing | Accelerate |
| Named ancestor curve | Slow Flash, `fast_section` |
| Scene curve and two-curve sum | Accelerate |
| Local linear curve | All eight |
| Phase offset | Flash Text, Parallel, Super Parallel, Animation |
| Image, word, caption, subtext, spiral | Collectively the eight |
| Primary, secondary, either, alternate image sources | Collectively the eight |
| Image animation interval | Seven built-ins |
| Image zoom baseline and alternate flip chance | Accelerate |
| Word visibility and selection chance | Accelerate; selection chance also Super Fast |
| Image constant opacity | Parallel, Super Parallel |
| Image triangle opacity | Flash Text |
| Image frame envelope | Animation |

No caller justifies line output, forever repeat, scalar easing controls, late ramp easing, random order, selector first-index options, self-selection, weights, text opacity, image selection chance, arbitrary expressions, user functions, registers, or generic JSON escape syntax. Root `id` occurs in all eight and must survive. There is no `name` or `weight` field in these eight AST sources.

## Concrete shared spelling

```text
parallel id flash_text for 1024 {
  parallel every 128 {
    image secondary zoom 0 -> 0.8;
  }
  parallel every 128 phase 64 {
    image secondary zoom 0 -> 0.8 opacity triangle(0, 1, 0);
  }
  parallel every 64 { word secondary; }
  parallel every 32 { caption primary; }
  spiral speed 2;
}
```

Other required forms are `for sample(64,128,8)`, `every ramp(56,12,140,early)`, `repeat 2`, `sum(0 -> 0.4 @scene, 0 -> 0.1)`, `0 -> 0.8 @fast_section`, and image `envelope(8,16,8)`. Keep sum to two linear curves, triangle to image opacity, envelope to images. Local linear curves need neither an easing clause nor a scope clause.

Place chance exits on edges, visibly outside the node header:

```text
cycle cooldown 64 {
  exit 1/12 every 8: parallel {
    parallel every 8 { image either zoom 0.0625 -> 0.1875; }
  }
  parallel for sample(64,128,8) {
    image either zoom 0.1 -> 0.7 animation 1;
  }
}
```

The exit interval and an enclosed cadence can then never be confused. Choose one spelling per field, reject duplicate clauses, and preserve child source order. Decimal and scientific notation must lex as complete numeric literals, never be passed through an authored expression evaluator.

## Authorized completion extension: adversarial cases

`child_mode` has one mutually exclusive value: independent, loop or wait. Independent remains the default, with warnings for provable inactive frames. Loop restarts a completed sequence body, or each completed parallel child, until its owner ends. A restart must preserve native authored-output selection counters and alternating state. Reject zero-time restart loops.

The agreed wait extension omits `for` and accepts only statically deterministic finite gating child compositions; direct outputs follow their owner and do not gate completion. Reject a wait owner with cadence or phase, sampled gating durations, an unbounded selector child, or no finite child gate. Existing ancestors still own and may clip descendants.

Do not implement wait as an eager sum or maximum of authored duration fields. A sequential child capped at 2000 with two 600-frame children currently completes at 1200. Summing its cap overstates actual execution. For a plain sequential child use its effective completion, `min(cap, sum(child completion))`; a cadence owner runs to its cap; include finite repeat. Reject sampled or otherwise uncertain gating subtrees rather than drawing random values during duration analysis. The derived finite span and native completion must agree.

A statically derived wait span supplies the progress denominator; runtime samples or completion guesses must not alter it. Reject phase on the wait owner itself in this initial extension. Ordinary bounded descendants retain phase. Finite repeat on a wait owner is unnecessary for the built-in baseline and should not be added accidentally.

## Acceptance evidence

Require eight source ASTs to survive AST -> canonical text -> AST, modulo documented insertion/removal of default local scope only. Compare native schedule and recording-renderer traces with fixed random seeds, including phase pre-roll and Super Fast RNG order. Formatter rejection tests must prove unsupported fields are not dropped. Exercise independent inactive tails, loop in both sequence and parallel, wait with early-finishing capped sequence, rejection of sampled gating children, ancestor clipping, and agreement between derived progress and native completion. These are proposed checks, not claims of completed implementation tests.
