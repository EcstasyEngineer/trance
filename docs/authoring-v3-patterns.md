# Authoring v3 visual patterns

V3 patterns are the shipped grammar for built-in visuals and custom patterns alike. It is the
only grammar -- there is no legacy fallback parser.

Your patterns live in standalone `*.pattern` files (plain UTF-8, `#` line comments). A session
references one per `custom_visual_pattern` entry by root-relative path; see
[session-json-format.md](session-json-format.md) §4. To try one without touching a session at
all, run `trance.exe --pattern=my_pattern.pattern some.session.json`, which forces every visual
selection to that file and prints a `line:col` diagnostic instead of falling back if it does
not parse.

## The Shape

V3 has two nouns and one rule:

- `pattern` is a named box with a length and a `0..1` clock.
- An effect line draws, drives, or mutates state: `image`, `draw`, `word`, `caption`,
  `subtext`, `spiral`, `warp`, `copy`, and the small scalar ops.
- Every numeric value is a modulator: a literal, `curve A -> B`, or raw `[expr]`. It rides the
  enclosing pattern's clock unless redirected with `over NAME`.

```text
pattern my_flash for 512f {
  every 64f { image primary zoom (curve 0 -> 0.5) }
  every 128f { word secondary }
  spiral speed 3
}
```

Children in a pattern run together by default. Add `seq` to a pattern header when child
patterns should run one after another:

```text
pattern slow_then_fast for 768f seq {
  pattern slow for 512f { every 64f { image primary zoom 0.5 } spiral speed 2 }
  pattern fast for 256f { every 8f { image secondary } spiral speed 4 }
}
```

## Content And Registers

The engine is bi-thematic. `primary` reads theme 0, `secondary` reads theme 1, and `runtime`
picks a side at fire time.

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
      image secondary -> cur fade in zoom (curve 0 -> 0.5)
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
  every beats 1 { image primary zoom (curve 0 -> 0.5) }
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
      image secondary -> cur fade in zoom (curve 0 -> 0.5)
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
  every beats 4 { audio primary loop volume (curve 0.2 -> 0.8) }
  every beats 1 { image primary zoom (curve 0 -> 0.4) }
  spiral speed 2
}
```

`audio primary` pulls a random precanned line from the primary theme's `audio_path` pool
(exactly like `image primary` pulls a random image) and starts it playing on the engine's
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
`word`: `primary` (theme 0), `secondary` (theme 1), or `runtime` (rolled at fire
time). Unlike `get_font` (which falls back to a system font when a theme has no fonts of its
own), a theme with an empty `audio_path` pool makes `get_audio` return an empty path; the
engine then fails to open it and logs `couldn't load ` to stderr rather than crashing --
not silent, but not fatal either. Give the theme at least one file in its audio pool before
using `audio` against it.

**Volume scale.** `audio ... volume M` is `0..1`, matching `Audio::set_theme_audio_volume`'s
signature -- NOT the `0..100` scale playlist `AudioEvent.volume` uses elsewhere in the engine.
Worth remembering if you're used to authoring playlist audio events.

## Windows, Envelopes, And Alternation

Three params and one verb control *when* a layer is on and *which theme* it pulls from. All
four are compile-time sugar over fields the runtime already evaluated -- they add vocabulary,
not engine.

**`show` -- when a layer paints.** Without it, a draw paints for the whole life of its pattern.
Write the window as a slice of the enclosing clock, in frames, or as a raw condition:

```text
every 64f -> beat {
  image primary
  word secondary show 0f..8f   # an 8-frame stab at the top of each cut
  line primary show 0.5..1     # the second half only
  caption runtime show [this.frame < 32]
}
```

The window is ANDed onto whatever gating is already in play, so it composes with sequenced
phases, `burst` blocks and `chance`. Frames and fractions can't be mixed inside one window
(`show 0f..0.5` is an error), and a frame window that runs past its clock's length is an error
too -- neither gets silently clamped.

**`env` -- rise, hold, fall, then gone.** `fade inout` is a whole-clock triangle: it peaks for
an instant and is never actually absent, so a layer beneath it never has the screen to itself.
`env` gives you a real hold and a real hole:

```text
every 64f -> beat {
  image primary anim                            # the animation runs the whole cut
  image secondary -> still env in 16f hold 16f out 16f
}                                               # ...and the still is GONE for the last 16f
```

Operands are frames or fractions (`env in 0.25 out 0.25`); omit `hold` for a triangle that
still has the absent tail. `in + hold + out` must fit the clock.

**`line` -- whole phrases.** `word` puts one word on screen at a time; `line` puts the whole
phrase up. Same content vocabulary, same params, same everything else.

**`alternate` -- deterministic A/B.** Not to be confused with `secondary`, which *pins* theme
1: `alternate` is the word that makes a draw switch sides. `primary` pins theme A and
`runtime` rolls a coin every firing. `alternate` ping-pongs A, B, A, B instead:

```text
every 48f { image alternate zoom (curve 0 -> 0.4) }
every 8f  { image alternate chance 0.25 anim }   # holds a side, pivots occasionally
```

`alternate chance P` flips only with probability `P` per pull, so the theme *holds* between
flips -- at `P = 0.5` that is a uniform-random side per image, and lower values read as
"stay in this world for a while, then pivot." Each `alternate` statement keeps its own phase.
It also works on the standalone animation load (`anim alternate`).

## Common Effects

```text
image primary -> cur zoom (curve 0 -> 0.5) fade in
image alternate chance 0.5 env in 16f hold 16f out 16f
draw prev alpha 0.5 origin 0.25 zoom 0.75
word secondary show 0f..8f
line primary show 0.5..1
caption primary
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
