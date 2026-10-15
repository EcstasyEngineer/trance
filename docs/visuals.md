# Visuals

Every built-in and custom visual uses the v3 pattern compiler. A visual schedules
content effects; its program supplies themes, speed, colors, and an entrainment
beat period. See [authoring](authoring-v3-patterns.md), the
[grammar](spec-grammar-v3.md), and [runtime](engine-today.md).

## Built-ins

| CLI name | JSON type | Behavior |
|---|---|---|
| `accelerate` | `accelerate` | Increasing image cadence through a sampled ramp. |
| `slow_flash` | `slow_flash` | Slow and fast flashing phases. |
| `sub_text` | `sub_text` | Image with scrolling subtext. |
| `flash_text` | `flash_text` | Image crossfade with text. |
| `simple` | `parallel` | Single-image visual. |
| `super_parallel` | `super_parallel` | Three staggered image layers. |
| `animation` | `animation` | Animation and a crossfading image layer. |
| `super_fast` | `super_fast` | Rapid base cuts, bounded bursts, randomized word/animation accents. |

Sources: [builtin_patterns_v3.cpp](../src/trance/visual/builtin_patterns_v3.cpp).
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

Timed patterns retain their declared span and provide local clocks. Nested
schedules contribute to their enclosing cadence, including when nested under
sampled ramps. Bursts own child schedules: branch entry restarts the schedule,
direct effects fire on entry, and explicit `every` constructs repeat inside it.
Burst curves follow the sampled occurrence duration.

A curve's `over NAME` selects a clock to read. Reading an ancestor clock does not
replace child scheduling. The grammar reference defines clock scope and how an
indefinite base can obtain a meaningful timed curve.

## Pipeline and diagnostics

```text
v3 source → parser/lowering → Node tree + generated render statements
          → compiler → Cycler tree → effects/registers → per-frame rendering
```

Effects update registers and content slots; render statements read those and
cycler state for alpha, zoom, and motion. Image registers are pattern-scoped.
Text and theme audio have fewer independent content slots.

F1 shows the cycler tree and active phase labels. Theme stars identify visible
image layers drawn from live slots that frame. Still-cache counts and animation
counts are separate.

`trance --lint [session]` parses, lowers, compiles, and evaluates expressions
without a window. It is author feedback, not proof of every runtime schedule.
`phase_execution_test` additionally checks effects, activity, reset, and local
clocks across frame advances.

Implementation: [director.cpp](../src/trance/director.cpp),
[compiled_visual.cpp](../src/trance/visual/compiled_visual.cpp),
[render_eval.cpp](../src/trance/visual/render_eval.cpp).
