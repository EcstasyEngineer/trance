## Thesis

I would **not** design this as “a prettier `every N` grammar.” That keeps the wrong object at the center.

I would design it as a **visual score language**:

> A pattern is a named experience made of **phases**, **voices**, **signals**, **theme relationships**, and a **presentation shape**.
> The compiler lowers that score into the current frame schedule.

The current system already has a clean execution pipeline: pattern text parses to `pattern::Node`, compiles to a `Cycler` tree, and each leaf becomes a `VisualControl` action plus render state. That is exactly the right lowering target, but it is the wrong authoring ontology. The current grammar’s surface is explicitly `every`/`timer`/`par`/`seq`/`one`/`repeat`/`offset`/`burst` plus primitive effects and small register operations, which means the author writes execution structure first and meaning second. 

The new surface should answer:

> “What kind of audiovisual event is this?”

not:

> “Which callback fires on which frame?”

The existing AST can survive, but demoted: it becomes **Frame IR**, not the author’s language.

---

## 1. Intent-level model

I would use five core concepts.

### 1. Phase

A **phase** is a named temporal section with a felt role: `build`, `flash`, `overload`, `release`, `drift`, `burst`, `hold`, `collapse`.

A phase has duration, repetition, and optional energy/tension curves:

```trance
phase SLOW for 1024 {
  tension = ramp 0.0 -> 0.5
  ...
}

phase FAST for 512 {
  tension = ramp 0.5 -> 1.0
  ...
}
```

This is not just `seq`. `seq` means “run child A then child B.” A phase means “this section of the experience has a name, curve, and purpose.” It can lower to `SequenceCycler`, but it should not be authored as `seq`.

The roadmap already points in this direction: `seq` is the sequencing affordance, `par` is simultaneity, and `one` mostly encodes setup; the v2 notes recommend replacing `one` with scoped `init` / `@enter` semantics and hiding most explicit `par` behind named lanes. 

### 2. Voice

A **voice** is a content stream: image cuts, text words, subtext texture, animation accents, spiral motion, upload/preload policy.

I prefer **voice** over **lane** for the authoring surface. “Lane” is still a good compiler term, but “voice” better implies intent: simultaneous streams in a composition.

Examples:

```trance
voice cuts:
  image theme 1 -> current
  cadence 8f

voice words:
  text word theme 1
  cadence 16f offset 8f

voice texture:
  small_text theme 1
  cadence 16f
```

This still allows frame-accurate cadences, but the cadence belongs to a semantic stream. The author is no longer writing anonymous leaf callbacks.

### 3. Signal

A **signal** is a named time-varying value: tension, density, cadence, zoom, animation probability, spiral speed.

This is the biggest missing abstraction. Today, `accelerate` expresses “speed up” by generating many explicit segment lengths. The roadmap correctly calls `generate` powerful but too heavy for the common case of “speed up over time,” and suggests a ramp instead. 

Signals should be first-class:

```trance
signal tension = ramp 0 -> 1 curve quint over RAMP
signal cut_period = map tension 56f -> 12f
signal spiral_speed = map tension 1 -> 3
signal text_density = step tension {
  <0.85: line every 8f
  >=0.85: word every cut
}
```

Signals replace a lot of current scalar-register machinery. The current AST has explicit `Set`, `Inc`, `Toggle`, `Roll`, `Pulse`, `Copy`, and `SuperFastTick` effects, plus guards; those exist mainly to reproduce hidden C++ member state.  The roadmap’s simplification is right: `pulse` becomes “every nth,” `toggle` becomes `alternating`, random modulus becomes `choice(...)`, and `inc sub_speed` becomes a ramp/stair signal. 

Important distinction:

```trance
every choice(2,4,8)th cut   # choose once at phase entry
every random(2..8)th cut    # reroll / jitter
chance 0.25 per cut         # Bernoulli event
```

Without that distinction, you re-invent hidden registers.

### 4. Theme relationship

The current API is binary: `get_image(bool alternate)`, `change_text(..., bool alternate)`, `change_animation(bool alternate)`, and related calls all use a primary/alternate bool.  That makes “theme A image with theme B text” possible only in a limited two-slot way, and makes three-theme patterns impossible without a lower-level redesign.

