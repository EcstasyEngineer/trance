## 0. My synthesis: v2 should not be “smaller v1”

The current language is a faithful **Cycler construction language**: authors write `seq` / `par` / `one`, leaves fire `effects`, and render expressions reach back into node IDs and registers. That was the right intermediate step because the engine now parses text into a normalized AST, compiles it to Cyclers, and renders from data rather than hand-written visual classes. 

But v2 should not expose that implementation model. The simpler language should be:

> **lanes + phases + declarative modifiers + render shapes**

Authors should say: “there is an image lane every 64 frames, a text lane every 128, the image lane alternates themes, the render shape is a focus zoom,” not “construct a `one` containing a `par` whose child IDs are later read by a render expression.”

That agrees with the roadmap’s strongest thesis, but I would sharpen it: **do not make `render { ... }` the v2 authoring surface.** It is technically data, but it is still a low-level expression language over live Cycler state. v2 should expose a small library of parameterized render shapes—`focus`, `fade`, `stack`, `cut`—and let those shapes lower to today’s `RenderStmt` / render evaluator machinery. The roadmap already points in this direction and explicitly warns against making render a second full DSL. 

The current grammar’s complexity is not just syntax count. It comes from four hidden obligations: byte identity, scalar-register emulation, generated segment unrolling, and render formulas tied to internal node IDs. The docs already identify these as the reasons v1 became large. 

---

# 1. Proposed v2 grammar

This is an **authoring grammar**, not the normalized IR. The compiler can still lower it to the existing AST/Cycler model.

```ebnf
pattern      ::= "pattern" ident "{" item* "}"

item         ::= header
               | init
               | signal
               | render
               | cycle
               | phase

header       ::= "weight" int
               | "themes" int              // requested live theme count; default 2

init         ::= "init" "{" action-list "}"
               // scoped: runs when the containing pattern / phase / repeat enters

signal       ::= ident "=" signal-expr

signal-expr  ::= "ramp(" number "->" number
                         [ "," "over" duration ]
                         [ "," "curve" ident ]
                         [ "," "dwell" ident ] ")"
               | "pick(" value-list ")"       // sampled once per containing scope
               | "jitter(" range ")"          // resampled as intervals are consumed
               | "coin(" [probability] ")"    // sampled once per containing scope

render       ::= "render" shape "(" render-args ")"

shape        ::= "focus"
               | "fade"
               | "stack"
               | "cut"

cycle        ::= "cycle" duration ":" suite
               | "cycle" "auto" ":" suite

phase        ::= "phase" string [ "for" duration ] ":" suite

repeat       ::= "repeat" int ":" suite

suite        ::= stmt
               | "{" stmt* "}"

stmt         ::= lane-stmt
               | action-stmt
               | repeat
               | phase
               | init

lane-stmt    ::= source [lane] theme? cadence modifier* ";"

source       ::= "image"
               | "text" split
               | "subtext"
               | "small"
               | "anim"
               | "upload"
               | "spiral"

lane         ::= ident | ident "[" range-or-list "]"

split        ::= "word" | "line" | "once" | "word_gaps" | "line_gaps"

theme        ::= "theme" theme-expr

theme-expr   ::= int
               | "any"
               | "same(" ident ")"
               | "alternating(" value-list ")"
               | "cycle(" value-list ")"
               | "pick(" value-list ")"
               | "[" value-list "]"
               | "bands(" ident ":" band-list ")"

cadence      ::= "every" clock [ "@" offset ]
               | "rate" number               // mostly for spiral
               | "at" event

clock        ::= duration | ident | "frame"

modifier     ::= "active" duration
               | "stagger" duration
               | "repeat" int
               | "for" duration
               | "until" condition
               | "while" condition
               | "window(" condition ")"
               | "force"
               | "anim" anim-filter
               | "alt" theme-expr "every" fire-filter
               | "transition" transition-name
               | "burst(" burst-args ")"

fire-filter  ::= ordinal
               | "pick(" value-list ")th"
               | "jitter(" range ")th"
               | "chance(" probability ")"

anim-filter  ::= "every" fire-filter
               | "chance(" probability ")"
               | "burst(" burst-args ")"

action-stmt  ::= action-list ";"

action-list  ::= action { ";" action }

action       ::= "themes"
               | "font" [ "force" ]
               | "spiral.new"
               | "image" ident "theme" theme-expr
               | "text" split "theme" theme-expr
```

