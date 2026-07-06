# Trance Visual Grammar — design history (appendix)

> **This is historical design material, not the current reference.** It captures
> the "Framing A vs Framing B" debate and the migration plan that led to today's
> data-driven visual system. **That plan is now done** — every visual is a
> compiled pattern and the seven hardcoded `*Visual` classes have been deleted
> (see §7–8 below for the landed state). For how the system works *today*, read
> **[visuals.md](visuals.md)** (the as-built reference) and
> **[authoring-v3-patterns.md](authoring-v3-patterns.md)** (the user
> guide; the v1 grammar and its authoring doc were deleted when v3 became the
> only parser). The §2 grammars below describe the visuals' behaviour as they were
> originally hand-built in C++; they remain accurate as a behavioural description
> but reference deleted classes and `visual.cpp:NNN` line numbers that no longer
> exist. Kept for the rationale and the entropy/primitive audit (§3–5).

A formal account of the cycler/visual system, written for two purposes:

1. **Framing A (descriptive, do-now):** annotate the existing hand-built cycler
   trees so the F1 overlay can *narrate* what's happening — current themes, current
   phase, and what changes next — instead of dumping a raw structural tree.
2. **Framing B (authorable, later):** a pasteable pattern DSL that compiles to
   cycler trees, so patterns can be authored from Creator / `.session` files with
   no recompile.

This document is the theoretical grammar for every shipped pattern, the descriptor
schema, the integration plan, and — importantly — the list of places where the
neat theory collides with how the code actually behaves. Read §3 before writing a
line of Framing A code; it is the "oh I didn't think about that" section.

---

## 0. The key realization

The cycler system **is already a grammar.** Every `Visual` constructor hand-builds
a tree of combinator nodes (`Action`, `OneShot`, `Parallel`, `Sequence`, `Repeat`,
`Offset`), and advancing that tree one frame at a time *is* executing a little
program. We are not inventing a grammar; we are adding **intent** to one that
already runs. Today the engine has *schedule without intent*: the tree knows it is
a `Sequence` of two 64-frame things; it does not know one means "slow" and the
other "fast."