Intent grammar should say **theme**, not slot:

```trance
image theme 0
text theme 1
anim theme same(image)
text theme other(image)
image theme cycle(0,1,2)
image theme [0,0,1] across voices
```

The roadmap calls this out as a high-leverage low-level change: generalize from `get_image(bool)` to theme indices over a live set of size K, enabling 3+ theme patterns and associative-conditioning pairings. 

Until the engine supports K themes, the compiler can map `theme 0` → primary and `theme 1` → alternate, rejecting `theme >= 2` unless a future ThemeBank supports it.

### 5. Presentation shape

This is the most important part: **render topology is intent**.

`super_parallel` is not merely three image pulls. It is a **stack**: three staggered image voices, sometimes soloed, otherwise layered at different alphas. `flash_text` is not “copy end into start”; it is a **fade** / handoff. `super_fast` is not `super_fast_tick`; it is a **cut shape with bursts**.

The existing render layer is already data-capable: `RenderStmt` describes image/text/subtext/small_text/spiral draw statements, including alpha, origin, zoom, animation gates, and `when` expressions.  But I would not expose arbitrary render expressions as the primary intent language. That becomes a second low-level DSL.

Instead, expose a small library of parameterized shapes:

```trance
present focus(...)
present fade(...)
present stack(...)
present cut(...)
present field(...)
```

The roadmap’s “render gate” conclusion is exactly right: use fixed parameterized render shapes, not a full render-expression language; suggested shapes include `focus`, `fade`, `stack`, and `cut`. 

I would add `field` for subtext/textural overlays, but it can also be a mode of `focus`.

---

## 2. Proposed surface grammar

This is a sketch, not final EBNF. The important thing is the object model.

```trance
pattern <name> {
  intent "<human phrase>"

  themes <K>
  enter {
    themes.roll
    font.roll
    spiral.roll
  }

  signal <name> = <curve>
  signal <name> = map <signal> <from> -> <to>

  present <shape> {
    <shape parameters>
  }

  cycle <duration> [repeat <n>] {
    phase <NAME> for <duration> [repeat <n>] {
      voice <name>:
        <source> theme <theme-expr> [-> <image-reg>]
        cadence <cadence-expr>
        [offset <duration>]
        [stagger <duration>]
        [active <duration>]
        [with anim <anim-policy>]
        [text <split-policy>]
        [density <signal>]

      spiral speed <expr>
      preload every <duration> [at <duration>]
    }
  }
}
```

### Sources

```trance
image
anim
text word
text line
text once
subtext
small_text
spiral
preload
```

These correspond to the primitive vocabulary already available through `VisualControl` and `VisualRender`: images, text, animation, subtext, small text, theme changes, font changes, spiral changes/rotation, upload/preload, and render calls. 

### Cadence expressions

```trance
cadence 64f
cadence ramp 56f -> 12f over RAMP curve quint
cadence density(tension, slow=64f, fast=8f)
every 3rd cut
every choice(2,4,8)th cut
chance 0.25 per cut
burst chance 1/8 duration 2..6 cooldown 4
```

### Theme expressions

```trance
theme 0
theme 1
theme same(cuts)
theme other(cuts)
theme alternating(0,1)
theme cycle(0,1,2)
theme any
theme [0,0,1] across img[0..2]
```

### Render shapes

```trance
present focus {
  image current zoom tension.sweep(0.0, 0.75)
  text after 0.5 at 0.8
  small_text during first_half alpha 0.2
  spiral
}

present stack {
  images [img0,img1,img2]
  solo active_window
  alpha [1.0, 0.5, 0.33]
  zoom root 0.125 + lane.progress 0.875
  text words first_half
  spiral
}

present fade {
  from start
  to end
  crossfade image.progress
  text after first_cut
  spiral
}

present cut {
  current current
  next next
  overlay burst_anim during burst
  text during base
  spiral
}
```

The shape is not decoration. It is the visual topology, and topology is part of intent.

---

## 3. The eight built-ins as intent

