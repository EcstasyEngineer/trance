# Authoring v3 visual patterns

V3 patterns are the shipped grammar for built-in visuals and are now accepted for custom
`.session` patterns. Playback tries v3 first, then falls back to the legacy parser for older
custom sources.

## The Shape

V3 has two nouns and one rule:

- `pattern` is a named box with a length and a `0..1` clock.
- An effect line draws, drives, or mutates state: `image`, `draw`, `word`, `caption`,
  `subtext`, `spiral`, `warp`, `copy`, and the small scalar ops.
- Every numeric value is a modulator: a literal, `curve A -> B`, or raw `[expr]`. It rides the
  enclosing pattern's clock unless redirected with `over NAME`.

```text
pattern my_flash for 512f {
  every 64f { image concept zoom (curve 0 -> 0.5) }
  every 128f { word reward }
  spiral speed 3
}
```

Children in a pattern run together by default. Add `seq` to a pattern header when child
patterns should run one after another:

```text
pattern slow_then_fast for 768f seq {
  pattern slow for 512f { every 64f { image concept zoom 0.5 } spiral speed 2 }
  pattern fast for 256f { every 8f { image reward } spiral speed 4 }
}
```

## Content And Registers

The engine is bi-thematic. `concept` reads the primary theme, `reward` reads the secondary
theme, and `runtime` picks a side at fire time.

`image CONTENT -> REG` pulls an image into a register and draws it. `draw REG` draws an
existing image register without pulling a new image. Registers are local to the nearest
enclosing `pattern`, so two sibling patterns can both use `cur` and `prev` safely.

The canonical handoff is a crossfade:

```text
pattern xfade for 512f {
  pattern life for 128f loop 4 {
    every 64f -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image reward -> cur fade in zoom (curve 0 -> 0.5)
    }
  }
}
```

Each image has two halves. While it is `cur`, it zooms `0 -> 0.5`; after `copy cur -> prev`,
it continues `0.5 -> 1.0`. The old layer draws first, and the new layer fades in above it.
That matches the renderer's source-over blending without a special `crossfade` keyword.

## Common Effects

```text
image concept -> cur zoom (curve 0 -> 0.5) fade in
draw prev alpha 0.5 origin 0.25 zoom 0.75
word reward
caption concept
subtext runtime
spiral speed (curve 1 -> 4)
warp amplitude (curve 0 -> 0.2) wavelength 0.15 speed 2
drunk (curve 0 -> 0.3)
copy cur -> prev
```

Text currently has one live slot; it can be changed and zoomed by the existing text render path,
but it cannot be copied and alpha-crossfaded like images until the deferred text-register
extension lands.

## Debugging

Press F1 during playback. The overlay shows the active visual, the deepest active pattern
section, all four ThemeBank queue slots (`unloaded`, `primary`, `secondary`, `loading`), image
layers drawn this frame, spiral state, entrainment state, and a minimized cycler tree. A `*`
next to a theme row means a visible image layer from that concrete slot was drawn this frame.

For implementation details, see [visuals.md](visuals.md). For the full v3 grammar and design
notes, see [spec-grammar-v3.md](spec-grammar-v3.md).