**Chomsky placement (for the curious):**
- *Executing* an instantiated tree is **regular / finite-state.** Every node is a
  bounded counter; `Parallel` is the synchronous product of bounded counters (its
  `length` is literally the LCM = the product automaton's cycle length).
  Concurrency / "many things at once" does **not** raise you up the hierarchy — a
  product of finite automata is still a finite automaton.
- *Parsing pattern definitions* (nested `Seq[ Par[ Repeat[...] ] ]` brackets) is
  **context-free.** A pushdown automaton shows up only in the Framing-B parser,
  never in the player.

The practical consequence: the runtime has no stack and needs none. The hard part
is not automata theory; it is that lambdas hide state the tree can't see (§3).

---

## 1. Notation

```
Action(len)                  no-op timer of length `len`
Action(len @k)               fires its effect on frame k of every len
Action(/frame)               length 1: fires every frame
  :: <effect>                the effect(s) a leaf performs (the descriptor)
Seq[ A, B, ... ]             children in order; length = sum
Par[ A, B, ... ]             children together, repeating; length = LCM
One[ A, B, ... ]             children together, once; length = max
Rep(n, X)                    X repeated n times; length = n·len(X)
Off(k, X)                    X phase-shifted by k frames; length = len(X)
«label»                      a phase label attached to a subtree
[slot1] / [slot2] / [slot?]  image theme slot: primary / alternate / runtime-decided
```

Effects (the leaf descriptor vocabulary) map 1:1 onto `VisualControl`:

| Effect            | API call                       | Carries        |
|-------------------|--------------------------------|----------------|
| `image(slot)`     | `get_image(alternate)`         | slot           |
| `text(split,slot)`| `change_text(split, alternate)`| split type, slot |
| `subtext(slot)`   | `change_subtext(alternate)`    | slot           |
| `small_sub(slot)` | `change_small_subtext(f, alt)` | slot           |
| `anim(slot)`      | `change_animation(alternate)`  | slot           |
| `font`            | `change_font(force)`           | —              |
| `spiral_new`      | `change_spiral()`              | —              |
| `spiral_rot(x)`   | `rotate_spiral(x)`             | rate           |
| `themes`          | `change_themes()`              | — (returns bool, **ignored everywhere**) |
| `upload`          | `maybe_upload_next()`          | —              |
| `noop`            | empty timer                    | —              |

A leaf may carry **more than one** effect (see §3.4) — the descriptor is a *list*,
not a scalar.

---

## 2. The eight grammars

Frame counts are exact (derived from the constructors). `slot` is marked `[slot?]`
where the `alternate` argument is a runtime-captured bool rather than a literal
(see §3.2). The proto `VisualType` enum and the C++ class don't line up: enum 5
(`PARALLEL`) is `SimpleVisual`; enum 6 (`SUPER_PARALLEL`) is `ParallelVisual`.

### 2.1 ACCELERATE — `visual.cpp:33`
```
One[
  «init»  Action(1) :: { font, spiral_new, themes ; _animation_mod = 2<<rand(3) },
  «accelerating»  Seq[ seg(56), seg(55), …, seg(12) ]      // 45 segments, L = 56..12
]

seg(L) = Rep( image_count(L),
           One[ Par[ img(L), spiral(L), upload(L) ], text(L) ] )

  img(L)    = Action(L)        :: { image[slotA(L)] ; anim[slotA(L)] every _animation_mod-th }
  spiral(L) = Action(/frame)   :: spiral_rot(1 + (56-L)/16)        // rate ramps up
  upload(L) = L>24 ? Action(L @L/2) :: upload : Action(L) :: noop
  text(L)   = Action(fastest?L:8) :: { text(fastest?WORD:LINE, slotA(L)) toggled by _text_on }
  where  slotA(L) = (L/12)%2==0      // a per-segment loop CONSTANT → statically known
         fastest  = L < 16
         image_count(L) = 1 + ((56-L)^6) / 56^5     // ramps 1 → ~14, see §3.6
```
Phase: one ramp. The "acceleration" is the shrinking `L` (image period 56→12
frames), the rising spiral rate, **and** the rising `image_count` — the short fast
segments repeat up to ~14× while the slow early ones run once. `slotA` alternates
per segment and **is predictable** because it is a loop constant and the active
segment is the `Seq`'s active child.

### 2.2 SUB_TEXT — `visual.cpp:102`
```
Par[
  «spiral»  Action(/frame) :: spiral_rot(4),
  One[
    «init» Action(1) :: { themes, font, spiral_new ; _animation_mod = rand∈{3,5,7} ; ++_sub_speed },
    Rep(16, loop)
  ]
]

loop = Par[ image, upload, text_loop, sub0, sub1, sub2 ]      // length 96
  image     = Action(48)   :: { _alternate=!_alternate ; image[slot?] ; anim[slot?]×2 }  // §3.4 double anim
  upload    = Action(48 @24):: upload
  text_loop = Seq[ Action(4)::text(WORD,slot?), Rep(23, Action(4)::text(ONCE_ONLY)) ]
  sub0      = Action(12) :: { if _sub_speed==1: subtext[slot?] }     // §3.3 gated
  sub1      = Action(24) :: { if _sub_speed==2: subtext[slot?] }
  sub2      = Action(48) :: { if _sub_speed>=3: subtext[slot?] }
```
Phase: none structural. The *felt* phase is the subtext rate (12→24→48 frames)
which slows as `_sub_speed` climbs once per main restart — invisible to the tree.

### 2.3 SLOW_FLASH — `visual.cpp:165`  ← the user's canonical "slow then rapid"
```
One[
  «init»  Action(1) :: themes,
  Rep(2, Seq[ slow, fast ])                                  // 3072 frames total
]

«slow» (1024f) = Par[
  One[ Action(1)::{spiral_new,font}, Rep(16, Action(64)::{ image[slot1]; anim[slot1=false];
                                                           text(LINE); small_sub }) ],
  Action(/frame)::spiral_rot(2),
  Action(64 @32)::upload
]

«fast» (512f) = Par[
  One[ Action(1)::{spiral_new,font}, Rep(32, Par[ Action(8)::image[slot2], Action(16 @8)::text(WORD,slot2) ]) ],
  Action(16 @0)::small_sub(slot2),
  Action(/frame)::spiral_rot(4)
]
```
Phase: `«slow»` / `«fast»` are exactly the two children of the `Seq`, and the
`SequenceCycler` already marks precisely one of them `active()`. This is the clean
case: **current phase = the active `Seq` child's label.** Both image slots here are
literal (`get_image()`=primary in slow, `get_image(true)`=alternate in fast).

### 2.4 FLASH_TEXT — `visual.cpp:225`
```
One[
  Par[
    Action(/frame)::spiral_rot(2.5),
    Action(64)::font,
    Rep(2, Action(32)),                              // subtext_counter timer
    Action(32)::small_sub,
    Action(64 @32)::upload,
    Rep(8, One[ Action(1)::{ _alternate=!_alternate ; text(LINE, slot?) },
                Rep(2, Action(64)::{ _start=_end ; if _animated anim[slot?] ; _end=image[slot?] }) ])
  ],
  Action(1)::themes        // 1-frame; fires at frame 0
]
```
Phase: none structural. Behaviour gated by `_animated` (a `random_chance()` rolled
at construction and re-rolled in `reset()`). Two-image crossfade between `_start`
and `_end`. `_alternate` toggled by a *separate* node from the one that reads it.

### 2.5 PARALLEL (enum 5) = SimpleVisual — `visual.cpp:286`
```
One[
  «init» Action(1)::{ spiral_new, font, themes },
  Par[
    Action(/frame)::spiral_rot(3),
    Action(32)::small_sub,
    Action(32 @16)::upload,
    Rep(16, Par[ Rep(4, Action(32)),                                   // counter 128
                 Action(64)::{ ++_anim_cycle ; image[slot?=_anim_cycle%3==1] ; if ++_anim_cycle%3==2: anim[false] },
                 Action(128)::text(LINE, rand) ])
  ]
]
```
Slot is `_anim_cycle % 3 == 1`, a running parity — runtime, not predictable.

### 2.6 SUPER_PARALLEL (enum 6) = ParallelVisual — `visual.cpp:330`  ← the "interleave" example
```
One[
  «init» Action(1)::{ spiral_new, font, themes },
  Par[
    Action(/frame)::spiral_rot(3.5),
    Action(32 @16)::upload,
    Action(32)::text(WORD, rand),
    Rep(12, One[ Action(1)::{_alternate_animation=!…},
                 «interleave» Par[ loop(0), loop(1), loop(2) ] ])
  ]
]

loop(i) = Off( i·32, Seq[ set(i), single(i), Action(64) ] )            // staggered by 32f
  set(i)    = Action(16) :: { _images[i] = image[ i>=2 ? slot2 : slot1 ] ; if i==0 anim[_alt_anim] }
  single(i) = Action(16)                  // render reads single(i).active() → "single image" mode
```
Phase: `«interleave»`. The staggering is three `Off` sequences 32 frames apart;
their heads sit at different points so images cross-fade in/out out of step — this
*is* what the user means by "now they're interleaved." Slots here are **literal**
(`i>=2`). Note: `Offset` is used in **exactly one place in the whole codebase**.

### 2.7 ANIMATION — `visual.cpp:388`
```
One[
  «init» Action(1)::{ spiral_new, font, themes },
  Rep(8, Par[
    Action(/frame)::spiral_rot(3.5),
    Action(32)::small_sub,
    Action(32 @24)::upload,
    Action(32)::{ _animation_backup = image[slot1] },
    Action(64 @32)::{ _current = image[slot2] },
    Action(16),                                            // image_timer
    Seq[ Action(64 @0)::{ text(LINE) ; anim[false] }, Action(64 @0)::{ text(LINE,true) ; anim[true] } ],
    Rep(2, Action(32))                                     // change_counter
  ]),
  «window» Seq[ Action(32), Action(960), Action(32) ]      // start / hold / end fade window
]
```
The `start_end_timer` (`Seq` of 32/960/32) is a sibling of the main loop used purely
by the render path to fade the crossfade image in at the start and out at the end.

### 2.8 SUPER_FAST — `visual.cpp:445`  ← the BLOCKER for any tree-only narration
```
One[
  «init» Action(1)::{ spiral_new, font, themes },
  Par[
    Action(2048),                       // hard length cap for the whole visual
    Action(8) :: «OPAQUE FSM»,          // ← see below
    Action(/frame)::spiral_rot(3)
  ]
]
```
The `Action(8)` lambda **is** the visual. It runs a hand-rolled state machine —
`RAPID → START_ANIMATION → ANIMATION → END_ANIMATION` — over `_state`,
`_cooldown_timer`, `_animation_timer`, with transitions gated on `random_chance(12)`
and the hidden timers. The cycler tree sees one static 8-frame node forever. **None
of this visual's real phases are tree-visible.** Narrating it requires the visual to
expose `_state` directly (§3.1).

### 2.9 Lowest-level annotated forms for the seven tree-expressible patterns

This is the data-shape version of §2. It is intentionally lower-level than the
readable grammars above: every node has a stable `id`, section labels live in
`phase`, leaves carry an ordered `effects` list, and effects include the annotation
items the overlay/compiler needs.

Notation:
```
A#id(len[, @frame], effects=[...], flags=[...])
Seq#id{...}  Par#id{...}  One#id{...}  Rep#id(n, X)  Off#id(k, X)
phase="NAME"              overlay section label on a real subtree
target=name               register written by an effect
slot=primary|alternate|runtime(expr)|random|const(expr)
conditional=expr          leaf may legally no-op
opaque=expr               payload depends on hidden C++ state
render_ref                render code reads this node's active/index/progress/frame
constructor=[...]         setup work done outside the cycler tree today
```

`SUPER_FAST` is excluded here on purpose: its useful grammar is a hidden FSM inside
one `Action(8)`, so it does not share this data shape without adding the rejected
state-machine primitive.

#### ACCELERATE
```
One#accel.root{
  A#accel.init(1,
    effects=[set(animation_counter=0), set(animation_mod=random{2,4,8}), font, spiral_new, themes],
    flags=[opaque=random])

  Seq#accel.ramp phase="RAMP" {
    generate L in 56..12:
      Rep#accel.seg[L](image_count(L),
        One#accel.oneshot[L]{
          Par#accel.image_lane[L]{
            A#accel.image[L](L,
              effects=[
                image(target=current, slot=const((L/12)%2==0)),
                set(animation_on=false),
                anim(slot=const((L/12)%2==0), conditional="++animation_counter == animation_mod"),
                set(animation_alternate=const((L/12)%2==0), conditional="animation fired")
              ],
              flags=[opaque=animation_counter])
            A#accel.spiral[L](1, effects=[spiral_rot(1 + (56-L)/16)])
            A#accel.upload[L](L, @L/2, effects=[upload], conditional="L > 24")
          }
          A#accel.text[L](L<16 ? L : 8,
            effects=[text(split=L<16 ? WORD : LINE, slot=const((L/12)%2==0))],
            flags=[conditional="text_on toggle unless fastest", opaque=text_on, render_ref])
        })
  }
}
```

#### SUB_TEXT
```
Par#sub.root{
  A#sub.spiral(1, effects=[spiral_rot(4)])
  One#sub.main{
    A#sub.init(1,
      effects=[
        set(animation_counter=0), set(animation_mod=random{3,5,7}),
        themes, font, spiral_new, increment(sub_speed_multiplier)
      ],
      flags=[opaque=random])
    Rep#sub.repeat(16,
      Par#sub.loop{
        A#sub.image(48,
          effects=[
            toggle(alternate),
            image(target=current, slot=runtime(alternate)),
            set(animation_on=false),
            anim(slot=runtime(alternate), conditional="++animation_counter == animation_mod"),
            anim(slot=runtime(alternate))
          ],
          flags=[opaque=alternate, opaque=animation_counter])
        A#sub.upload(48, @24, effects=[upload])
        Seq#sub.text_loop{
          A#sub.text_reset(4, effects=[text(split=WORD, slot=runtime(alternate))])
          Rep#sub.text_tail(23, A#sub.text_once(4, effects=[text(split=ONCE_ONLY, slot=runtime(alternate))]))
        }
        A#sub.sub0(12, effects=[subtext(slot=runtime(alternate))],
          flags=[conditional="sub_speed_multiplier == 1"])
        A#sub.sub1(24, effects=[subtext(slot=runtime(alternate))],
          flags=[conditional="sub_speed_multiplier == 2"])
        A#sub.sub2(48, effects=[subtext(slot=runtime(alternate))],
          flags=[conditional="sub_speed_multiplier >= 3"])
      })
  }
}
```

#### SLOW_FLASH
```
One#slowflash.root{
  A#slowflash.init(1, effects=[themes])
  Rep#slowflash.repeat(2,
    Seq#slowflash.phases{
      Par#slowflash.slow phase="SLOW" {
        One#slowflash.slow_main{
          A#slowflash.slow_enter(1, effects=[spiral_new, font])
          Rep#slowflash.slow_repeat(16,
            A#slowflash.slow_loop(64,
              effects=[image(target=current, slot=primary), anim(slot=primary),
                       text(split=LINE, slot=primary), small_subtext(force=true, slot=primary)],
              flags=[render_ref]))
        }
        A#slowflash.slow_spiral(1, effects=[spiral_rot(2)])
        A#slowflash.slow_upload(64, @32, effects=[upload])
      }
      Par#slowflash.fast phase="FAST" {
        One#slowflash.fast_main{
          A#slowflash.fast_enter(1, effects=[spiral_new, font])
          Rep#slowflash.fast_repeat(32,
            Par#slowflash.fast_loop{
              A#slowflash.fast_image(8, effects=[image(target=current, slot=alternate)])
              A#slowflash.fast_text(16, @8, effects=[text(split=WORD, slot=alternate)])
            })
        }
        A#slowflash.fast_subtext(16, @0, effects=[small_subtext(force=true, slot=alternate)])
        A#slowflash.fast_spiral(1, effects=[spiral_rot(4)])
      }
    })
}
```

#### FLASH_TEXT
```
constructor=[
  set(animated=random_chance), image(target=end, slot=alternate), set(alternate=true)
]

One#flashtext.root{
  Par#flashtext.body{
    A#flashtext.spiral(1, effects=[spiral_rot(2.5)])
    A#flashtext.font(64, effects=[font(force=true)])
    Rep#flashtext.subtext_gate(2, A#flashtext.subtext_timer(32), flags=[render_ref])
    A#flashtext.small_subtext(32, effects=[small_subtext(force=true, slot=primary)])
    A#flashtext.upload(64, @32, effects=[upload])
    Rep#flashtext.main_repeat(8,
      One#flashtext.main_once{
        A#flashtext.alternate(1,
          effects=[toggle(alternate), text(split=LINE, slot=runtime(alternate))],
          flags=[opaque=alternate])
        Rep#flashtext.image_repeat(2,
          A#flashtext.image(64,
            effects=[
              copy(start=end),
              anim(slot=runtime(alternate), conditional=animated),
              image(target=end, slot=runtime(alternate))
            ],
            flags=[opaque=animated, opaque=alternate, render_ref]))
      })
  }
  A#flashtext.themes(1, effects=[themes])
}
```

#### PARALLEL / SimpleVisual
```
constructor=[set(anim_cycle=2), image(target=image, slot=primary)]

One#simple.root{
  A#simple.init(1, effects=[spiral_new, font, themes])
  Par#simple.body{
    A#simple.spiral(1, effects=[spiral_rot(3)])
    A#simple.small_subtext(32, effects=[small_subtext(force=true, slot=primary)])
    A#simple.upload(32, @16, effects=[upload])
    Rep#simple.repeat(16,
      Par#simple.loop{
        Rep#simple.counter(4, A#simple.counter_tick(32), flags=[render_ref])
        A#simple.image(64,
          effects=[
            increment(anim_cycle),
            image(target=image, slot=runtime(anim_cycle % 3 == 1)),
            increment(anim_cycle),
            anim(slot=primary, conditional="anim_cycle % 3 == 2")
          ],
          flags=[opaque=anim_cycle, render_ref])
        A#simple.text(128, effects=[text(split=LINE, slot=random)])
      })
  }
}
```

#### SUPER_PARALLEL / ParallelVisual
```
constructor=[
  image(target=images[0], slot=primary),
  image(target=images[1], slot=primary),
  image(target=images[2], slot=alternate),
  set(alternate_animation=true)
]

One#superparallel.root{
  A#superparallel.init(1, effects=[spiral_new, font, themes])
  Par#superparallel.body{
    A#superparallel.spiral(1, effects=[spiral_rot(3.5)])
    A#superparallel.upload(32, @16, effects=[upload])
    A#superparallel.text(32, effects=[text(split=WORD, slot=random)])
    Rep#superparallel.repeat(12,
      One#superparallel.once{
        A#superparallel.toggle_anim(1, effects=[toggle(alternate_animation)])
        Par#superparallel.interleave phase="INTERLEAVE" {
          generate i in 0..2:
            Off#superparallel.offset[i](i*32,
              Seq#superparallel.progress[i] flags=[render_ref] {
                A#superparallel.set[i](16,
                  effects=[
                    image(target=images[i], slot=const(i>=2 ? alternate : primary)),
                    anim(slot=runtime(alternate_animation), conditional="i == 0")
                  ],
                  flags=[opaque=alternate_animation])
                A#superparallel.single[i](16, flags=[render_ref])
                A#superparallel.hold[i](64)
              })
        }
      })
  }
}
```

#### ANIMATION
```
One#animation.root{
  A#animation.init(1, effects=[spiral_new, font, themes])
  Rep#animation.repeat(8,
    Par#animation.loop{
      A#animation.spiral(1, effects=[spiral_rot(3.5)])
      A#animation.small_subtext(32, effects=[small_subtext(force=true, slot=random)])
      A#animation.upload(32, @24, effects=[upload])
      A#animation.image_backup(32, effects=[image(target=animation_backup, slot=primary)])
      A#animation.image_current(64, @32, effects=[image(target=current, slot=alternate)])
      A#animation.image_timer(16, flags=[render_ref])
      Seq#animation.text_anim_pair{
        A#animation.change_primary(64, @0,
          effects=[text(split=LINE, slot=primary), anim(slot=primary)],
          flags=[render_ref])
        A#animation.change_alt(64, @0,
          effects=[text(split=LINE, slot=alternate), anim(slot=alternate)],
          flags=[render_ref])
      }
      Rep#animation.change_counter(2, A#animation.half_counter(32), flags=[render_ref])
    })
  Seq#animation.window flags=[render_ref] {
    A#animation.window_in(32)
    A#animation.window_hold(960)
    A#animation.window_out(32)
  }
}
```

---

## 3. Framing A reality check — where the theory breaks

The current overlay (`director.cpp:455`, `append_cycler`) is *sound* because it
reads only `type_name / position / length / progress / active`, none of which depend
on hidden state. Framing A first cut adds two current-state claims — **current
phase** and **theme slot**. Future lookahead / next-event prediction is explicitly
deferred; if it returns, it must stay timing-only and treat hidden payload state as
opaque. The honest design that falls out of this section:

> **For current-state debug, reuse the existing `active()` flag for phase; never
> re-derive it. Emit a concrete slot only when the slot is literal or compile-time
> constant; otherwise label it runtime/opaque. If lookahead is ever revived, predict
> only TIMING from the tree and keep conditional/multi-effect payloads opaque.**

### 3.1 Opaque lambda state (BLOCKER for SUPER_FAST, MAJOR elsewhere)
`SuperFastVisual`'s phases live entirely in `_state` (`visual.cpp:449–491`), not the
tree. `AccelerateVisual`'s animation cadence is `_animation_counter == _animation_mod`
with `_animation_mod` randomized in a *different* node (`:78`). Alternation bools
(`_alternate`, `_alternate_animation`, `_anim_cycle`, `_text_on`, `_sub_speed`) are
all invisible.
*Mitigation:* **none, by choice.** Seven visuals have schedulable tree shapes, but
only viewer-named sections that are real subtrees should receive `phase` labels.
The first cut labels `RAMP`, `SLOW`/`FAST`, and `INTERLEAVE`; other patterns may
have stable ids/render refs without claiming a section. SUPER_FAST has no useful
tree-visible state, so the overlay simply shows `section --` for it. A
`Visual::debug_phase()` virtual was considered and **rejected** as a debug-only
appendage (a smell). The only clean way to make SUPER_FAST's phase tree-visible is
to refactor its FSM into a grammar node — and that was evaluated and dropped too
(§4.1).

### 3.2 Slot predictability — classify every `get_image` site
**Literal or compile-time constant:** SlowFlash primary/alternate; FlashText
constructor alternate; Simple constructor primary; Animation primary/alternate;
SuperParallel constructor and `set(i)` (`i>=2` selects alternate); Accelerate's
per-segment `slotA(L)` once the active generated segment is known.
**Runtime bool/modulo (NOT statically predictable):** SubText `_alternate`;
FlashText `_alternate`; Simple `_anim_cycle % 3`; SuperFast `_alternate`.
*Mitigation:* print `slot=runtime` for hidden-state sites rather than guessing.

### 3.3 Conditional no-op leaves (MAJOR)
SubText `sub0/sub1/sub2` (`:131–145`) each fire every 12/24/48 frames but only one
actually calls `subtext`, gated on `_sub_speed`. Accelerate's `text_action` (`:59`)
fires every *other* trigger via `_text_on`. A naive predictor announces events that
don't happen.
*Mitigation:* tag gated leaves "conditional"; never assert they fire.

### 3.4 Multi-effect leaves (MAJOR)
A single lambda often does several API calls: every `«init»` does `spiral_new +
font + themes`; SlowFlash `slow_loop` does `image + anim + text + small_sub`; SubText
`image` calls `change_animation` **twice** (one gated, one unconditional, `:120` &
`:123`). A scalar `{kind, slot}` silently drops calls.
*Mitigation:* the descriptor is a **list** of effects.

### 3.5 Cycler-semantics traps for a forward walk
- **`Offset` pre-advances at construction** (`cyclers.cpp:359`, `advance_to_offset`):
  the child's internal position is already shifted at t=0 while `Offset::position()`
  starts at 0. Don't reason about offset arithmetically — step a copy.
- **`Sequence::index()` uses `frame()`, `active()` uses `position()`**
  (`cyclers.cpp:234` vs `:300`): at a boundary they name adjacent segments. Pick one
  accessor per call site; don't assume they agree.
- **`OneShot::active()` = `position() <= child.position()`** with parent position =
  `max(children)` (`cyclers.cpp:144`): the *longest* child stays active and shorter
  ones switch off once overtaken. Reuse this flag; do not re-derive "deepest active."
- **`Repeat::index()` = `frame()/sublen`** (`cyclers.cpp:324`): at wrap, `frame()`
  reports the *last* repetition, so a forward step sees index jump backward.
- **`OneShot::advance` may reset-and-re-advance in one call** (`cyclers.cpp:118`):
  sample state only *after* a full `advance()` returns, never mid-call.

**Safe prediction technique:** deep-copy the subtree, call `advance(false)` (actions
disabled) N times, sample after each full call. This respects offset pre-advance and
the reset-and-re-advance automatically — but it still cannot reveal which branch a
conditional lambda takes, so payloads stay opaque. Timing is exact; intent is not.

### 3.6 The non-obvious ACCELERATE ramp (and a self-caught error)
`ACCELERATE`'s `image_count(L) = 1 + ((56-L)^6)/56^5` (`visual.cpp:42` — the
denominator is `56*56*56*56*56`, **five** factors = 56^5). At `L=12` (`d=44`),
`44^6/56^5 ≈ 13.2`, so `image_count` ramps from 1 (slow early segments) to ~14
(fast late segments). The `Repeat` is therefore **load-bearing** — it is what makes
the tail repeat rapidly. *(An earlier draft of this doc miscounted the exponent as
56^6 and wrongly called the `Repeat` dead; verifying against source corrected it.
This non-obvious integer-power falloff is exactly the kind of thing Framing B's
`generate` construct (§5.4) must reproduce faithfully.)*