The current as-built reference summarizes the eight built-ins as: `ACCELERATE` is a length 56→12 ramp, `SLOW_FLASH` is slow then fast phases, `SUB_TEXT` is image plus scrolling subtext with gated cadence, `FLASH_TEXT` is a two-image crossfade plus text, `PARALLEL`/`simple` is actually single-image, `SUPER_PARALLEL` is a three-image staggered interleave, `ANIMATION` is animation plus crossfade window, and `SUPER_FAST` is rapid current/next cuts driven by `super_fast_tick`. 

Here is how I would express their intent.

### 1. `slow_flash`

Intent: **two-phase escalation: slow build, then fast flash.**

```trance
pattern slow_flash {
  intent "build slowly, then flash fast"

  enter { themes.roll; font.roll; spiral.roll }

  present focus {
    image current zoom phase_sweep
    text during slow.second_half or fast.text_back_half
    small_text during slow.first_half or fast
    spiral
  }

  repeat 2 {
    phase SLOW for 1024 {
      voice image: image theme 0 -> current cadence 64f with anim always
      voice text:  text line theme 0 cadence 64f
      voice small: small_text theme 0 cadence 64f
      spiral speed 2
      preload cadence 64f offset 32f
    }

    phase FAST for 512 {
      voice image: image theme 1 -> current cadence 8f
      voice text:  text word theme 1 cadence 16f offset 8f
      voice small: small_text theme 1 cadence 16f
      spiral speed 4
    }
  }
}
```

This maps cleanly. The existing source already has explicit `SLOW` and `FAST` phase labels, with primary image/text/small-text every 64 in slow and alternate image every 8 plus word text every 16 in fast. 

### 2. `accelerate`

Intent: **image cuts speed up over time; spiral and animation pressure rise with the same ramp.**

```trance
pattern accelerate {
  intent "cuts accelerate until the image stream becomes urgent"

  enter {
    themes.roll
    font.roll
    spiral.roll
    choose anim_period = choice(2,4,8)
  }

  signal tension    = ramp 0 -> 1 over RAMP curve quint
  signal cut_period = map tension 56f -> 12f
  signal spin       = map tension 1 -> 3
  signal text_mode  = line until 0.85 then word

  present cut {
    image current zoom map(tension, 0.0 -> 0.5)
    text when text_on zoom map(tension, 0.6 -> 0.8)
    spiral
  }

  phase RAMP {
    voice cuts:
      image theme banded([1,0,1,1,0,0]) -> current
      cadence cut_period
      with anim every anim_period cuts

    voice text:
      text text_mode theme same(cuts)
      cadence 8f until fastest, then cadence cut_period

    spiral speed spin
    preload while cut_period > 24f at half_cut
  }
}
```

This is almost exactly what the current generated DSL is trying to say, but cannot say directly. The current source comments explain that `accelerate` is a ramp of image segment lengths from 56 to 12, with image count, spiral speed, slot choice, upload/timer behavior, and “fastest” behavior derived from length; the DSL has to emit this with `generate` and expressions. 

This is the canonical reason to add first-class signals.

### 3. `super_parallel`

Intent: **overload: three simultaneous theme streams, staggered and partially soloed.**

```trance
pattern super_parallel {
  intent "overload: three staggered image voices stack into one field"

  enter { themes.roll; font.roll; spiral.roll }

  present stack {
    images [img0,img1,img2]
    solo active 16f
    alpha [1.0, 0.5, 0.33]
    zoom root 0.125 + lane.progress 0.875
    text words first_half
    spiral
  }

  cycle 1152 {
    voice img0: image theme 0 -> img0 cadence 96f offset 0f  active 16f
                with anim theme alternating(0,1) per lap
    voice img1: image theme 0 -> img1 cadence 96f offset 32f active 16f
    voice img2: image theme 1 -> img2 cadence 96f offset 64f active 16f

    voice words: text word theme any cadence 32f
    preload cadence 32f offset 16f
    spiral speed 3.5
  }
}
```

