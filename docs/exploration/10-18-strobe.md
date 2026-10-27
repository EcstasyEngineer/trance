> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../spec-grammar-v4.md).

# Strobe composition investigation: tasks 10-18

Status: exploration against the current v3 parser, scheduler and render dispatcher.
No production behavior changes. Recipes and an executable recorder test accompany
this report. The initial five-recipe target passed in the full Release build and
CTest run; the parent exploration handoff records final results after the sixth
prototype fixture. Source inspection and test coverage below are distinguished.

## 10. What exists, and what historical follow-up is established?

The current grammar has the necessary mechanism for **gating a selected image or
animation**: `show` lowers to a render predicate, including a frame window or a
modulo expression. It has no `strobe` keyword, solid-color draw, whole-scene
postprocessing gate, or separately configurable flash generator.

`builtin_patterns_v3.cpp` describes `accelerate`'s increasingly rapid media
changes as a strobe. `slow_flash` switches from 64-tick to 8-tick selections.
Those are existing media-change effects, not evidence of an implemented
light/dark oscillator. The authoring guide already demonstrates image visibility
windows. The earlier checkout's source/docs and bounded history-message searches
also contain strobing references, chiefly the built-in intent and a corrected
animation fallback that previously selected a new still every read. That bug is
the opposite of a deliberate visibility gate.

The user's earlier specific strobe request is **not established as completed** by
those references. This investigation supplies a concrete grammar-only follow-up
with testable semantics, rather than inferring completion from the word
"strobe". No previous history was merged or used to replace current source.

## 11. Grammar-only recipe: hold content, pulse visibility

Executable asset: [held-still.pattern](../../tests/exploration/strobe/held-still.pattern).

```text
pattern held_still for 48f {
  image primary show [this.frame % 12 < 3]
}
```

One image selection fires on scene entry. It is drawn on ticks 0-2, 12-14,
24-26 and 36-38, and absent on the other ticks. The 48-tick scene repeats if its
cycler continues running. The recorder tests two complete iterations. This is
the simplest implementation of independent **selection lifetime** and **visibility
cadence**: adding a cadence containing `image` would select new content as well.

An equivalent gate can be expressed structurally with `every 12f { draw cur
show 0f..3f }` after a hidden selection, as in the underlay recipe. This form is
a better fit for a structured editor and avoids making users type expressions.
Its ownership must still be visible: nesting it under a shorter cut restarts
the gate at every cut boundary.

## 12. Images, animation, and theme fallback

Executable asset: [held-animation.pattern](../../tests/exploration/strobe/held-animation.pattern).
Adding `anim` requests animation once on scene entry and sends the same gate
through `render_animation_or_image`. The recorder checks that each scene selects
one still and requests one animation; none of its visibility pulses reloads them.

The media behavior is broader than the recorder proves. Source inspection of
`VisualApiImpl::render_animation_or_image`, `ThemeBank::advance_frames` and
`AsyncStreamer::advance_frame` establishes the following:

- The gate controls drawing, not animation playback time. Streamers advance on
  content ticks with the animation file's frame delays while the layer is hidden.
- A missing animation uses the still fallback. A theme containing only animation
  can produce live motion even through an ordinary image draw. An editor must
  distinguish requested media kind from the selected theme's actual fallback.
- `anim every Nth` counts selections, not visibility pulses or schedule ticks.
  For an image selected once per scene, every fourth means every fourth scene
  selection; it does not mean every fourth flash.
- Still registers are scoped, but animation draws share the last selected
  animation side for the compiled visual. They are not independent animation
  registers. Concurrent sibling clips must not be represented as isolated
  animation streams in an authoring UI.
- Theme generation changes may refresh a live selected image. "Held" means no
  new grammar-driven pick during pulses, not an immutable pixel snapshot across
  every external theme change. `copy` is the snapshot mechanism.

Actual decoding, fallback pixel output, and temporal behavior of a real GIF are
outside this recorder test. Existing `theme_bank_test` supplies separate media
coverage; it must not be conflated with this test's dispatch assertions.

## 13. Three operations that should have different names