## Why these choices

### `cycle` and `phase` replace most explicit `seq` / `par`

The current grammar exposes `seq`, `par`, and `one`; `seq` length is sum, `par` length is LCM, and `one` length is max. Those are compiler concepts, not author concepts. The docs explain the current Cycler semantics directly: `OneShotCycler` is max, `ParallelCycler` is LCM, `SequenceCycler` is sum, `RepeatCycler` is N×child, and `OffsetCycler` phase-shifts one child. 

In v2, **statements in the same block are parallel by default**. Ordered time is represented by `phase`. That makes “slow then fast” readable as:

```text
phase "SLOW" for 1024: ...
phase "FAST" for 512: ...
```

instead of a `repeat 2 seq { phase "SLOW" par { ... } phase "FAST" par { ... } }` tree.

### `init` replaces the `one { every 1 : setup ; body }` idiom

The roadmap correctly notes that `one` is mostly a disguised setup mechanism and should become scoped enter semantics. 

Important detail: `init` is **not only frame-zero setup**. It runs when the containing scope enters. That is required because v1 uses `one` inside repeats and generated segments, not just at the top.

### Lanes replace image registers as the author-facing concept

Current effects write image registers such as `current`, `start`, `end`, `backup`, `img0`, and render blocks read them. The authoring docs describe this register model explicitly. 

In v2, the lane name *is* the public binding:

```text
image img theme 0 every 64;
render focus(img, text, small, spiral)
```

The compiler may still lower `img` to an image register, but authors do not have to think in register assignment.

### Theme index replaces `primary` / `alternate` / `reg alt`

The current slot vocabulary is binary: `primary`, `alternate`, `runtime`, `random`, or `reg NAME`.  The roadmap correctly identifies theme-index as the high-leverage change: `theme 0`, `theme 1`, `theme 2`, etc., with K live themes later. 

I would make the grammar accept `themes K` immediately, but the first compiler can reject `K > 2` until the ThemeBank is actually redesigned. Do **not** pretend this is just parser work; the roadmap is right that K>2 touches the visual API, overlay, and loader model. 

### Declarative modifiers replace scalar-register effects

Current v1 has `set`, `inc`, `toggle`, `roll`, `pulse`, `copy`, `when`, and the native `super_fast_tick` escape hatch. The parser header describes these as the bounded imperative surface for reproducing stateful built-ins. 

v2 should remove those from the authoring surface. Common mappings:

```text
toggle alt + image reg alt       -> theme alternating(0,1)
pulse counter every 3 -> flag    -> anim every 3rd
roll mod : 2 4 8                 -> pick(2,4,8)th
inc sub_speed + when >= N        -> ramp(...) or window(...)
copy end -> start                -> transition previous / render fade(...)
super_fast_tick                  -> cut(...) + burst(...)
```

I would deliberately distinguish:

```text
pick(2,4,8)th     // sampled once per scope: old captured random modulus
jitter(2..8)th    // resampled repeatedly: new expressive affordance
chance(1/3)       // independent Bernoulli per eligible fire
```

The roadmap flags this distinction as important because otherwise you reintroduce registers just to say “sampled once” versus “random every time.” 

### `ramp` is a signal, not only a cadence

`ACCELERATE` proves that “speed up” is not just an image interval. The same ramp drives image cadence, spiral speed, text density, upload gating, animation rate, and render zoom. v2 should let a named signal be reused:

```text
len = ramp(56 -> 12, curve linear, dwell pow6)
image img every len ...
spiral rate ramp(1 -> 3.75 by len)
render focus(img, zoom ramp(0.4 -> 0.5 by len))
```

This improves the roadmap: `ramp(from,to,over)` is too narrow unless it is a first-class signal.

### Render shapes replace raw render expressions

The current `render {}` block supports `image`, `text`, `subtext`, `small_text`, and `spiral`, with expressions for alpha/origin/zoom and conditions over live node/register state.  The evaluator then maps each statement to `VisualRender` calls. 