---

## 4. What the original creator left on the table

Surfaced by the entropy audit (concepts the visuals strain toward but the
primitives lack). These are the seeds of Framing B's primitive set.

**Unused / dead today:**
- `SPLIT_WORD_GAPS` (2) and `SPLIT_LINE_GAPS` (3) — **zero call sites.** Two
  text-reveal styles ship but are unreachable from any visual.
- `change_subtext` / `render_subtext` (the *large* subtext lane) — **SubText only.**
- `change_themes()`'s `bool` return — **discarded at all 8 sites.** The "did the
  theme actually swap" signal exists and nobody branches on it.
- `OffsetCycler` — **one call site** (SuperParallel). Never nested in `Repeat`,
  never offsetting an `Action` or `Parallel`.
- Spiral type/width — always uniform-random; no visual ever selects a specific one.
- `Action(len @k)` with `k ∉ {0, len/2}` — the mid-phase-offset action is
  expressible but never used.

**Missing primitives (each currently faked with lambda state):**
1. **~~General `StateCycler` / branching cycler~~ — rejected.** It *would* replace
   SuperFast's hand-rolled FSM (`:449–491`), but a general state machine needs named
   states, transitions, guards, random predicates, counters, and assignment — which
   turns the clean timing algebra into an imperative mini-language inside `.session`.
   That is the spaghetti the grammar exists to avoid.