| Intent | Existing implementation | Off-phase result |
| --- | --- | --- |
| Pulse one held layer | `show` on image/draw | That layer is absent |
| Reveal background beneath a pulsed accent | Continuous background, then gated accent | Background remains |
| Change media rapidly | `every Nf { image alternate }` | No absent phase; new selection each cut |
| Opaque full-frame blackout or white flash | No dedicated primitive | Requires a design/runtime decision |

[reveal-underlay.pattern](../../tests/exploration/strobe/reveal-underlay.pattern)
selects both images once, draws primary first, and draws secondary on top for
three of every twelve ticks. The test asserts the exact recorded layer order
and the continuous primary draw. [switch-media.pattern](../../tests/exploration/strobe/switch-media.pattern)
changes theme sides every six ticks and has a draw on every tick. It is a distinct
effect even when the user perceives both as flashing.

`render_eval.cpp` preserves source order. `show` only removes that statement;
it cannot hide unrelated captions, spiral, sibling images, or UI. `alpha 0`
still dispatches a zero-alpha layer and has different API side effects from
an absent draw. Changing the order of parallel-looking visual blocks can change
the composite result, so the editor needs an explicit layer stack.

`ScreenRenderer::render` clears the target before drawing. With no other layers,
an absent image leaves its cleared black target. This does not implement an
opaque blackout layer that can cover siblings. Output composition matters:
desktop overlay currently has whole-window opacity, and the source's zero-alpha
clear is not sufficient evidence of per-pixel desktop transparency. No live
overlay/headset experiment was performed here.

## 14. Tick quantization, duty cycle, and clocks

Let `F` be configured content ticks per second, `P` the integer pulse period,
and `K` the integer on-duration. Nominal pulse frequency is `F/P`; duty is `K/P`.
At `F=60`, the held recipe means 5 cycles/s, three on ticks and nine off ticks,
25% duty. At `F=120`, the same source means 10 cycles/s. These are schedule
calculations, not measured presentation frequencies. Headset refresh rate is
not a substitute for `F`.

For alternating media every `N` ticks, selections occur at `F/N`, but the full
primary-secondary cycle takes `2N` ticks. The switch recipe thus has ten changes
per second and five complete two-side cycles per second at 60 ticks/s.

An editor taking Hz and duty should calculate `P=max(1, round(F/requestedHz))`
and `K=round(P*duty)`, then display achieved Hz and exact on/off ticks. This is a
**proposed conversion policy**, not current grammar behavior. A true pulsing
control should require `P>=2` and `1<=K<P`; allow steady hidden/steady visible as
explicit states rather than quietly pretending a quantized zero-length phase
still pulses. For fractional `show 0..d`, half-open sample positions select
`ceil(P*d)` ticks for positive non-integral `P*d`, not nearest rounding. Display
the achieved duty rather than silently mixing those conversion rules.

`locked`/`beats` use a rounded entrainment-derived period; they do not lock visual
phase to an audio oscillator. Bursts round duration/cooldown to their controller
period; a regular `every` visibility gate does not apply that extra rounding.
Frame windows exclude the endpoint. Main-loop catch-up may advance multiple
content ticks before a presentation, so an on-window can be missed by a slow
display. This test establishes discrete schedule output, not brightness, frame
pacing, perceptual effect, or synchronization on hardware.

## 15. Nesting, re-entry and clipping

Executable asset: [nested-gate.pattern](../../tests/exploration/strobe/nested-gate.pattern).
A 23-tick outer scene selects the held image. Its deterministic burst controller
has 10 active ticks followed by 4 cooldown ticks. Inside each burst, a six-tick
gate is visible for its first three ticks. The test runs two outer iterations:

```text
scene ticks       0..2  3..5  6..8  9..13  14..16  17..19  20..22
image draw        on    off   on    off    on      off     on
```

The final burst would continue past tick 22; the outer scene clips it. The next
scene starts afresh at tick 0. No image draw appears during the inactive base
branch. The image selected in the outer scope remains available to the nested
gate. Its register lifetime is independent of that gate's clock reset.

Named clock reads (`over scene`, `scene.frame`) only choose where modulation
values come from. They do not move a gate outside its parent, prevent re-entry
reset, or stop parent clipping. To maintain an independent pulse phase across
image cuts, put the pulse cadence beside the selection cadence, or use an
explicit scene-clock expression. A visual node connection must not imply that
reading an ancestor's clock changes schedule ownership.

## 16. Executable verification and its boundary

