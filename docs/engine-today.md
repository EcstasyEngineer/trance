# Visual runtime model

The v3 language lowers to a schedule tree plus an ordered render block. The
schedule chooses media and updates state. The render block reads that state and
draws it. A new language feature must have a concrete lowering to those parts,
or explicitly require a runtime change.

The [language reference](spec-grammar-v3.md) defines syntax;
[visuals.md](visuals.md) maps the implementation. This page explains the model.

## Schedule, state, rendering

`pattern_parser_v3.cpp` produces `pattern::Node` and `pattern::RenderStmt` data.
`pattern_compiler.cpp` turns nodes into `Cycler` objects. `CompiledVisual`
connects the nodes' ordered effects to `VisualControl` and owns their registers.
`render_eval.cpp` evaluates the render statements against current clocks and
registers, then calls `VisualRender`.

Selecting an image and drawing an image are separate operations. An image
effect captures a selection into a register when its schedule fires. A render
statement can then draw that register every frame with changing zoom or opacity,
without selecting a different image. `show` hides a draw; it does not stop that
draw's selection schedule.

The runtime can select images, animations, text, fonts and themes; control theme
audio; and draw images, text, captions, subtext and spirals with supported numeric
parameters. Warp and theme-audio volume also have render-time parameters. Scalar
effects support bounded internal state such as chance flags, alternating sides,
and every-Nth accents. These IR operations are not all public grammar verbs.

## Clocks and ownership

A cycler has a length, position, active flag, and children. On each content tick,
the root advances the parts of the tree it owns. After the first advance,
`frame == 0`; `progress == frame / length`. A finite N-frame occurrence therefore
visits `0, 1/N, ..., (N-1)/N`, not an extra endpoint frame at 1.

| Node | Scheduling rule |
|---|---|
| `Action` | Fire ordered effects on frame zero of each period. |
| `OneShot` | Advance unfinished children together; duration is their maximum length. |
| `Parallel` | Advance children together, allowing repeats; duration is their least common multiple. |
| `Sequence` | Advance one child at a time; duration is the sum of child lengths. |
| `Repeat` | Repeat one child a fixed number of times. |
| `Offset` | Start a child at a phase offset; pre-roll suppresses effects. |
| `Phase` | Run entry effects once and advance owned child schedules during a bounded occurrence. |
| `Burst` | Choose between a base phase and a sampled-duration phase; advance only the chosen branch. |

Language scopes and runtime nodes are related but distinct. Clock names resolve
lexically while parsing; compiled node IDs identify the clocks read by render
expressions. A child may read an ancestor without sharing that ancestor's
scheduling state.

A `pattern ... for Nf` owns exactly N frames per iteration. Its children run
inside that lifetime and restart with it. An `every Nf` body with nested schedules
likewise owns those children for each N-frame occurrence; ramp segments own their
sampled segment lengths. Simple effect-only cadences can compile to action leaves
without an extra wrapper. A sequence changes the order of children without
extending the enclosing pattern's lifetime.

For example, `pattern scene for 25f { every 8f { image primary } }` selects at
frames 0, 8, 16 and 24. The last cut is clipped after one frame when the scene
ends. The declared scene lasts 25 frames rather than being rounded down to three
complete cuts. A cadence longer than its parent can still fire once and be
clipped. Clock IDs remain distinct even when a scope contains only one child.

A scene can also have a slow overall approach while images change every
8 frames. The image's local curve restarts each cut; an expression reading
`scene.progress`, or a curve using `over scene`, follows the larger scene.
This already works across named enclosing scopes. There is no need for a new
parent-timer feature to express a long envelope over short cuts.

Clock access is read-only. `over NAME` selects which progress a curve reads; it
does not reset the parent, change its speed, or replace the child's lifetime.
Those would be separate features with different scheduling consequences.

## Burst phases and the SuperFast repair