2. **`RandomBurstCycler` / `burst` — narrow replacement for the rejected FSM.** This
   is the constrained version that may be worth adding: a base loop, randomly
   interrupted by a bounded burst, followed by cooldown. It names the visual concept
   in SUPER_FAST without exposing arbitrary control flow.
3. **`ChoiceCycler(weights)`** — pick 1 of N children, optionally weighted.
   Randomness is currently smeared across `random(3)`, `random_chance(12)`, etc.
4. **`OnceCycler` / enter-vs-steady** — "do X on entry, then Y for the rest";
   currently faked with `SPLIT_ONCE_ONLY` + `_sub_speed` ladders + `text_reset`.
5. **Rate divider** — "fire every N-th *completion* of a child"; replaces
   `_animation_mod`, `_sub_speed`, `_anim_cycle` counters.
6. **Transition / blend primitive** — cross-fades between layers (and ideally
   between two `Visual` instances) are re-implemented per visual in raw alpha math.
7. **Easing on `progress()`** — it is linear-only; every render lambda hand-rolls
   zoom curves.

---

## 5. Framing B — authorable pattern DSL spec (overhauled)

Goal: a pattern should be pasteable into `creator.exe` as readable text, validated
there, stored in the `.session`, and compiled at playback into the existing cycler
tree plus render/debug metadata. The authoring language should be **simpler than
the C++ tree**, not a serialized dump of it. The low-level forms in §2.9 are still
useful as compiler/debug IR, but they are not what a normal pattern author should
write.