v2 should keep that layer as the **lowering target**, not the authoring language. The four shape families are enough to cover the built-ins:

```text
focus(...)   // one dominant image/animation layer + text/subtext/small/spiral
fade(...)    // previous/current or backup/current transition
stack(...)   // N interleaved image lanes, e.g. SUPER_PARALLEL
cut(...)     // rapid current/next cuts + burst overlays, e.g. SUPER_FAST
```

---

# 2. All 8 built-ins, current shape vs v2

I am not pasting each full current source verbatim because the point is readability; `kAccelerate` alone is about 70 nonblank DSL lines, and the full current built-ins are already in `builtin_patterns.cpp`. The “current” side below shows the essential v1 structure and the state/render tricks it uses; the v2 side is the proposed author-facing source. The current built-ins and their behavioral notes are summarized in the as-built docs. 

---

## 2.1 `slow_flash`

**Current v1 shape**

```text
pattern slow_flash {
  render { image current anim if [slow_loop.active ...]; spiral; small_text ...; text ... }
  one {
    every 1 : themes
    repeat 2 seq {
      phase "SLOW" par {
        one { every 1 : spiral_new, font
              repeat 16 image primary every 64 : image, anim, text line, small_text }
        every 1 : spiral 2
        every 64 @32 : upload
      }
      phase "FAST" par {
        one { every 1 : spiral_new, font
              repeat 32 par { image alternate every 8; text word alternate every 16 @8 } }
        every 16 : small_text alternate
        every 1 : spiral 4
      }
    }
  }
}
```

The actual source uses render expressions over `slow_loop`, `slow_repeat`, `fast_repeat`, `fast_loop`, and `fast_text` IDs. 

**Proposed v2**

```text
pattern slow_flash {
  init { themes }
  render focus(img, text, small, spiral, mode slow_fast)
  repeat 2:
    phase "SLOW" for 1024: init { spiral.new; font }; image img theme 0 every 64 anim; text line theme 0 every 64; small theme 0 every 64; upload every 64 @32; spiral rate 2
    phase "FAST" for 512:  init { spiral.new; font }; image img theme 1 every 8; text word theme 1 every 16 @8; small theme 1 every 16; spiral rate 4
}
```

**Readability delta:** “two cycles of slow primary flashes, then fast alternate flashes” is now visible without tracing `one` / `par` / `seq`.

---

## 2.2 `accelerate`

**Current v1 shape**

```text
pattern accelerate {
  render { image current anim if [animation_on] alt [animation_alt] ...; spiral; text when [text_on] ... }
  one {
    every 1 : set animation_counter 0, roll animation_mod : 2 4 8, font, spiral_new, themes
    id "ramp" phase "RAMP" seq {
      generate L from 56 to 48 { repeat pow6(L) one { image alternate every [L] ... } }
      generate L from 47 to 36 { repeat pow6(L) one { image primary   every [L] ... } }
      generate L from 35 to 25 { repeat pow6(L) one { image alternate every [L] ... } }
      generate L from 24 to 24 { repeat pow6(L) one { image alternate every [L] ... } }
      generate L from 23 to 16 { repeat pow6(L) one { image primary   every [L] ... } }
      generate L from 15 to 12 { repeat pow6(L) one { image primary   every [L] ... } }
    }
  }
}
```

The source comments say the current ramp shortens segment length from 56→12, uses `generate` + `[expr]`, captures a random animation modulus `{2,4,8}`, switches slots by length bands, uploads only when `L > 24`, and is already an approximation in render because generated un-ID’d image leaves cannot be addressed precisely. 

**Proposed v2**

```text
pattern accelerate {
  init { themes; font; spiral.new }
  len = ramp(56 -> 12, dwell pow6)
  render focus(img, text, spiral, zoom by len, text slide(0.6 -> 0.8))
  cycle auto:
    image img theme bands(len: 56..48=1, 47..36=0, 35..24=1, 23..12=0) every len anim every pick(2,4,8)th; upload every len @half while len > 24; spiral rate ramp(1 -> 3.75 by len)
    text line theme same(img) every 16 until len < 16; text word theme same(img) every len while len < 16
}
```