`strobe_composition_test` reads the six actual recipe files, runs the production
parser and `CompiledVisual` scheduler/effects, and invokes production render
evaluation against a recording `VisualControl`/`VisualRender`. CPU images with
different widths identify primary and secondary draws. No window or GL context
is created, and no real media or flashing output is played.

It asserts 526 content ticks and repeats every render as a second output pass:

| Test | Observable assertion |
| --- | --- |
| Held still, 96 ticks | Exact 3-on/9-off output; one pick per 48-tick scene |
| Held animation, 96 ticks | Same gate; one animation request per scene; animation dispatch mode |
| Underlay reveal, 48 ticks | Base always first; accent last only when on; exactly two picks |
| Media switch, 48 ticks | One draw every tick; side changes only at six-tick boundaries |
| Nested burst, 46 ticks | Gate re-entry, inactive suppression, outer clipping, image lifetime |
| Prototype default, 192 ticks | Five selections; 16 windows; 64 visible accent ticks; cut-clock zoom |

[prototype-default.pattern](../../tests/exploration/strobe/prototype-default.pattern)
mirrors the structured authoring prototype's default emitted source: a continuous
secondary backdrop, primary selection every 48 ticks, and a 4-on/8-off gate
inside each cut. The recorder verifies every visible zoom sample against the
prototype's cut-relative model. It also explicitly observes the zero-alpha
selection draw on every tick, separately from the positive-alpha gated draw.
This establishes default recipe/model correspondence; arbitrary user-edited
prototype configurations are not an exhaustive production equivalence claim.

Each recipe must parse without warnings. Repeated output passes must record
identical draws. This is stronger than parser success or an evaluated expression
trace, but stops at the render API boundary. It does not establish actual black
pixels, image decoder correctness, async loading readiness, desktop compositing,
or a measured temporal waveform on a monitor/headset.

Run through the supported configured preset, then:

```powershell
ctest --test-dir build/windows-msvc -C Release -R strobe_composition_test --output-on-failure
```

## 17. Proposed authoring control and static preview

Expose a **Pulse visibility** block with period ticks, on ticks, phase source,
and target layer, plus an achieved-Hz display using the session's content rate.
Keep **Select new media every** a different block. Offer **Reveal background**
as a composition of a continuous background and an accent pulse. It should show
its expanded grammar so both a power user and an LLM can see what state changes.

The first preview should remain stopped. A frame scrubber and discrete pulse
strip show the on/off intervals; a layer inspector at the selected tick explains
which image draw is active, its source theme, and its clock owner. A thumbnail
can indicate a hidden layer without playing rapid alternating full-screen
content. Changing period/duty updates the strip and generated grammar. A curve
clock selector should state "read motion from scene" separately from "restart
this gate with cut". Animation status must distinguish requested, unavailable,
and fallback, rather than claiming every theme contains both media kinds.

These are authoring proposals. The prototype accompanying the broader
exploration is a separate static model, not a production renderer or decoder.

## 18. Recommendation, gaps and implementation handoff

Use existing `show` plus scoped registers as the first strobe authoring feature.
It is small, composable, and the meaning is executable today. A dedicated
language keyword adds little until an opaque color flash or scene-wide mask is
required. Keep selection, drawing, scheduling, and clock reading visible as four
separate concerns, even if a beginner-facing block groups common combinations.

Before shipping an editor, resolve these concrete gaps:

1. Define whether users need a pulsed layer, a full scene mask, or explicit black
   and white solid-color layers. The latter two require additional lowering and
   runtime behavior; they cannot be implemented honestly with a hidden `show`.
2. Decide whether cross-cut pulse phase is scene-relative or restarts with each
   cut. Emit that choice directly and test partial cuts and scene repetition.
3. Provide requested-versus-achieved timing feedback and preserve integer ticks
   in saved source. Avoid promising audio phase lock or device-exact Hz.
4. Preserve the shared animation-stream limitation in the UI until an explicit
   per-register animation model is designed and implemented.
5. Add a non-flashing offscreen pixel-composite test if claiming blackout pixels,
   then controlled device timing validation if claiming measured pulse timing.
   Those claims remain outside this investigation.

The grammar-only recipes and recorder are ready to serve as acceptance fixtures
for a later editor. No production UI integration is implied by this report.