### 5.1 What this design maximizes
- **Authorability:** common patterns should read like timing recipes: phases,
  beats, lanes, transitions, bursts.
- **Debug output:** every named phase/lane/effect in the source becomes debug
  metadata automatically. The overlay should not need parallel hand annotations.
- **Backwards compatibility:** existing `Program.visual_type` weights and all
  built-in C++ visuals remain valid. Custom patterns are additive.
- **Bounded execution:** no unbounded loops, no arbitrary assignment language, no
  user-defined functions, no general FSM.
- **Backend reuse:** compile to the current `Cycler` classes wherever possible so
  existing timing semantics (`Sequence`, `OneShot`, `Offset`, LCM `Parallel`) stay
  authoritative.
- **Gradual migration:** built-ins can coexist with compiled custom patterns until
  equivalence tests prove a replacement.

### 5.2 Three-layer model
1. **Surface DSL (what users paste):** semantic pattern recipes. Names concepts like
   `phase`, `every`, `transition`, `burst`, and `interleave`.
2. **Normalized pattern AST (what the parser produces):** a small, typed set of
   semantic nodes with explicit ids, durations, slots, registers, and effect lists.
3. **Cycler IR (what the player executes):** existing `ActionCycler`,
   `SequenceCycler`, `ParallelCycler`, `OneShotCycler`, `RepeatCycler`,
   `OffsetCycler`, plus a few narrow new semantic cyclers only where the current IR
   cannot express a shipped behavior cleanly.

