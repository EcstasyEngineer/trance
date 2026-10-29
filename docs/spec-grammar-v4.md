# V4 visual grammar

V4 has a [compact text grammar](v4-text-grammar.md) and a JSON abstract syntax tree
(AST). Both feed the same native compiler and visual composer. Text deliberately
covers only features exercised by the eight built-ins, plus the requested
subpattern timing controls. Advanced JSON remains supported; formatting an unsupported
AST as text reports an error without dropping fields. There is no intermediate
V3 source generation or V3 parser. Unknown properties are errors; V3 is rejected.
Existing session JSON remains a separate format; upgrading visual patterns does
not require replacing themes, playlists or audio settings.

## Compose and run

Use the [composer](https://ecstasyengineer.github.io/trance/) to edit a built-in,
drag outputs and patterns, and export a `.pattern` file. For native compiler
validation, run `python tools/authoring_server.py` from the repository root after
building the Release executable, then open the local address it prints.

```console
trance --lint --pattern=example.pattern
trance --pattern=example.pattern my.session.json
```

The filename extension does not determine the language: `.pattern` accepts V4
text or JSON. The composer displays text by default and offers an explicit JSON view.
Session custom-pattern entries reference these files by path. F2 source display,
copy, new patterns and lint use the same V4 representation.

```json
{
  "version": 4,
  "root": {
    "kind": "parallel",
    "duration": 1024,
    "children": [
      {
        "node": {
          "kind": "parallel",
          "cadence": 64,
          "children": [{"node": {"op": "image", "source": "primary"}}]
        }
      },
      {"node": {"op": "spiral", "speed": 2}}
    ]
  }
}
```

This selects a new primary image every 64 content frames while the spiral remains
active. A frame is a content tick at the program's configured frame rate, rather
than a monitor refresh or an audio sample.

## Three composition modes

Every composition has `kind` and `children`. Each child is an edge object whose
`node` contains another composition or an output. Every mode can contain every
other mode; no named burst controller, phase or authored register is required.

| Kind | Children |
| --- | --- |
| `parallel` | Run together; source order determines image stacking. |
| `sequential` | Run one finite child at a time, in source order. |
| `random` | Run one child at a time, switching when it completes or exits by chance. |

An image can belong directly to a parallel pattern. A wrapper supplies an
independent lifetime or cadence when that is needed; storage isolation does not
require a wrapper. Each image output owns its captured image automatically.
Sequential and random children need a meaningful duration or exit condition;
timed wrappers give instantaneous outputs that lifetime.

## Timing

| Property | Meaning |
| --- | --- |
| `duration` | Positive integer frames for one iteration, or `"inherit"` to calculate from subpatterns. |
| `repeat` | Positive iteration count, or `"forever"` under a bounded owner. |
| `cadence` | Positive interval between body activations, or `{ "ramp": { "from": 64, "to": 8, "steps": 8, "ease": "linear" } }`. |
| `offset` | Phase shift in frames; it does not preload images or mean an inactive delay. |
| `id` | Optional ancestor clock name for structured curves. |
| `loop_subpatterns` | Boolean on parallel/sequential, default `false`. |

**Inherit** calculates this pattern's duration from its finite timed subpatterns:
the sum for a sequence or maximum for parallel. For example, sequential
subpatterns of 200 and 300 frames give their parent 500 frames. Changing 300 to
400 immediately changes the parent to 600. This works at every nesting level;
the source keeps `"duration": "inherit"`, and compilation resolves its frame
count. Nested Inherit modes resolve from the bottom up. Finite child repeats
count toward the total. Direct outputs follow their owner and do not supply a
duration. Random exits, forever repetition and unresolved child lifetimes do not
provide an exact total; use fixed frames when the length cannot be determined.

Omitting `duration` retains the existing owner-window behavior, called **Parent
frames** in the nested editor. At the root, where there is no parent, omission
derives a finite subpattern total. A duration-omitted child cannot supply an
independent total for Inherit because its window depends on that same parent;
give that child fixed frames or its own Inherit mode.

A parent clips its descendants. Finished finite children stay inactive until their owner restarts
them; a sequence may finish before its authored cap while retaining that cap as
its modulation-clock span. A longer parent does not implicitly repeat a sequence.
Exact inactive or clipped spans produce advisory diagnostics with AST paths;
inactivity does not necessarily mean the entire screen is blank.

**Loop subpatterns** keeps a sequential child list cycling, or restarts each completed
parallel child independently, until the parent's duration/cadence boundary. It
does not add an idle transition tick. Repeating the parent and looping its
children are separate choices.

With the checkbox off, subpatterns complete independently. Inherit controls
duration; looping controls what happens when a subpattern completes early.
There is no separate Wait setting or frame-calculation action. Sampled durations
and the former `child_mode` field are rejected.

A parent-provided window can shrink on later loop or cadence entries. Envelopes need
a known fitting local clock, and phase pre-roll has a fixed execution budget.
When those cannot be established for a variable parent window, validation asks for
Fixed frames or a fixed cadence. An explicit clock may still be intentionally
clipped by its parent.

Repeat is explicit. Ramp intervals are scaled and
rounded to fill the explicit duration, with at least one frame per step.

Offset pre-roll advances clocks while suppressing selections and random draws.
An unselected new image is empty; retained content can be visible at its shifted
phase after owner re-entry. Selection counters and alternating-theme state belong
to authored outputs and persist across cadence, ramp and scene re-entry.

## Random selection

`select` is legal on random patterns:

```json
{"order": "random", "allow_self": false, "first": 0, "cooldown": 64}
```

`order` is `random` or `round_robin`; `first` is a zero-based child index.
Random chooses uniformly among eligible children. Allowing self-selection makes
choosing the current child a real restart. Round robin advances in source order
and does not allow `allow_self: true`. A single eligible child can restart once
per transition, rather than spin through zero-time switches.

A child edge can include `exit: { "probability": { "numerator": 1,
"denominator": 12 }, "check_every": 8 }`. Chance checks occur on the selector's
clock grid. Cooldown blocks chance checks after a transition; it never blocks
finite child completion. A transition frame does not perform a second chance
check. Inactive children do not select media or consume random values.

Super Fast uses round robin between rapid cuts that exit by chance every eight
frames and a held image with a fixed 96-frame lifetime. These are ordinary
nested patterns, not privileged default and interrupt phases.

## Output operations

| `op` | Behavior and supported modulation |
| --- | --- |
| `image` | Select and hold an image; optional animation accent, zoom, zoom baseline, opacity, visibility and envelope. |
| `word` | Main text selection; optional selection probability and visibility. |
| `line` | Main text line selection; optional selection probability and visibility. |
| `caption` | Caption selection; optional opacity, visibility and envelope. |
| `subtext` | Scrolling subtext selection; optional opacity, visibility and envelope. |
| `spiral` | Spiral output with optional scalar `speed`; has no media source. |

Media outputs require `source`: `primary`, `secondary`, `either`, or `alternate`.
`either` makes a fresh fair theme-side choice per selection. An image and its
animation use that same choice. `alternate` flips remembered sides before each
ordinary selection, starting with secondary. `flip_probability` changes the
chance of that flip; it does not control whether content is selected.

`selection_probability` independently controls selection attempts. Zero means
never; one means always. Image misses retain the previous captured image, reset
local motion and advance the animation accent counter. Text misses suppress
selection and rendering for that occurrence. `animation: { "every": 2 }` requests
animation on every second selection attempt; one requests it every time.

Animation decoders are shared per theme side, and text channels are shared.
Private image captures do not create an independent movie decoder or text channel
for every output. Simultaneous same-side animations can therefore share playback.

## Curves, opacity and visibility

Scalars are finite numbers or structured curves, triangles and sums:

```json
{"curve": {"from": 0, "to": 0.5, "ease": "linear", "scope": "local"}}
```

Ease is `linear`, `early` or `late`. Scope defaults to `local`, and may be `scene`
or an enclosing pattern's `id`. Local means the current cadence interval, ramp
segment or unscheduled occurrence. Scene means the root occurrence. An explicit
ancestor reads its occurrence progress, independently of its cadence.
`{"sum": [0.1, {"curve": {"from": 0, "to": 0.4}}]}` combines values.
`{"triangle": {"from": 0, "peak": 1, "to": 0}}` fades in and out.

`visibility: [0.25, 0.75]` renders only in that half-open fraction of local
progress. `envelope: {"in": 8, "hold": 16, "out": 8}` fades in, holds and fades
out in integer frames; it must fit a known local duration, and any remaining tail
is absent. Choose either opacity or an envelope. Unsupported output/property
combinations fail validation instead of being ignored.

Probabilities accept decimals with at most six decimal places or exact rational
objects. Expressions, arbitrary code, authored destinations, copy/draw operations
and audio output leaves are not part of V4. Playlist audio and synthesized beds
remain session/program settings; see [audio](audio.md).

## Built-ins, validation and boundaries

The eight [visuals](visuals.md) use this exact representation. Their canonical
source is [builtin_patterns_v4.cpp](../src/trance/visual/builtin_patterns_v4.cpp).
Parallel contains two image lanes, Super Parallel three, and Flash Text two
offset image lanes forming a dissolve. Flash Text's first image starts opaque;
its lanes refresh from current themes rather than preserving a copied snapshot.

The native compiler is authoritative. It rejects duplicate JSON members, unknown
fields, invalid scalar scopes, out-of-range values and impossible timing.
Limits include four MiB source, 64 composition nesting levels, 4,096 children per
composition, 32 scalar nesting levels, two through eight sum terms, and signed
32-bit positive frame ranges. Ramp steps are bounded to 65,536. Phase pre-roll is
bounded to one million frames; nested ramp offsets can be rejected
conservatively. Errors identify the relevant AST path. Text syntax errors identify
line and column and enforce source, nesting and child limits before recursion.

The player presents the final executed content frame before selecting the next
visual on the following tick. Ready opaque Flash Text media has an opaque base;
cold media loading and transparency in source images remain separate from its
opacity schedule.

See [runtime architecture](engine-today.md) and
[validation coverage](architecture-maturity.md). Schedule traces establish
execution behavior; they do not establish decoded-media pixels, GPU timing or
physical headset behavior. Earlier reviews and adapters are linked from the
[archive](archive/README.md) and are not the production implementation.