This fits extremely well. The current source comment already says the pattern is three offset image lanes plus an alternate-animation toggle, and the render fades three lanes by “single-mode.”  The actual v1 implementation has offsets 0, 32, and 64, with `img0`/`img1` using primary and `img2` using alternate. 

The roadmap gives nearly the same six-line shape/lane expression, which is strong evidence that this abstraction is not just aesthetic; it recovers the hidden concept. 

### 4. `super_fast`

Intent: **fast cuts, with occasional animation bursts and next-image flashes.**

```trance
pattern super_fast {
  intent "rapid cuts with unstable animation bursts"

  enter { themes.roll; font.roll; spiral.roll }

  present cut {
    current current
    next next during pre_cut
    burst blank_anim during burst_tail
    text during base every text_gate
    spiral
  }

  cycle 2048 {
    voice rapid:
      image theme any -> current
      cadence 8f
      with next prefetch
      burst chance 1/N duration 2..16 ticks cooldown M
      burst emits anim theme alternating(0,1)

    voice text: text word theme same(rapid) during base chance text_gate
    spiral speed 3
  }
}
```

This is one of the places where intent exposes that the built-in’s exact mechanism is probably incidental. The current implementation explicitly calls it “the one genuine state machine,” a four-state FSM ticking every 8 frames through a native `super_fast_tick` effect that writes `sf_*` registers.  The v2 roadmap’s verdict is also that, under the 90% felt-behavior target, `super_fast_tick` should disappear and be replaced by `burst` over a fast image cadence. 

Exact `super_fast` is not an intent primitive. It is a specific state machine that happened to produce the desired feeling.

### 5. `sub_text`

Intent: **one image theme with textural subtext that changes density/rhythm over cycles.**

```trance
pattern sub_text {
  intent "focus image plus increasingly insistent subtext texture"

  enter {
    themes.roll
    font.roll
    spiral.roll
    choose anim_period = choice(3,5,7)
  }

  signal sub_refresh = stair per cycle {
    1: 12f
    2: 24f
    3+: 48f
  }

  present focus {
    image current zoom image.progress * 0.375
    subtext field alpha 0.25 origin image.progress * 0.375
    text foreground with shadow tied_to image.progress
    spiral
  }

  cycle {
    voice image:
      image theme alternating(0,1) -> current
      cadence 48f
      with anim every anim_period cuts

    voice words:
      text word theme same(image) cadence 4f
      then text once theme 0 cadence 4f repeat 23

    voice sub:
      subtext theme same(image) cadence sub_refresh

    preload cadence 48f offset 24f
    spiral speed 4
  }
}
```

The current source describes exactly the ingredients that should become declarative: alternate-slot toggling, captured random animation modulus `{3,5,7}`, and `sub_speed` gating of subtext cadence.  Whether the user-facing label should say “escalating density” or “evolving subtext refresh” depends on felt review; mechanically, the cadence changes by `sub_speed`, and the intent grammar should let that be named as a signal rather than as `inc` plus three guarded leaves.

### 6. `flash_text`

Intent: **text-led two-image handoff / crossfade, sometimes animated.**

```trance
pattern flash_text {
  intent "line text drives a start/end image flash"

  enter {
    themes.roll
    choose animated = chance 0.5 per cycle
    end = image theme 1
  }

  present fade {
    from start
    to end
    crossfade image.progress
    anim when animated
    text after first half
    small_text on alternate half
    spiral
  }

  cycle {
    voice text:
      text line theme alternating(0,1)
      cadence 128f

    voice image:
      image theme same(text) handoff end -> start -> end
      cadence 64f
      repeat 2 per text

    voice small: small_text theme 0 cadence 32f force
    preload cadence 64f offset 32f
    font cadence 64f force
    spiral speed 2.5
  }
}
```

The current source says the hidden mechanics are a captured random `animated` flag, an `alt` toggle, and copying previous end image into start; the render block draws start/end crossfading.  In an intent grammar, `copy end -> start` is not something an author should write. It is the lowering of `present fade`.

### 7. `simple` / enum `PARALLEL`

Intent: **steady single-image focus with small text texture and every-third animation accent.**