Debug output is generated from layer 2 and live state from layer 3. That means the
same parsed source drives behavior, debug labels, and future Creator validation.

### 5.3 Surface language sketch
This is intentionally friendlier than the §2.9 IR:

```text
pattern SlowFlash {
  weight 10
  render zoom_image_text

  enter: themes

  repeat 2 {
    phase SLOW duration 1024 {
      enter: spiral.new, font
      every 64: image primary -> current, anim primary, text line primary, small_text primary
      every frame: spiral.rotate 2
      every 64 @32: upload
    }

    phase FAST duration 512 {
      enter: spiral.new, font
      every 8: image alternate -> current
      every 16 @8: text word alternate
      every 16: small_text alternate
      every frame: spiral.rotate 4
    }
  }
}
```

The compiler lowers each `phase` body to a `Parallel` of lanes, `every N` to an
`Action(N)`, `every N @K` to an `Action(N @K)`, `repeat` to `Repeat`, and lexical
order to `Sequence` where needed. If `duration` is omitted the compiler can infer it
from lane lengths; if supplied, it validates the lanes fit or pads with `noop`.

### 5.4 Minimal grammar concepts
The surface grammar should stay small:

| Concept | Purpose | Backend lowering |
|---|---|---|
| `pattern` | Named authorable visual with weight and render preset | `VisualPatternSource` |
| `enter` | One-shot effects at pattern/phase entry | `Action(1)` inside `OneShot` |
| `phase NAME` | Viewer/debug-named section | `Cycler::phase(NAME)` on a subtree |
| `every N [@K]` | Periodic effect lane | `ActionCycler(N, K)` |
| `repeat N` | Bounded loop | `RepeatCycler` |
| `together {}` | Explicit parallel group | `ParallelCycler` |
| `sequence {}` | Explicit ordered group | `SequenceCycler` |
| `delay K {}` | Phase shift/stagger | `OffsetCycler` |
| `generate VAR from A down_to B` | Bounded expansion for ramps | compile-time AST expansion |
| `choice weighted {}` | Pick one branch at entry/reset | narrow `ChoiceCycler` |
| `divide every N completions` | Fire every N-th child completion | narrow rate-divider cycler |
| `transition` | Named crossfade/window behavior | render preset + timer nodes |
| `burst` | Random bounded interruption with cooldown | narrow `RandomBurstCycler` |

The language deliberately does **not** include arbitrary `if`, `while`, mutable
assignment, or user-defined states. When a shipped pattern needs hidden state, add a
named semantic primitive only if that primitive is understandable to an author and
debuggable in the overlay.

### 5.5 Effects, slots, and registers
Effects are still ordered lists, but the authoring surface should make them look
like commands:

```text
image primary -> current
image alternate -> images[2]
image.shift current <- next, next <- image toggle_slot
anim primary
text line primary
text word random
subtext alternate
small_text random
font
spiral.new
spiral.rotate 3.5
themes
upload
```

Slots:
- `primary`, `alternate`: literal theme slots `[1]` and `[2]`.
- `random`: choose a slot at fire time.
- `toggle_slot`: a primitive-owned toggle, not an arbitrary user variable.
- `generated(EXPR)`: allowed only inside `generate`; compile-time constant.

Registers:
- Fixed image registers: `current`, `next`, `start`, `end`, `backup`,
  `images[0..2]`.
- Primitive-owned scalar state: counters/toggles may exist **inside** `burst`,
  `choice`, `divide`, or `transition`, but the surface language does not expose
  arbitrary mutable variables.
- Render presets declare which registers and node ids they require. The compiler
  validates this before playback.

Defaults must be documented before implementation, not inferred from examples:
- `small_text SLOT` means `small_text force=true SLOT`, matching most current call
  sites. A later `small_text toggle SLOT` form can expose the force=false behavior.