**Readability delta:** the author now sees “a length ramp from 56 to 12” instead of 45-ish expanded near-duplicates.

---

## 2.3 `sub_text`

**Current v1 shape**

```text
pattern sub_text {
  render { image current anim if [animation_on] alt [alt]; subtext; spiral; text }
  par {
    every 1 : spiral 4
    one {
      every 1 : set animation_counter 0, set alt 1,
                roll animation_mod : 3 5 7, themes, font, spiral_new, inc sub_speed
      repeat 16 par {
        image runtime every 48 : toggle alt, image reg alt, pulse ... every animation_mod, anim reg alt
        every 48 @24 : upload
        seq { every 4 : text word reg alt; repeat 23 every 4 : text once primary }
        every 12 : subtext reg alt when sub_speed == 1
        every 24 : subtext reg alt when sub_speed == 2
        every 48 : subtext reg alt when sub_speed >= 3
      }
    }
  }
}
```

The current comment describes exactly the state complexity: toggled alternate slot, captured random animation modulus `{3,5,7}`, and `sub_speed` ramp gating subtext cadence. 

**Proposed v2**

```text
pattern sub_text {
  init { themes; font; spiral.new }
  sub_rate = ramp(12 -> 48, over cycles, clamp); anim_period = pick(3,5,7)
  render focus(img, text, sub, spiral, zoom sweep(0 -> 0.375), sub alpha 0.25)
  cycle 768:
    image img theme alternating(1,0) every 48 anim every anim_period-th; upload every 48 @24; spiral rate 4
    text word theme same(img) every 4 with hold_once(theme 0, beats 23); subtext sub theme same(img) every sub_rate
}
```

**Readability delta:** the ugly `inc sub_speed` + three guarded subtext leaves becomes one named cadence signal.

---

## 2.4 `flash_text`

**Current v1 shape**

```text
pattern flash_text {
  render { image start anim if [animated ...]; image end anim if [animated ...]; spiral; small_text; text }
  one {
    every 1 : roll animated : 1 0, set alt 1, image alternate -> end
    par {
      every 1 : spiral 2.5
      every 64 : font force
      repeat 8 one {
        every 1 : toggle alt, text line reg alt
        repeat 2 image runtime every 64 : copy end -> start, anim reg alt when animated, image reg alt -> end
      }
      small_text / upload lanes...
    }
    every 1 : themes
  }
}
```

The current source notes that `animated` is a captured coin flip, `alt` toggles per one-shot iteration, and `copy end -> start` hands the previous image to the crossfade. 

**Proposed v2**

```text
pattern flash_text {
  init { themes; image img theme 1; animated = coin(0.5) }
  render fade(img, text, small, spiral, crossfade 64, animate animated)
  cycle 1024:
    repeat 8: image img theme alternating(1,0) every 64 transition previous anim chance(animated); text line theme same(img) at enter
    small theme 0 every 32 force; font force every 64; upload every 64 @32; spiral rate 2.5
}
```

**Readability delta:** `copy end -> start` disappears behind `transition previous`, which is the concept the visual actually wants.

---

## 2.5 `simple` / enum `PARALLEL`

**Current v1 shape**

```text
pattern simple {
  render { image current anim if [anim_on]; spiral; small_text; text when [counter.index == 1 or 2] }
  one {
    every 1 : spiral_new, font, themes
    par {
      every 1 : spiral 3
      every 32 : small_text primary force
      every 32 @16 : upload
      repeat 16 par {
        repeat 4 timer 32
        image runtime every 64 : pulse simple_counter every 3 -> anim_on,
                                 image reg anim_on -> current,
                                 anim primary when anim_on
        every 128 : text line runtime
      }
    }
  }
}
```

The source comment says the old behavior was “every third image fire: pull the alternate slot, fire the primary animation, render ANIM,” and that the underlying counter trick was an implementation detail. 

**Proposed v2**

```text
pattern simple {
  init { themes; font; spiral.new }
  render focus(img, text, small, spiral, zoom sweep(0 -> 0.5), text middle(64))
  cycle 2048:
    image img theme 0 every 64 alt 1 every 3rd anim every 3rd; text line theme any every 128
    small theme 0 every 32 force; upload every 32 @16; spiral rate 3
}
```

