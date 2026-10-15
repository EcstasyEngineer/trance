# Authoring visual patterns

Write one v3 pattern in a UTF-8 `.pattern` file. Comments start with `#`.
Built-ins use this same language; see their
[source](../src/trance/visual/builtin_patterns_v3.cpp).

```text
pattern my_flash for 512f {
  every 64f { image primary zoom (curve 0 -> 0.5) }
  every 128f { word secondary }
  spiral speed 3
}
```

This selects a primary-theme image every 64 schedule frames and zooms it through
that cut. Words change every 128 frames. Both schedules run together. `primary`
and `secondary` are the two live theme sides; `runtime` chooses at effect firing.

## Try a pattern

```powershell
.\trance.exe --pattern=my_flash.pattern path\to\example.session.json
.\trance.exe --lint --pattern=my_flash.pattern
.\trance.exe --lint path\to\example.session.json
```

`--pattern` forces the file during playback; combined with `--lint` it checks a
standalone source without a window. That standalone lint has no beat context.
For patterns using `beats`/`locked`, reference them from the session's
`program_map.<name>.custom_visual_pattern` list and lint the session, which supplies each
program's beat period. See the [session format](session-json-format.md).
Playback requires a usable media root. F1 shows active sections, clocks, themes,
and image layers. Lint checks syntax and sampled expressions; playback checks
appearance. Neither is formal verification.

## Give each motion the right clock

`zoom 0.4` is static magnification. A `curve` moves over the nearest timed
occurrence. A named ancestor supplies a longer envelope without taking over the
child's schedule:

```text
pattern scene for 512f {
  every 16f -> cut {
    image primary zoom (curve 0 -> 0.4) alpha (curve 0 -> 1 over scene)
  }
  spiral speed (curve 1 -> 4)
}
```

Each image zooms independently while the image layer fades in and the spiral
speeds up across the scene. This is the practical reason to read a parent clock.
It works at arbitrary lexical depth: choose `over scene`, or write raw math such
as `[0.2 * scene.progress + 0.1 * this.progress]`.

Clock references are read-only. They do not merge timers, restart a parent, or
let child work run outside its owner. Prefer names to numeric ancestor depths:
adding a wrapper then leaves the intended reference readable.

A 64-frame cut displays progress 0 through 63/64. The final frame approaches a
curve's endpoint without adding a frame to show exactly 1.

## Nest and sequence sections

```text
pattern slow_then_fast for 768f seq {
  pattern slow for 512f {
    every 64f { image primary zoom (curve 0 -> 0.5) }
    spiral speed 2
  }
  pattern fast for 256f {
    every 8f { image secondary zoom (curve 0 -> 0.5 over fast) }
    spiral speed 4
  }
}
```

`seq` runs child schedules in order; otherwise they run together. `loop N` repeats
a pattern's declared duration N times. A parent truncates unfinished children
and resets their schedules on re-entry. Image contents and hidden selection
counters persist; restarting time does not wipe visual state.

For accelerating cuts:

```text
pattern accelerate for 512f {
  every ramp 32f -> 8f steps 32 ease early {
    image runtime zoom (curve 0 -> 0.4)
  }
}
```

The 32 cuts are scaled to fill 512 frames. Endpoints set relative duration shape;
total duration divided by steps sets average cut length.

## Interrupt rapid cuts with a moving hold

```text
pattern cuts_and_holds for 2048f {
  burst period 8f chance 1/12 cooldown 64f duration 64f..128f {
    base {
      every 8f { image runtime zoom (curve 0.0625 -> 0.1875) }
    }
    burst { image runtime zoom (curve 0.1 -> 0.7) anim }
  }
}
```

The base cuts every eight frames. The burst selects one animation and moves it
over its sampled 64–128-frame lifetime. An unavailable animation falls back to a
still with the same motion. Returning to a branch restarts its child schedules;
inactive branches do not select images or consume burst rolls.

The base's interruption time is unknown, so a bare base `curve` has no meaningful
percentage-complete clock and is rejected. Use a timed child or a bounded ancestor.
For repeated cuts with one long burst envelope, name the branch:

```text
pattern burst_envelope for 1024f {
  burst period 8f chance 1/4 cooldown 32f duration 64f..128f {
    base { every 8f { image primary } }
    burst -> held {
      every 8f { image secondary zoom (curve 0.1 -> 0.7 over held) }
    }
  }
}
```

Direct branch effects fire once on entry. `period` controls interruption checks;
it does not repeat those effects. Optional `enter { ... }` runs setup before
burst-body effects. Timed work belongs in `burst`, not `enter`. Fractional `env`
works in sampled bursts; frame envelopes and ramps need a fixed duration. See
the [burst reference](spec-grammar-v3.md#burst-phases).

## Crossfade with image registers

```text
pattern dissolve for 512f {
  every 64f {
    copy cur -> prev
    draw prev zoom (curve 0.4 -> 0.8)
    image secondary -> cur fade in zoom (curve 0 -> 0.4)
  }
}
```

The old image draws first; the new one fades in above it. Initially `prev` is
empty; thereafter each image continues its zoom after the copy. Registers belong
to the nearest pattern, so siblings can each use `cur`. Cadences/burst branches
share that scope. `scene.cur` can address an enclosing pattern's register.
Text does not have equivalent copyable registers.

## Show, fade, and alternate

```text
pattern accents for 512f {
  every 64f {
    image primary anim
    image secondary -> still env in 8f hold 16f out 8f
    word primary show 0f..8f
  }
}
```

The still rises, holds, falls, then stays transparent for half the cut. `fade
inout` instead spans the whole cut. `show` gates visibility without delaying
selection; use `show 0.5..1` for fractions or `show [this.frame < 8]` for raw math.
Main word/line text supports visibility windows and zoom/origin, not alpha envelopes.

`image alternate` flips sides each selection. `image alternate chance 0.25`
selects every time but flips only on a chance hit. Statements have independent
alternation state. Ordinary `image primary chance 0.25` instead retains the old
image on misses. Text chance also gates visibility. Chances are quantized and
clamped to 1..99 percent; omit chance for an unconditional effect.

## Audio and nominal beat lengths

```text
pattern mantra_pulse for beats 16 {
  every beats 4 { audio primary loop volume (curve 0.2 -> 0.8) }
  every beats 1 { image primary zoom (curve 0 -> 0.4) }
  spiral speed 2
}
```

`beats N` and `locked` require a pulsed entrainment layer. They use a frame period
computed when the program is parsed, not a live audio phase signal. At 60 schedule
frames/s, an 8 Hz bed becomes an 8-frame cadence (7.5 events/s), so exact sync is
not promised. Plain frame counts work without a bed.

`audio` selects from the theme's audio pool. One dedicated slot plays grammar
audio; the next selection replaces it. `loop` continues until replacement or
`audio stop`, including after the enclosing visual block ends. Volume is 0..1.
See [audio.md](audio.md) for other audio paths and volume scales.

## Reference and validation

The [language reference](spec-grammar-v3.md) lists syntax, scopes, expressions, and
limits. The [visual guide](visuals.md) maps them to code. For code changes, run
the complete build and CTest commands in [CLAUDE.md](../CLAUDE.md). Phase execution
tests exercise counter transitions that lint alone cannot observe.