- `font` means `font force=false`; `font force=true` is explicit.
- `text line SLOT` and `text word SLOT` map to the existing split modes. `text once`
  is explicit; the unused gap modes should be named explicitly if exposed.
- `image SLOT -> current` is the default target if `-> target` is omitted.
- `spiral.rotate X` is per-frame only when written under `every frame`; elsewhere it
  follows the lane period.

### 5.6 The narrow `burst` primitive for SUPER_FAST
The rejected idea was a general FSM. The useful idea is much smaller:

```text
pattern SuperFastLike {
  weight 10
  render rapid_burst

  enter: spiral.new, font, themes

  burst RAPID_ANIMATION {
    length 2048
    period 8
    chance 1/12
    cooldown 8
    duration random 8..16
    slot toggle_slot

    base:
      image.shift current <- next, next <- image toggle_slot
      text word toggle_slot every 4

    burst:
      enter anim toggle_slot
      freeze_base_images true
      upload_at_remaining 4
      reset_text_cadence_on_exit true
  }

  every frame: spiral.rotate 3
}
```

This affords `SUPER_FAST` without a mini-language. The primitive has a fixed state
shape (`base`, `enter_burst`, `during_burst`, `exit_burst`, `cooldown`) and fixed
debug names. Authors can tune timing and effects, but cannot invent arbitrary
states or transitions. `burst` is the stress test for the whole DSL: implement it
only if it stays this declarative. If the implementation wants generic guards,
assignments, or user-defined sub-states, leave SUPER_FAST in C++ instead.

### 5.7 Render strategies
First pass should use **named render presets**, not free-form render expressions.
Examples:
- `zoom_image_text`: one image register, optional animation, spiral, text/small text.
- `two_image_crossfade`: `start`/`end` registers with fade window.
- `triple_interleave`: `images[0..2]` with staggered progress nodes.
- `animation_window`: primary animation plus `current` crossfade window.
- `rapid_burst`: SUPER_FAST-style current/next image and burst animation.

Each render preset publishes a small contract:
```text
requires registers: current, next
requires nodes: burst.RAPID_ANIMATION
optional effects: text, spiral
```

B2 can add a render expression language later. It should not block the paste-a-new-
pattern Creator workflow.

### 5.8 Proto/session compatibility
Do not replace `Program.visual_type`; add custom patterns beside it at field 11:

```proto
message VisualPatternSource {
  string name = 1;
  uint32 random_weight = 2;
  string source_text = 3;       // the pasteable DSL, source of truth
  bool enabled = 4;
}

message Program {
  // existing fields unchanged...
  repeated VisualPatternSource custom_visual_pattern = 11;
}
```

Playback selection becomes:
1. add enabled built-in `visual_type` weights exactly as today;
2. parse/validate enabled `custom_visual_pattern` entries;
3. add valid custom patterns to the same weighted visual shuffle;
4. if a custom pattern fails parsing/validation, report it and skip that pattern,
   never the whole session.

This is backwards compatible for existing sessions and code paths. Older builds do
not know how to execute custom patterns, but they still have the built-in
`visual_type` list. Caveat: a session that contains only custom patterns and zero
built-in visual weights will render no custom visuals on older builds, so Creator
should warn before saving/exporting such a session. New Creator should preserve
`source_text` exactly and show parser diagnostics inline.

### 5.9 Compiler (`visual_compiler.cpp`, new)
`CompiledVisual build_from_source(const VisualPatternSource&, VisualControl&)`:
1. Parse source text to the normalized AST; report line/column diagnostics.
2. Validate boundedness: positive durations, finite `repeat`, finite `generate`,
   no empty groups, action frame `< period`, valid register names, and render preset
   requirements satisfied.
3. Expand semantic sugar (`phase`, `every`, `generate`) into AST nodes with stable
   ids. Generate debug descriptors at the same time.
4. Lower to existing `Cycler` classes wherever possible; lower only narrow
   primitives (`choice`, `divide`, `burst`) to new dedicated cyclers.
5. Allocate the fixed register block required by the effects/render preset.
6. Synthesize action lambdas from ordered effect lists. At this point descriptors
   become behavior, eliminating drift between debug and execution.
7. Bind the named render preset.

### 5.10 Migration and guardrails
Do not delete C++ visuals during the first implementation. Prove equivalence first:
1. **Parse/golden tests:** sample DSL snippets parse to the expected normalized AST.
2. **Structural tests:** hand-built and compiled trees match `position/length/active`
   over K frames with `advance(false)`.
3. **Action-log tests:** fake `VisualControl` records exact ordered effects per
   frame; compiled and hand-built visuals match.
4. **Render-log tests:** fake `VisualRender` records render calls; compiled and
   hand-built visuals match.
5. **Creator validation tests:** invalid pasted patterns produce actionable
   line/column errors and do not corrupt the session.

Suggested migration order:
1. Implement parser/storage/Creator validation with one render preset.
2. Compile `SLOW_FLASH` as DSL while keeping the C++ version.
3. Compile `ANIMATION` with `animation_window`.
4. Add `transition`, `generate`, and `triple_interleave` for ACCELERATE and
   SUPER_PARALLEL.
5. Add the narrow `burst` primitive for SUPER_FAST.
6. Only after tests pass, consider replacing built-ins or exposing a library of
   editable built-in pattern sources in Creator.

---

## 6. Recommended order of work

1. **Framing A first cut — landed.** Optional `phase` on `Cycler`, labels only on
   real viewer-named subtrees (`RAMP`, `SLOW`/`FAST`, `INTERLEAVE`), last-pulled
   theme-slot capture in `VisualApiImpl::get_image`, and the F1 overlay summary.
   No `debug_phase` hook; SUPER_FAST honestly reports no tree-visible section.
2. **Framing A hardening only.** Keep the overlay current-state: active section,
   theme slot, layers, entrainment, and tree labels. Do not add lookahead unless
   there is a concrete UX need for it.
3. **Framing B0 — parser/storage/Creator validation.** Add
   `Program.custom_visual_pattern = 11`, store source text, parse it to a normalized
   AST, and show line/column diagnostics in Creator. No built-in visual replacement
   yet.
