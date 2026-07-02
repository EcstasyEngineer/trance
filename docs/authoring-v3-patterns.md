# Authoring v3 visual patterns

V3 patterns are the shipped grammar for built-in visuals and custom `.session` patterns alike.
It is the only grammar -- there is no legacy fallback parser.

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

## Beats: phase-locking to the entrainment bed

Most patterns are timed in frames (`for 512f`, `every 64f`) -- an arbitrary clock with no
relationship to the audio. If the session has a pulsed entrainment bed (a binaural or
isochronic layer with `pulse_hz > 0`, synthesized in `entrainment.cpp`: the gate that fades
each ear in and out, 180 degrees out of phase for binaural), a pattern can instead lock its
cadence to that bed's pulse period, so a flash or a handoff lands exactly on the beat instead
of drifting against it.

`director.cpp` resolves one beat period per program before parsing: it looks at the program's
entrainment layers, picks the pulsed layer with the highest amplitude, and converts its
`pulse_hz` to a frame count (`round(global_fps / pulse_hz)`). That frame count is threaded into
the parser as `locked_period_frames`; the grammar exposes it through two length keywords:

- **`locked`** -- exactly one beat period.
- **`beats N`** -- `N` beat periods (`N * locked_period_frames`).

Both are valid anywhere a `<len>` is expected: a pattern's `for` span, or a cadence's `every`
length.

```text
pattern pulse_flash for beats 8 {
  every beats 1 { image concept zoom (curve 0 -> 0.5) }
}
```

With an 8 Hz isochronic layer at 60 fps, `locked_period_frames` resolves to 8 (60/8, rounded),
so this pattern runs for 64 frames total and flashes once per beat -- 8 flashes, one per pulse.

The same substitution works inside the crossfade shape shown above; swap the inner clock's
frame length for a beat length and the handoff itself locks to the bed instead of running on
an arbitrary cadence:

```text
pattern xfade_on_beat for beats 8 {
  pattern life for beats 2 loop 4 {
    every beats 1 -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image reward -> cur fade in zoom (curve 0 -> 0.5)
    }
  }
}
```

Each image now lives for exactly 2 beats before the next one cuts in, and the cut itself
always lands on a pulse.

**Prefer `beats`/`locked` when the pattern's whole point is to feel synced to the bed** --
a flash cadence, a crossfade handoff, anything meant to read as "on the beat." **Prefer plain
frame lengths (`Nf`) everywhere else**, including anything that must also work in a program
with no pulsed bed at all.

That last clause is load-bearing, not stylistic: **`beats N` and `locked` hard-error at parse
time when `locked_period_frames` is 0** -- i.e. when the program has no pulsed entrainment
layer (`` `beats` needs a pulsed entrainment bed (none in this program) ``, same for `locked`).
There is no silent fallback to a frame count. This is exactly why the eight shipped built-ins
(`builtin_patterns_v3.cpp`) are written entirely in frames: they are parsed once at startup
against whatever program is loaded (`Director::build_builtin_patterns()`), including sessions
with no entrainment bed configured at all, so they cannot use a length keyword that hard-errors
in that case. A custom pattern authored for a specific session that is known to always ship a
pulsed bed does not have that constraint.

Themes own PRECANNED audio pools exactly like their image and font pools (no TTS, ever), and
the `audio` effect (issue #23) is the grammar's window onto them: `every beats N { audio
mantra }` triggers a spoken line phase-locked to the entrainment bed, the same way `image`/
`word` trigger a flash. See the worked example below.

## Audio: phase-locking a mantra to the beat

This is the showcase for `beats`: a precanned mantra line, pulled from the active theme's
audio pool, fires exactly on the entrainment pulse -- the same phase-lock a flash gets, but
for the ear instead of the eye.

```text
pattern mantra_pulse for beats 16 {
  every beats 4 { audio concept loop volume (curve 0.2 -> 0.8) }
  every beats 1 { image concept zoom (curve 0 -> 0.4) }
  spiral speed 2
}
```

`audio concept` pulls a random precanned line from the primary theme's `audio_path` pool
(exactly like `image concept` pulls a random image) and starts it playing on the engine's
dedicated theme-audio channel. `loop` keeps it going for the rest of its `every beats 4`
window; `volume (curve 0.2 -> 0.8)` fades it in across each 32-beat-frame span, riding the
SAME curve machinery as `zoom`/`fade`/`spiral speed` -- there is no separate "audio curve"
concept. Meanwhile `every beats 1` flashes an image once per pulse: the mantra cadence (every
4 beats) and the flash cadence (every beat) both lock to the same entrainment bed
independently, so they never drift relative to each other even though they fire at different
rates.

**Single-slot v0:** there is exactly one live grammar-driven theme audio at a time, the same
shape as the engine's single live text slot (`docs/audio.md`). A second `audio` fire --
whether it's a different pattern or the next cycle of this one -- replaces whatever was
already playing; there is no queueing or crossfade for audio the way there is for images.
Use `audio stop` to cut a line early:

```text
audio stop
```

**Content vocabulary.** `audio` takes the exact same bi-thematic content word as `image`/
`word`: `concept` (primary theme), `reward` (alternate theme), or `runtime` (rolled at fire
time). Unlike `get_font` (which falls back to a system font when a theme has no fonts of its
own), a theme with an empty `audio_path` pool makes `get_audio` return an empty path; the
engine then fails to open it and logs `couldn't load ` to stderr rather than crashing --
not silent, but not fatal either. Give the theme at least one file in its audio pool before
using `audio` against it.

**Volume scale.** `audio ... volume M` is `0..1`, matching `Audio::set_theme_audio_volume`'s
signature -- NOT the `0..100` scale playlist `AudioEvent.volume` uses elsewhere in the engine.
Worth remembering if you're used to authoring playlist audio events.

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