A burst controller checks its probability on period boundaries. Durations and
cooldowns are authored in frames, rounded up to whole controller periods. On
entry, one duration is sampled and retained for that occurrence.

- Direct branch effects fire once on entry. `enter` effects run before the
  burst body's direct effects.
- A branch owns its nested schedules. Inactive branches neither advance those
  schedules nor make their media selections.
- Re-entry resets child schedule positions. A child still running when the
  branch ends is cut off at that boundary.
- The burst's default curve clock is its sampled local duration. An explicit
  timed child has its own clock and can use a named ancestor with `over NAME`.
- The base can be interrupted by a random decision, so it has no known
  normalized endpoint. Put repeating motion inside `every Nf`, use elapsed
  frames, or explicitly name a finite ancestor. Bare base progress/length
  expressions are rejected.

A nested burst controller inside an indefinite base or sampled-duration branch
also lacks a fixed enclosing length. Its controller `.progress`/`.length`
cannot be used as a substitute for the owning phase's sampled clock. Use the
named phase for that envelope; controller `.frame`/`.index` remain available.

SuperFast uses an 8-frame local zoom for rapid cuts and a separate 64-128-frame
zoom for the held burst image. The hold selects media once and changes its
transform throughout the hold. Consequently a still substituted for a missing
animation also moves. The correction concerns owned execution and local clocks,
as well as authored curves; it is larger than merely counting another timer.

Schedule reset does not imply clearing every piece of visual state. Registers
belong to the compiled visual, and selector flags/counters have their own
lifetime. Do not assume that re-entering a branch wipes stored images or every
hidden scalar. Tests should state which state must restart.

## Content and registers

The live theme interface has two sides: `primary` and `secondary`. The grammar's
`alternate` content selector toggles between those sides; it does not name a
third theme. A program can rotate through many themes over time.

`ThemeBank` selects from each theme's own content, including explicitly inherited
content. Still/animation requests are preferences: when that kind is unavailable,
the bank can use the other kind, then keep the side's last good frame. A theme
with nothing available and no previous frame can still draw nothing.
Failed decodes are excluded from cache capacity and reported separately in F1.
A selection miss checks actual resident content before falling back, and a
cache replacement must load successfully before its outgoing image is removed.

Image registers also record their source side and its generation. When a theme
changes, `CompiledVisual::refresh_stale_registers()` retries a current-theme pick
on the first render pass until it succeeds. Initial empty captures and captures
served from an old last-good fallback also retry, without waiting for another
theme change or scheduled image effect. A `copy` register is a snapshot and
is excluded from that automatic refresh, preserving the outgoing image in a
crossfade.

An animation-only theme is read through its live animation frame when drawn,
so capturing an image does not freeze it permanently. Animation frame delays
come from the file. The current animation selector is shared runtime state,
not a separate independent video player for every named image register.

Image registers are scoped to patterns. Cadences supply clock scopes without
creating new image-register scopes, except sampled ramp segments, which currently
have their own register scopes. Text is shared renderer state rather than
an image-like text register file, so independent text crossfades are not
available simply by copying an image recipe.

## Playback and presentation

Pattern counters use integer content ticks at `global_fps`; the application still
has wall-clock timing, audio timing, and display pacing. See
[architecture.md](architecture.md#timing-and-execution-ownership) for the split.

Rendering may evaluate the same pattern for two eyes and a desktop output.
Those evaluations must not run the schedule again. Accumulating render state
such as spiral phase and warp time is advanced once using the frame's playback
elapsed time. Content selection resolved for the frame is reused across output
passes.

## What verification establishes

`--lint` checks parsing/compilation and samples expression evaluation; it does
not establish execution correctness. `phase_execution_test` advances actual
compiled schedules and checks phase entry, clipping, restart, clocks and
SuperFast transforms with a lightweight effect recorder. It does not run real
media decoding or GL drawing. These are regression tests, not a formal proof
of all legal programs. See [coverage and limits](architecture-maturity.md).