4. **Framing B1 — compile simple authoring patterns.** Implement fixed registers,
   effect execution, and one or two named render presets. Compile SLOW_FLASH first,
   then ANIMATION, while keeping the C++ versions for equivalence testing.
5. **Framing B2 — semantic primitives, not a mini-language.** Add `generate`,
   `transition`, `choice`, `divide`, and the narrow `burst` primitive as needed.
   This is what can make all eight shipped patterns grammar-addressable without a
   general `StateCycler`.
6. **Framing B3 — Creator pattern library.** Expose editable built-in pattern
   sources and allow users to paste/add their own patterns once validation and
   rollback behavior are solid.
7. **Framing B4 — optional render expressions.** Only add a render expression
   language if named render presets become the bottleneck.
8. **Deferred optional: next-event prediction.** If revived, add an
   `ActionDescriptor` list on `ActionCycler`, deep-copy the tree, step
   `advance(false)`, and predict timing only. Payloads stay opaque wherever hidden
   state, conditionals, or multi-effect lambdas are involved.

---

## 7. Implementation status (what is actually built)

**Done.** All eight built-in visuals are now data-driven compiled patterns and the seven
that were hardcoded `*Visual` classes have been **deleted**. `change_visual` compiles a
built-in DSL source (`builtin_patterns.cpp`) for every `Program::VisualType`; the enum is
unchanged, so existing `.session` files keep working.

**Landed and tested** (headless `ctest`, gated behind `-DTRANCE_BUILD_TESTS=ON`):
- `pattern_ast.h` — the normalized IR (schedule + effects + phase/image annotations +
  the scalar-register state ops and the `when` guard).
- `pattern_compiler.{h,cpp}` — lowers AST → `Cycler` tree, reusing the existing classes,
  via an api-free `make_action` seam (`pattern_compiler_test`, `pattern_effects_test`).
- `pattern_parser.{h,cpp}` — DSL text → AST with `line:col` diagnostics
  (`pattern_parser_test`); `pattern_primitives_test` covers `generate` / `burst` /
  `divide`.
- `trance.proto` — `VisualPatternSource` + `Program.custom_visual_pattern = 11`.
- `compiled_visual.{h,cpp}` — `run_effect` maps every effect (draw effects, the scalar
  ops `set/inc/toggle/roll/pulse/copy`, slot-from-register, the `when` guard) to
  `VisualControl` + the register block; the lone genuine FSM is isolated in the native
  `super_fast_tick` effect. `Director` selection prefers the compiled built-in for every
  `visual_type`; custom `.session` patterns join the weighted shuffle.
- `render_preset.{h,cpp}` — a named render preset per built-in, reading image + scalar
  registers and named cycler nodes (`Cycler::index()` is virtual; the DSL has an `id`
  prefix). `Registers` carries `images` + `scalars`.
- `builtin_patterns.{h,cpp}` — the DSL source for all eight; `builtin_patterns_test`
  parses + compiles every one.
- `pattern_render_test` — render-log equivalence. The fake `get_image` returns a
  **slot-tagged** placeholder (primary=1, alternate=2) and the log captures the drawn
  tag, so routing the wrong slot/image is caught — not just "blank == blank". SLOW_FLASH,
  SIMPLE, SUPER_PARALLEL, ANIMATION are proven identical frame-for-frame (anim type
  included). ACCELERATE / SUB_TEXT / FLASH_TEXT were proven identical **ignoring only the
  anim-type enum** (their sole RNG-dependent render output; every float arg + the image
  tag matched), with the anim type Codex-reviewed; their reference classes were then
  deleted. SUPER_FAST (a fully random FSM whose render *structure* varies) was
  Codex-reviewed line-for-line and is smoke-tested to drive its preset.

**Backwards compatibility: total.** The proto field is additive (old `.session` files
have no field 11 → `custom_visual_pattern()` is empty). The `visual_type` enum is
untouched, so a session selecting e.g. `SUPER_FAST` now plays the compiled `super_fast`
built-in. Newer sessions with a custom pattern are ignored cleanly by older builds
(unknown proto field).

## 8. Retiring the hardcoded visuals — DONE

All gates are met and the seven target classes are gone (`AccelerateVisual`,
`SlowFlashVisual`, `SubTextVisual`, `FlashTextVisual`, `SimpleVisual`, `ParallelVisual`,
`SuperFastVisual`). `AnimationVisual` is the **only** remaining hardcoded class: it is the
live reference the render-equivalence harness advances next to the compiled `animation`
built-in. The engine plays the compiled version.

The mechanism that closed the "state-heavy" gap (§4): a small, bounded set of scalar
state effects on top of the schedule/effect model, **not** a general scripting language:
- **`toggle` / `set` / `inc`** — named bool/int registers (e.g. `_alternate`,
  `_sub_speed`).
- **`roll NAME : a b c`** — captured random (e.g. `_animation_mod` ∈ {2,4,8} or {3,5,7},
  `_animated`).
- **`pulse COUNTER every MOD -> FLAG`** — the `divide` counter trick made visible: a
  bounded counter that raises a one-frame flag every MOD-th fire (the `_animation_on`
  every-Nth animation). MOD may be a register (the captured modulus).
- **`copy SRC -> DST`** — image-register hand-off (FLASH_TEXT's `_start = _end`).
- **slot-from-register** (`image reg NAME`, `anim reg NAME`, …) — a toggle used as a
  primary/alternate selector.
- **`when REG [== N | >= N]`** — the language's only conditional, gating a single effect
  on a register (SUB_TEXT's `_sub_speed` cadence, the conditional `change_animation`).
- **`super_fast_tick`** — SUPER_FAST's 4-state FSM, deliberately isolated in one native
  effect rather than leaking an FSM language into the DSL.

Known, accepted divergences (both RNG visuals, documented at the call sites): FLASH_TEXT's
`_animated` is re-rolled on each pattern loop rather than on `Visual::reset()` re-selection
(`CompiledVisual` keeps the no-op reset); SUPER_FAST's `_text_mod` was read uninitialized
in the original and is now defined as 0.