```trance
pattern simple {
  intent "steady focus, sparse text, every-third image becomes an animation accent"

  enter { themes.roll; font.roll; spiral.roll }

  present focus {
    image current zoom image.progress * 0.5
    text during counter windows [1,2]
    small_text always alpha 0.2
    spiral
  }

  cycle {
    voice image:
      image theme 0 -> current cadence 64f
      accent every 3rd image {
        image theme 1 -> current
        anim theme 0
      }

    voice text: text line theme any cadence 128f
    voice small: small_text theme 0 cadence 32f force
    preload cadence 32f offset 16f
    spiral speed 3
  }
}
```

This built-in’s enum name is actively misleading: the as-built reference says enum `PARALLEL` maps to `kSimple`, and the note says it is a single-image pattern.  The source comment says the observable behavior is every third image firing an animation accent; the `++ twice` old behavior was implementation detail.  So the intent name should not be `parallel`; it should be something like `steady_accent`, `simple_focus`, or `third_accent`.

### 8. `animation`

Intent: **animation-dominant focus with alternating theme animation and start/end fade windows.**

```trance
pattern animation {
  intent "animation showcase with current-image fade windows"

  enter { themes.roll; font.roll; spiral.roll }

  present fade {
    base backup anim theme alternating_by_voice
    overlay current during intro_outro windows
    text during first half of change cycle
    small_text texture
    spiral
  }

  cycle {
    voice backup:
      image theme 0 -> backup cadence 32f

    voice current:
      image theme 1 -> current cadence 64f offset 32f

    voice anim_text:
      sequence every 64f {
        text line theme 0; anim theme 0
        text line theme 1; anim theme 1
      }

    voice small: small_text theme random cadence 32f force
    preload cadence 32f offset 24f
    spiral speed 3.5

    window intro 32f
    window body 960f
    window outro 32f
  }
}
```

This mostly fits, but it teaches an important design constraint: some “intent” is actually render-window topology. The current render reads `change_alt`, `change_counter`, `start_end_timer`, and backup/current image registers; the schedule pulls primary backup images every 32, alternate current every 64 offset 32, alternates text/animation every 64, and has explicit start/body/end timer windows.  This wants either a `fade` shape with `intro/body/outro` windows or a specialized `animation_focus` shape.

---

## 4. Concrete lowering sketch: `super_parallel`

### Intent source

```trance
pattern super_parallel {
  intent "overload: three staggered image voices stack into one field"

  enter { themes.roll; font.roll; spiral.roll }

  present stack {
    images [img0,img1,img2]
    solo active 16f
    alpha [1.0, 0.5, 0.33]
    zoom root 0.125 + lane.progress 0.875
    text words first_half
    spiral
  }

  cycle 1152 {
    voice img0: image theme 0 -> img0 cadence 96f offset 0f  active 16f
                with anim theme alternating(0,1) per lap
    voice img1: image theme 0 -> img1 cadence 96f offset 32f active 16f
    voice img2: image theme 1 -> img2 cadence 96f offset 64f active 16f

    voice words: text word theme any cadence 32f
    preload cadence 32f offset 16f
    spiral speed 3.5
  }
}
```

### Lowering step A — normalize to Score IR

The compiler turns the above into a canonical semantic plan:

```cpp
PatternScore {
  enter: [themes, font, spiral_new, set alt_anim = 1]

  shape: Stack {
    lanes: [img0, img1, img2]
    solo_window: 16
    alphas: [1, .5, .333]
    zoom: root_progress * .125 + lane_progress * .875
    text: first_half(words)
  }

  cycle length: 1152

  voices:
    img0 = ImageVoice(theme=0, target=img0, period=96, offset=0,  active=16,
                      anim=AlternatingTheme(0,1, scope=lap))
    img1 = ImageVoice(theme=0, target=img1, period=96, offset=32, active=16)
    img2 = ImageVoice(theme=1, target=img2, period=96, offset=64, active=16)
    words = TextVoice(split=word, theme=runtime, period=32)
    preload = UploadVoice(period=32, offset=16)
    spiral = SpiralVoice(speed=3.5, period=1)
}
```

