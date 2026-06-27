# Authoring visual patterns

> Legacy note: this page describes the older custom-pattern grammar. New patterns should use
> the v3 guide in [authoring-v3-patterns.md](authoring-v3-patterns.md). Playback tries v3 first
> for custom `.session` patterns, then falls back to this legacy grammar for older sessions.

You can write your own visual and ship it inside a `.session` as readable text —
no recompile. A custom pattern is selected for playback alongside the eight
built-in visuals. This is the user guide; for how the engine compiles and runs a
pattern, see [visuals.md](visuals.md). The authoritative grammar is the header
comment in `src/trance/visual/pattern_parser.h`; the built-in patterns in
`src/trance/visual/builtin_patterns.cpp` are worked examples.

## Where a pattern lives

A pattern is stored in a `Program` as a `VisualPatternSource` message
(`Program.custom_visual_pattern`, field 11 in `src/common/trance.proto`):

```proto
message VisualPatternSource {
  string name = 1;          // unique within the program
  uint32 random_weight = 2; // selection weight, same shuffle as built-in visual_type
  string source_text = 3;   // the DSL source below
  bool   enabled = 4;       // disabled patterns are stored but never selected
}
```

At playback the engine parses every enabled pattern, and adds the valid ones to
the same weighted visual shuffle as the built-in `visual_type` entries
(`Director::change_visual`, `director.cpp:384`). A pattern that fails to parse is
skipped with a `line:col` warning on stderr — never the whole session. The proto
`name`/`random_weight` override anything written in the source.

## A minimal pattern

```text
pattern my_flash {
  weight 10
  render simple
  one {
    every 1 : spiral_new, font, themes
    par {
      every 1  : spiral 3
      every 64 : image primary -> current
      every 128 : text line primary
    }
  }
}
```

This declares a pattern named `my_flash` with selection weight 10, using the
`simple` render preset. The `one { … }` block runs its children once: a one-frame
`every 1` leaf does the setup effects (new spiral, font, swap themes), then a
`par { … }` runs three lanes in parallel — rotate the spiral every frame, pull a
new primary-slot image into the `current` register every 64 frames, and change the
displayed text line every 128 frames.

## Structure: nodes

A pattern is a tree of nodes. The grammar (see `pattern_parser.h`):

| Node | Meaning | Lowers to |
|---|---|---|
| `every N [@K] [divide M] [: effects]` | A leaf that fires every N frames (on frame K of each N), running its effects. `divide M` runs them only every Mth fire. | `ActionCycler` |
| `timer N [@K]` | A no-op leaf of length N (a counter a render preset may read). | `ActionCycler` |
| `par { … }` | Children together, repeating (length = LCM). | `ParallelCycler` |
| `seq { … }` | Children in order (length = sum). | `SequenceCycler` |
| `one { … }` | Children together, once (length = max). | `OneShotCycler` |
| `repeat N node` | Repeat one node N times. | `RepeatCycler` |
| `offset K node` | Phase-shift one node by K frames. | `OffsetCycler` |
| `burst { … }` | A base loop randomly interrupted by a bounded burst. | `BurstCycler` |
| `generate VAR from A to B { … }` | Compile-time expansion: emit the block once per VAR in `[A..B]`. | expands at compile time |

**Node prefixes** (optional, before any node): `id "name"` gives the node a stable
id a render preset can address; `phase "LABEL"` tags a section for the F1 overlay
(e.g. `phase "SLOW"`); `image SLOT [as "label"]` marks an image-bearing node so
the overlay knows which theme slot it shows.

## Effects

A leaf's effects come after `:` and are comma-separated. They run in order each
time the leaf fires.

**Draw effects:**