**Readability delta:** “every third image animates and uses the alternate theme” is first-class.

---

## 2.6 `super_parallel`

**Current v1 shape**

```text
pattern super_parallel {
  render { image img0 ...; image img1 ...; image img2 ...; spiral; text ... }
  one {
    every 1 : spiral_new, font, themes, set alt_anim 1
    par {
      every 1 : spiral 3.5
      every 32 @16 : upload
      text word runtime every 32
      repeat 12 one {
        every 1 : toggle alt_anim
        phase "INTERLEAVE" par {
          offset 0  seq { image primary every 16 -> img0, anim reg alt_anim; timer 16; timer 64 }
          offset 32 seq { image primary every 16 -> img1; timer 16; timer 64 }
          offset 64 seq { image alternate every 16 -> img2; timer 16; timer 64 }
        }
      }
    }
  }
}
```

The current comment describes it as three offset image lanes plus an alternate-animation toggle; the render fades the three lanes by single-mode. 

**Proposed v2**

```text
pattern super_parallel {
  init { themes; font; spiral.new }
  render stack(img[3], solo 16, alpha [1, .5, .33], zoom sweep(0.125, 0.875), text flash(32 half), spiral)
  cycle 1152:
    image img[0..2] theme [0,0,1] every 96 stagger 32 active 16; anim img[0] theme alternating(0,1) every 96
    text word theme any every 32; upload every 32 @16; spiral rate 3.5
}
```

**Readability delta:** “three staggered lanes” is visible in one line. This is the strongest proof that v2 should be lane-based.

---

## 2.7 `animation`

**Current v1 shape**

```text
pattern animation {
  render {
    image backup anim alt [change_alt.active]
    image current during start fade window
    image current during end fade window
    spiral; small_text; text when [change_counter.index == 0]
  }
  one {
    every 1 : spiral_new, font, themes
    repeat 8 par {
      every 1 : spiral 3.5
      every 32 : small_text random force
      every 32 @24 : upload
      image primary every 32 -> backup
      image alternate every 64 @32 -> current
      seq { text line primary + anim primary; change_alt text line alternate + anim alternate }
      repeat 2 every 32 as change_counter
    }
    start_end_timer seq { every 32; every 960; every 32 }
  }
}
```

The current render reads `change_alt`, `change_counter`, `start_end_timer`, and the `backup` / `current` image registers. 

**Proposed v2**

```text
pattern animation {
  init { themes; font; spiral.new }
  render fade(backup, current, text, small, spiral, window start 32 hold 960 end 32, zoom 0.625)
  cycle 1024:
    image backup theme 0 every 32; image current theme 1 every 64 @32 transition fade; anim theme alternating(0,1) every 64; text line theme alternating(0,1) every 64
    small theme any every 32 force; upload every 32 @24; spiral rate 3.5
}
```

**Readability delta:** the start/hold/end render behavior becomes a named fade-window parameter instead of three render expressions.

---

## 2.8 `super_fast`

**Current v1 shape**

```text
pattern super_fast {
  render {
    image current anim alt [sf_alternate] when [sf_state == 2 or 3]
    image current when [sf_state != 2 and != 3]
    image blank anim alt [sf_alternate] near rapid end when [sf_state == 1]
    image next near rapid end when [sf_state == 0 or 3]
    text when [sf_state == 0 and sf_text_mod == 0]
    spiral
  }
  one {
    every 1 : spiral_new, font, themes
    par {
      timer 2048
      image runtime every 8 : super_fast_tick
      every 1 : spiral 3
    }
  }
}
```

The current source calls this “the one genuine state machine”: a native 4-state FSM ticking every 8 frames and writing `sf_*`, `current`, and `next` registers. 

**Proposed v2**

```text
pattern super_fast {
  init { themes; font; spiral.new }
  render cut(img, next, text, spiral, preview 4, zoom quick, burst_overlay blank)
  cycle 2048:
    image img theme any every 8; image next theme any every 8 @4 active 4
    anim img burst(chance 0.25, duration 8..16, cooldown 16, theme alternating(0,1)); text line theme 0 every 32 chance(0.25); spiral rate 3
}
```