This is the layer the F1 overlay should understand.

### Lowering step B — desugar modifiers

`active 16f` becomes a `singleN` timer for render gating.

`offset 32f` becomes an `OffsetCycler`.

`cadence 96f` plus active/hold becomes a 96-frame sequence.

`with anim theme alternating(0,1) per lap` becomes the current register idiom:

```trance
set alt_anim 1
toggle alt_anim per repeat
anim reg alt_anim
```

That register is an implementation detail. It should not leak to the author.

### Lowering step C — emit current Frame IR / v1 AST

The lowered v1-style shape is essentially the current source:

```trance
one {
  every 1 : spiral_new, font, themes, set alt_anim 1
  par {
    every 1 : spiral 3.5
    every 32 @16 : upload
    id "text" every 32 : text word runtime

    repeat 12 one {
      every 1 : toggle alt_anim
      id "interleave" phase "INTERLEAVE" par {
        offset 0 id "prog0" image primary as "img[0]" seq {
          every 16 : image primary -> img0, anim reg alt_anim
          id "single0" timer 16
          timer 64
        }

        offset 32 id "prog1" image primary as "img[1]" seq {
          every 16 : image primary -> img1
          id "single1" timer 16
          timer 64
        }

        offset 64 id "prog2" image alternate as "img[2]" seq {
          every 16 : image alternate -> img2
          id "single2" timer 16
          timer 64
        }
      }
    }
  }
}
```

That is not hypothetical; it is structurally the current `kSuperParallel` implementation. 

### Lowering step D — emit render block

The `present stack` shape lowers to the current three image render statements:

```trance
image img0 anim alt [alt_anim] when [no_solo or single0.active] : ...
image img1                   when [no_solo or single1.active] : alpha [solo ? 1 : 1/2], ...
image img2                   when [no_solo or single2.active] : alpha [solo ? 1 : 1/3], ...
spiral
text when [text.frame < text.length / 2] : ...
```

Again, this matches the current source: three image render layers, alpha differences, `singleN.active` gating, root/lane progress zoom, spiral, and text first-half gating. 

### Lowering step E — compile to Cyclers

The existing compiler already maps `Action`, `Seq`, `Par`, `One`, `Rep`, `Off`, and `Burst` nodes to `ActionCycler`, `SequenceCycler`, `ParallelCycler`, `OneShotCycler`, `RepeatCycler`, `OffsetCycler`, and `BurstCycler`; it also attaches phase/image annotations and fills the node map for ids. 

So the current execution machinery does not need to be replaced. The design change is above it.

---

## 5. Where intent breaks down

Intent grammar can cover the majority, but it should not pretend to cover everything.

### Exact state machines

`super_fast` is the sharpest example. The source calls it a genuine four-state FSM, isolated in the native `super_fast_tick` effect.  Under the new goal, that exact FSM is probably not sacred. The felt intent is rapid cuts plus bursts. But if a future author truly needs exact state transitions, the surface needs an escape hatch.

I would allow:

```trance
native gesture super_fast_tick
  exports current, next, sf_state, sf_alternate
  described_as "rapid cuts with occasional animation burst"
```

or:

```trance
mechanics {
  every 8f: super_fast_tick
}
```

But require a human `described_as` string and declared outputs. Otherwise the low-level hatch becomes the default language again.

### Bespoke render geometry

`flash_text`, `super_parallel`, and `animation` all contain specific render math. The roadmap already flags `FLASH_TEXT` crossfade, `SUPER_PARALLEL` triple fade, and `ANIMATION` fade windows as places where “90% similar” may require dedicated shapes. 

The rule should be:

> If three patterns need it, make a parameterized shape.
> If one pattern needs it, allow a named native shape.
> Do not expose arbitrary render expressions as the normal surface.

A raw render block can remain as:

```trance
render.raw {
  image current when [...]
  text when [...]
}
```

but it should be labeled advanced, not taught first.

### Theme count

Theme-index intent cannot fully work until the low-level API and ThemeBank support more than primary/alternate. Today, the API takes `bool alternate` for images, text, animation, subtext, and small text.  So `theme 2` is a future-facing surface feature unless the engine changes underneath it.