```text
image primary -> current      # pull a primary-slot image into the `current` register
image alternate -> end        # alternate slot into the `end` register
anim primary                  # fire the animation (primary slot)
text line primary             # change displayed text (split: word | line | word_gaps | line_gaps | once)
text word random              # `random` slot = pick a slot at fire time
subtext alternate             # the large subtext lane
small_text random [force]     # the small subtext lane
themes                        # swap the active themes
font [force]                  # change the font
spiral_new                    # pick a new spiral
spiral 3.5                    # rotate the spiral at this rate
upload                        # upload a queued image to video memory
```

**Slots** select the theme: `primary` (slot 1), `alternate` (slot 2), `random`,
`runtime` (decided by hidden state — annotate the node with `image runtime`), or
`reg NAME` (use a scalar register as the primary/alternate selector).

## Registers and state

The only mutable state is a set of **named scalar registers** plus the **image
registers** that effects write. There are no general variables — these exist to
express stateful behaviour without a scripting language.

```text
set counter 0                       # counter = 0
inc sub_speed                       # sub_speed += 1   (or: inc x by 2)
toggle alt                          # alt ^= 1
roll animation_mod : 2 4 8          # animation_mod = one of {2,4,8} at random
pulse counter every animation_mod -> animation_on   # raise a 1-frame flag every Nth fire
copy end -> start                   # copy one image register to another
```

Image effects write image registers (`current`, `end`, `backup`, or any name you
choose). The render block reads them — match the register names your effects write
to the ones your `render { }` statements draw (the built-ins mostly draw
`current`, plus pattern-specific names).

## `when` — the only conditional

A `when` guard runs a single effect only if a register condition holds:

```text
every 12 : subtext reg alt when sub_speed == 1
every 24 : subtext reg alt when sub_speed == 2
every 48 : subtext reg alt when sub_speed >= 3
```

Comparisons are `when REG` (truthy, non-zero), `when REG == N`, or `when REG >= N`.
This is the language's only branch — there is no `if`/`else`, no loops beyond the
bounded `repeat`/`generate`, and no user-defined functions.

## `generate` and `[expr]`

`generate VAR from A to B { … }` expands its block once for each integer VAR in
the range (ascending or descending). Inside, an `[expr]` arithmetic expression
(operators `+ - * / ^`, plus the active `generate` variable) may appear anywhere
an integer is expected; it is floored in integer contexts. This is how
`ACCELERATE` builds its ramp of shortening segments
(`builtin_patterns.cpp`, the `kAccelerate` source):

```text
generate L from 56 to 48 {
  repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
    par {
      image alternate every [L] : image alternate -> current, ...
      every 1 : spiral [1 + (56-L)/16]
      every [L] @[L/2] : upload
    }
    every 8 : toggle text_on, text line alternate when text_on
  }
}
```

## The `render { }` block — what gets drawn

A `render { }` block turns your registers and live timing state into pixels. It is
a list of draw statements, each an op (`image`, `text`, `subtext`, `small_text`,
`spiral`) with an optional `when [cond]` guard and `[expr]` params
(`alpha`, `origin`, `zoom`, `shadow_origin`, `shadow_zoom`). The expressions are
evaluated every frame against your registers and cycler state — which you address
by node `id`, e.g. `slow_loop.active`, `ramp.progress`, `slow_repeat.index`:

```text
render {
  image current : alpha 1, origin 0, zoom [0.375 * image.progress]
  spiral
  text when [text_on] : origin 0.6, zoom 0.6
}
```

If you omit a `render { }` block entirely, a single-image default is used
(`current` image + spiral + text) so the pattern always renders something — start
there and add statements as you go. (The old `render NAME` header that named a
built-in preset is retired and no longer does anything; use a `render { }` block.)

## Worked reference

The cleanest full example is `SLOW_FLASH` (`builtin_patterns.cpp`, `kSlowFlash`):
a `one` containing a `themes` setup leaf and a `repeat 2 seq { … }` of a
`phase "SLOW"` block (slow 64-frame primary images) and a `phase "FAST"` block
(fast 8-frame alternate images), each a `par` of image / spiral / text / upload
lanes. Reading it alongside this guide is the fastest way to learn the surface.