**Readability delta:** the visual becomes “rapid cuts with animation bursts.” This is not exact, but it is exactly the kind of fidelity loss v2 should accept.

---

# 3. Fidelity losses — the breakages that become the real spec

These are not bugs in the proposal; they are the simplification contract.

## Global losses

1. **No byte/pixel identity.** v2 does not preserve exact render formulas, exact active-node windows, or exact random sequences. The roadmap explicitly says v2 targets felt behavior rather than identity. 

2. **No public scalar registers.** Arbitrary `set` / `inc` / `toggle` / `roll` / `pulse` / `copy` / `when` programs are not expressible in core v2. They lower from modifiers internally, but authors cannot build new ad hoc state machines.

3. **No general `when REG >= N`.** v2 allows declarative windows such as `while len > 24`, `active 16`, `chance(0.25)`, and `window(...)`. It does not expose a general conditional language over registers.

4. **No general render-expression authoring.** Authors choose render shapes and parameters. The current `render { image current when [expr] : zoom [expr] }` system remains a lowering target or “advanced/v1” mode, not the default v2 surface.

5. **`generate` is not core v2.** The common `ACCELERATE` use becomes `ramp`. If someone truly needs parameterized structural generation, keep v1 or provide a separate macro/preprocessor tier. Do not put `generate` in the friendly grammar.

6. **`copy` becomes transitions.** `FLASH_TEXT`’s `copy end -> start` is replaced by `transition previous` / `render fade`. That covers the built-in but loses arbitrary image-register choreography.

7. **Theme K>2 is aspirational until runtime changes.** The grammar can say `themes 3`, but the current system’s primary/alternate slot model and loader must change before that works beyond syntax. The roadmap correctly warns this is a real ThemeBank/API redesign. 

8. **Validation changes.** Frame-for-frame equivalence is no longer the safety net. v2 needs shape golden tests, pattern-signature tests, fuzz/property tests, and visual review artifacts—the roadmap’s proposed replacement stack is right. 

## Per-pattern losses

| Pattern          | Specific losses                                                                                                                                                                                                                                                                                              |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `slow_flash`     | Exact render gating for small text and late text is approximated by `render focus(... mode slow_fast)`. The v1 render reads `slow_loop`, `slow_repeat`, `fast_repeat`, and `fast_text`; v2 does not expose those exact node IDs.                                                                             |
| `accelerate`     | The exact `generate` dwell formula can be approximated by `dwell pow6`, but not reproduced unless the ramp implementation intentionally matches it. Slot band edges are preserved in the example, but the tiny per-image zoom wobble already noted as approximate in v1 remains approximate.                 |
| `sub_text`       | `sub_speed` currently increments as hidden persistent scalar state and selects among 12/24/48-frame subtext cadences. v2’s `sub_rate = ramp(12 -> 48, over cycles)` captures the feel, not exact lifetime/persistence semantics.                                                                             |
| `flash_text`     | The exact start/end image-register handoff is replaced by shape-managed previous/current fade. The captured `animated` coin is preserved conceptually, but exact frame placement of animation vs image copy may differ.                                                                                      |
| `simple`         | The exact `counter.index == 1 or 2` text window becomes a render-shape parameter like `text middle(64)`. The observable “every third image animates” is kept.                                                                                                                                                |
| `super_parallel` | The triple-lane stagger, alpha stack, and 16-frame solo windows can be captured well by `stack`. The exact `alt_anim` toggle timing may differ if `alternating(0,1)` is defined per lane fire rather than per repeat-enter. This must be specified.                                                          |
| `animation`      | The 32/960/32 start-hold-end window can be captured, but the exact alpha formulas in the render block are shape-owned. If the shape uses a different easing curve, it will feel similar but not match.                                                                                                       |
| `super_fast`     | This loses the most. The 4-state `super_fast_tick` FSM, `sf_state`, `sf_anim_timer`, `sf_text_mod`, blank overlay, and current/next handoff are replaced by rapid cuts plus probabilistic animation bursts. The roadmap already identifies deleting `super_fast_tick` as a v2 move under the 90%-feel rule.  |