Recommendation: support `theme 0` and `theme 1` immediately; reserve syntax for K themes; validate and reject unavailable indices.

### Asset upload / preload

`upload every 32 @16` is not expressive intent. It is a streaming/performance policy. In the intent surface, this should usually be automatic:

```trance
preload auto
```

or attached to high-frequency voices:

```trance
voice cuts: image theme any cadence 8f preload ahead half_cut
```

Keep explicit `preload every ...` only for tuning.

### “Mood words” are not always semantics

Words like `overload`, `dream`, `threat`, `hypnosis`, `release`, or `subliminal` are useful, but they should not all be compiler semantics. Some should be metadata:

```trance
intent "overload: three theme streams interleaved"
feel overload
```

`feel overload` can choose defaults, but the real lowering should come from concrete voices, signals, and shapes.

### Validation changes

Dropping byte identity removes the old safety net. The roadmap’s replacement strategy is right: exact golden tests for render shapes, pattern-signature metrics such as cadence ranges and layer counts, property/fuzz tests for bounded valid patterns, and visual-review artifacts. 

---

## 6. Escape hatch design

The escape hatch should be powerful, but socially expensive.

I would expose three levels:

### Level 1: named primitive escape

```trance
voice rapid:
  native super_fast_tick every 8f
  exports current, next, sf_state
  intent "rapid cuts with animation burst FSM"
```

Use for legacy compatibility and rare C++ gestures.

### Level 2: raw frame schedule block

```trance
mechanics {
  one {
    every 1 : spiral_new, font, themes
    par {
      ...
    }
  }
}
```

This embeds the current v1 grammar. It should be allowed inside a pattern, but marked as low-level. The parser can require an `intent` annotation:

```trance
mechanics "exact legacy SuperFast FSM" {
  ...
}
```

### Level 3: raw render block

```trance
render.raw "bespoke start/end fade" {
  image start ...
  image end ...
  text when [...]
}
```

Again: allowed, but not the default.

The important rule is that an escape hatch must declare **what it exports back to the intent layer**: lanes, registers, shape bindings, and overlay labels. Otherwise it is opaque to debugging.

---

## 7. Relationship to Q1: readable grammar vs intent grammar

They are related, but not the same.

A readable grammar could still be:

```trance
repeat 12 {
  offset 0  every 16 : image primary -> img0
  offset 32 every 16 : image primary -> img1
  offset 64 every 16 : image alternate -> img2
}
```

That is readable mechanics. It still does not say:

> “This is an overload stack of three staggered image voices.”

Intent grammar must name the conceptual object:

```trance
present stack images [img0,img1,img2] stagger 32f active 16f
```

So I would use **four layers**:

```text
1. Intent Surface
   phases, voices, signals, theme relationships, render shapes

2. Score IR
   canonical semantic model:
   resolved voices, cadences, signals, shapes, source spans, origin metadata

3. Frame IR
   current pattern::Node-like AST:
   seq/par/one/repeat/offset/action/burst/effects/render statements

4. Runtime
   Cycler tree + Registers + VisualControl / VisualRender
```

The current docs already describe today’s pipeline as pattern DSL → `pattern::Node` → `Cycler` tree → `VisualControl` effects/render.  I am inserting an **Intent Surface** and **Score IR** above the existing `pattern::Node`.

### Why Score IR is necessary

Without Score IR, the overlay cannot show intent. The F1 roadmap correctly says the current cycler tree is the compiled form, not the human form; it proposes showing the actual DSL source with live highlights and preserving source spans/origin metadata through desugaring. 

That becomes much easier if the compiler has a semantic Score IR:

```cpp
Voice img2
  source: image
  theme: 1
  cadence: 96f
  offset: 64f
  active: 16f
  shape_binding: stack.images[2]
  origin_span: source lines X-Y
```

Then the overlay can show:

```text
SUPER_PARALLEL
intent: overload — 3 staggered image voices

active now:
  img0  theme 0  stack layer alpha 1.0
  img1  theme 0  stack layer alpha 0.5
  img2  theme 1  stack layer alpha 0.33
  words every 32f
```

