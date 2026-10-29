# Visuals

All eight builtins use the native V4 AST compiler. Custom visuals accept V4 text
or JSON through the same source loader. A program supplies themes,
speed, colors and entrainment. See the [V4 grammar](spec-grammar-v4.md),
[native runtime](engine-today.md), and
[composer](exploration/authoring/README.md).

## Built-ins

| CLI name | JSON type | Behavior |
|---|---|---|
| `accelerate` | `accelerate` | Increasing image cadence through a cadence ramp. |
| `slow_flash` | `slow_flash` | Slow and fast flashing phases. |
| `sub_text` | `sub_text` | Image with scrolling subtext. |
| `flash_text` | `flash_text` | Image crossfade with text. |
| `simple` | `parallel` | Two staggered primary/secondary image lanes. |
| `super_parallel` | `super_parallel` | Three staggered image layers. |
| `animation` | `animation` | Animation and a crossfading image layer. |
| `super_fast` | `super_fast` | Rapid base cuts, bounded bursts, randomized word/animation accents. |

Sources: [builtin_patterns_v4.cpp](../src/trance/visual/builtin_patterns_v4.cpp).
These express current intended effects; their frame sequences are not a
compatibility contract with retired hand-coded visuals.

## Selection and errors

The director combines enabled built-in and custom-pattern weights in one pool.
Saved visual pins restrict the pool. A visual can remain selected for more than
one cycle.

`--visual=NAME` forces a built-in; `--pattern=FILE` forces a source file.
The flags are mutually exclusive. Runtime `visual` also accepts active-program
custom names. `unload pattern` releases a runtime force.

Built-in parse failures are fatal. A session custom pattern with invalid syntax
is skipped with a warning; other visuals remain eligible. A missing referenced
file fails the session load. Invalid explicitly forced source reports a
diagnostic rather than silently choosing another visual.

## Nested timing

V4 has three composition kinds: parallel, sequential and random. One occurrence
scheduler handles their timing, cadence/ramp, phase offsets, explicit repetition,
and subpattern looping. A finished finite child remains inactive unless its owner
loops it. Inherit calculates a pattern's length from its finite timed subpatterns
at any nesting level. Omitted durations still use the enclosing window. Random patterns expose random or round-robin order,
self-selection, cooldown and probability checks on child edges.

Local motion clocks are distinct from total elapsed program duration. Structured
curves can read local, scene or named ancestor progress.

## Pipeline and diagnostics

```text
V4 text / JSON AST → native compiler → occurrence scheduler + output state
            → per-frame rendering
```

Scheduled selections update output state; rendering reads that state and
occurrence clocks for opacity, zoom, and motion. Each image owns its capture.
Text and theme audio have fewer independent content slots.

F1 shows the cycler tree and active phase labels. Theme stars identify visible
image layers drawn from live slots that frame. Still-cache counts and animation
counts are separate.

`trance --lint [session]` parses, lowers, compiles, and evaluates expressions
without a window. It is author feedback, not proof of every runtime schedule.
`v4_runtime_test` and `v4_native_execution_test` check scheduling, image ownership
and animation behavior across frame advances.

Implementation: [director.cpp](../src/trance/director.cpp),
[compiled_visual.cpp](../src/trance/visual/compiled_visual.cpp),
[render_eval.cpp](../src/trance/visual/render_eval.cpp).