---

# 4. Lowering sketch to the existing AST / Cycler model

## 4.1 Keep v1 AST as the first lowering target

The current pipeline is already sound: DSL text → `pattern::Node` tree → Cycler tree → effects/registers/render. 

v2 can be a **front-end** that desugars to current-ish IR. You do not need to rewrite Cyclers first.

### `cycle D`

Lower:

```text
cycle D: statements
```

to a bounded scope whose body is parallel lanes with explicit repeat counts:

```text
one {
  init-leaf
  par {
    timer D
    lane-1-lowered-for-D
    lane-2-lowered-for-D
    ...
  }
}
```

For a lane `every N` inside `cycle D`, if `D % N == 0`, lower to:

```text
repeat D/N every N : effects
```

If not divisible, either reject in v2.0 or lower to a masked action with a finite phase window. I would reject first; it keeps the mental model clean.

### `phase NAME for D`

Lower ordered phases to `seq` children. Each phase becomes a bounded parallel scope:

```text
phase "NAME" one {
  init-leaf
  par {
    timer D
    phase-lanes-lowered-for-D
  }
}
```

That preserves `seq` length = sum and keeps the overlay label.

### Parallel-by-default lanes

Statements in one block lower to a `par`. A lane with `stagger S` lowers to `offset S`. A vectorized lane:

```text
image img[0..2] theme [0,0,1] every 96 stagger 32 active 16
```

expands to three lanes:

```text
offset 0  lane img0 theme 0 every 96 active 16
offset 32 lane img1 theme 0 every 96 active 16
offset 64 lane img2 theme 1 every 96 active 16
```

`active 16` lowers to a `seq` of an action window plus a timer pad:

```text
seq {
  every 16 : image theme -> imgN
  timer 80
}
```

or a more exact synthetic node whose `active()` is true for the first 16 frames and whose image effect fires at the start.

### `init`

Top-level `init` lowers to a one-frame action at scope entry:

```text
every 1 : themes, font, spiral_new
```

Nested `init` lowers similarly inside the repeated/phase scope. This preserves the useful part of `one` without exposing `one`.

### `theme`

Initial compatibility mapping:

```text
theme 0  -> Slot::Primary
theme 1  -> Slot::Alternate
theme any -> Slot::Runtime or random, depending on desired old behavior
```

But the AST should change from:

```cpp
enum class Slot { None, Primary, Alternate, Runtime };
```

to something like:

```cpp
struct ThemeRef {
  enum Kind { None, Index, Any, Alternating, Cycle, Pick, SameLane, Bands };
  std::vector<int> values;
  std::string signal_or_lane;
};
```

The current AST stores slot and optional `slot_reg`; v2 should not encode theme selection as a bool/register trick. 

### `anim every 3rd`

If literal, lower to current `divide` or a hidden `pulse`:

```text
anim every 3rd
```

becomes one of:

```text
every N divide 3 : anim theme
```

or:

```text
pulse __img_counter every 3 -> __img_anim
anim theme when __img_anim
```

The second lowering is more general and matches existing behavior for dynamic periods like `pick(2,4,8)th`.

### `pick(2,4,8)th`

Lower to a scoped hidden `roll` in the scope’s `init`:

```text
init : roll __period : 2 4 8
...
pulse __counter every __period -> __flag
anim when __flag
```

The author sees `pick`, but the existing compiler can still use `Roll` and `Pulse`.

### `theme alternating(0,1)`

Lower either to deterministic fire-index selection if the compiler learns lane fire counts, or to a hidden toggle:

```text
toggle __lane_theme
image reg __lane_theme -> img
```

For K>2, this needs the ThemeRef AST change; the current bool slot trick only handles 0/1.

### `ramp`

Two implementation options:

1. **Desugar to v1-style `seq` of repeated segments** for compatibility. `ACCELERATE` can compile almost exactly this way, but the generated code is internal.
2. **Add a `Signal` object to compiled patterns** and let lane actions read it. This is cleaner long-term because render shapes, cadence, spiral rate, and text density can share the same signal.