The existing overlay plan says runtime state lines like themes, layers, spiral, and entrainment bed remain useful because they are live observations, not grammar structure.  That is exactly the split: show intent source plus live state, not raw cycler internals first.

---

## 8. Should the current per-frame AST survive?

Yes, but not unchanged.

I would keep the current `pattern::Node` as **Frame IR v1**, because it is already a good lowering target: it carries bounded schedule structure, effects, render data, and compiles cleanly to Cyclers. The current `Node` has `Action`, `Seq`, `Par`, `One`, `Rep`, `Off`, and `Burst`, with leaf timing/effects and burst parameters. 

But I would change its role and add metadata:

```cpp
struct FrameNode {
  NodeKind kind;
  SourceOrigin origin;       // points back to Intent/Score node
  std::string voice_id;      // optional semantic lane
  std::string phase_id;
  std::string shape_id;
  std::string debug_label;
  ...
}
```

And I would stop putting author-facing meaning on `Cycler` itself. The F1 roadmap already identifies `set_phase`, `set_image_slot`, `image_label`, and perhaps virtual `index()` as runtime annotations that can be deleted once the overlay derives narration from Node/source-origin mappings. 

### What changes in the AST

I would evolve the AST in four ways:

1. **Add source spans and origin metadata.** Required for overlay highlighting and for generated/desugared nodes.

2. **Add semantic ids.** `voice_id`, `phase_id`, `shape_binding`, `signal_id`.

3. **Generalize `Slot`.** Today `Slot` is `None`, `Primary`, `Alternate`, `Runtime`.  It should become a theme expression in Score IR and maybe a resolved small integer in Frame IR.

4. **Separate render shape from render statements.** Keep `RenderStmt` as the final render IR, but let Score IR carry `RenderShape`. A shape lowers to render statements late.

---

## 9. Recommended implementation sequence

I would not jump straight to a full parser rewrite.

### Step 1 — Create Score IR in C++

Before syntax, define the semantic data model:

```cpp
IntentPattern
Phase
Voice
Signal
ThemeExpr
CadenceExpr
RenderShape
LifecycleAction
```

Then manually build Score IR for the eight built-ins and lower it to the current AST. This tests the model without bikeshedding syntax.

### Step 2 — Implement shape lowering

Implement `focus`, `fade`, `stack`, `cut`, and perhaps `field`.

A shape should lower to today’s data-driven `RenderStmt` list. That lets you reuse the existing render evaluator while keeping the authoring surface shape-based.

### Step 3 — Implement modifiers as desugarings

Start with:

```trance
alternating
every nth
choice(...)th
chance(...)
ramp
stagger
active
burst
```

Each should lower to either existing registers/effects or existing Cycler structure.

### Step 4 — Build the overlay against Score IR

Store source text, source spans, and origin mappings. Show the intent source with live multi-highlights, not the Cycler tree by default. Keep raw Cycler view behind a debug toggle.

### Step 5 — Add syntax

Once the eight built-ins can be expressed in Score IR, write the parser.

### Step 6 — Compatibility

Keep v1 parser support or add a versioned source field. Stored custom patterns are raw source today, and the roadmap notes that a v2 replacement needs a compatibility story before it can become default. 

---

## Bottom line

The right model is:

```text
pattern = story over time
story = phases
phase = voices + signals
voice = content stream with cadence and theme relationship
shape = how voices become pixels
lowering = voices/signals/shapes → frame schedule/effects/render statements → Cyclers
```

The current AST should survive as a lowering target. But the new authoring surface should make `slow_flash`, `accelerate`, `super_parallel`, and `super_fast` read like the experiences they are:

```trance
slow_flash     = build → flash
accelerate     = ramp cut cadence 56f → 12f
super_parallel = stack three staggered voices
super_fast     = fast cuts with burst accents
sub_text       = focus image with evolving subtext field
flash_text     = text-led image handoff
simple         = steady focus with every-third accent
animation      = animation focus with fade windows
```

That is not just “readable grammar.” It is a different semantic layer.