I would do option 1 first for `every len` because Cyclers are integer-schedule based, and option 2 for render/shape parameters.

### `render focus/fade/stack/cut`

Add a v2 AST field:

```cpp
struct RenderShape {
  enum Kind { Focus, Fade, Stack, Cut };
  std::vector<Arg> args;
};
```

Then lower `RenderShape` to the current `std::vector<RenderStmt>` where possible. The existing render evaluator already supports image/text/subtext/small_text/spiral statements with numeric expressions and conditions. 

For example:

```text
render stack(img[3], solo 16, alpha [1,.5,.33], zoom sweep(.125,.875))
```

can emit today’s three `image img0/img1/img2 when [...] : alpha ..., zoom ...` render statements, using compiler-generated node IDs for the lane progress/single windows.

For shapes that need memory—`fade(previous,current)`—the lane lowering should produce hidden image registers like `__img_prev` and `__img_current`, or the shape runtime should own that state. I prefer hidden registers first because it fits the current model.

---

# 5. What must change in AST/runtime

Minimum viable v2 can lower to v1 AST with hidden names, except for theme-index.

Required or strongly recommended changes:

1. **ThemeRef instead of Slot bool.** This is the real runtime expansion. Without it, v2 is just prettier syntax for primary/alternate.

2. **RenderShape AST before `RenderStmt`.** Do not make authors write render expressions. Let shape compilation generate current `RenderStmt` objects.

3. **Scoped hidden state.** The compiler needs a hygienic way to create hidden registers like `__accelerate_anim_period`, `__simple_anim_flag`, etc., without exposing them to the author or colliding with user names.

4. **Signals.** Add a v2 compile-time/runtime signal table for `ramp`, `pick`, `coin`, and maybe lane fire counts. Some signals lower to constants/registers; others are sampled over time.

5. **Better lane identity.** Today render binds to arbitrary node IDs. v2 render should bind to lane IDs, and the compiler should decide which node IDs/render expressions implement the lane.

6. **Optional: keep raw `render {}` as advanced mode.** This is useful for internal tests and power users, but it should not be the headline grammar.

---

# 6. Compatibility / versioning

This is the dangerous part: custom visual patterns are stored as raw `source_text` in `VisualPatternSource`, parsed at playback, with no grammar version field.  The roadmap also calls out that v2 cannot simply replace the v1 parser for stored sessions. 

My recommendation:

```text
visual v2 slow_flash {
  ...
}
```

or:

```text
pattern slow_flash v2 {
  ...
}
```

Pick a syntax that the v1 parser will **not** accidentally accept.

Then dispatch:

1. If source starts with the v2 marker, use v2 parser.
2. Otherwise, parse as v1 forever.
3. Built-ins can move to v2 because they are compile-time sources.
4. Do not silently auto-migrate custom v1 patterns at runtime.
5. Later, add a proto field:

```proto
uint32 grammar_version = 5;
```

or a new `source_v2` field, but do not block v2 experimentation on proto migration.

A migration tool can offer approximate v1→v2 rewrites for the eight built-ins and common user patterns, but arbitrary v1 patterns with scalar registers and custom render expressions should remain v1/advanced.

---

# 7. Final verdict

The roadmap is directionally right, but the design should be stricter:

1. **Do not design “v2 Cycler syntax.”** Design a lane language.
2. **Do not expose general render expressions in the friendly grammar.** Use render shapes.
3. **Do not keep scalar registers as a “small escape hatch.”** They will leak everywhere again.
4. **Do add theme indices, but treat them as a runtime/API project, not a parser tweak.**
5. **Do keep v1 indefinitely for stored custom patterns and advanced exactness.**

The v2 built-ins then become readable summaries of behavior:

* `slow_flash`: slow primary phase, fast alternate phase.
* `accelerate`: cadence ramp 56→12.
* `sub_text`: alternating image lane with subtext cadence ramp.
* `flash_text`: previous/current fade with optional animation.
* `simple`: single image, every third one animates.
* `super_parallel`: three staggered image lanes.
* `animation`: backup/current fade window.
* `super_fast`: rapid cuts with animation bursts.

That is the right abstraction level for humans.
