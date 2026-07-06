# Trance v2 Intent Grammar — Authoritative Spec

> **⚠ SUPERSEDED by [`spec-grammar-v3.md`](spec-grammar-v3.md).** The v2 grammar and its parser
> have been retired; v3 (a primitive grammar where crossfade/spiral/zoom/fade/warp are composed,
> not baked) is the shipped path. This document is kept for history and for the still-valid
> design rationale it records (the clocks, the bi-thematic decision, the run-time-extension
> discipline). Do not implement against it.

> **Status: spec (historical).** This is the single, self-contained, decisive specification for the
> v2 visual intent grammar. It supersedes the design notes in `roadmap-grammar-v2.md`
> and the two competing drafts. The ground-truth floor it compiles to is
> [`engine-today.md`](engine-today.md); read that first if you have not. Every
> construct here states what counter-tree + effects + render statements it becomes.
> Where a construct cannot lower to today's runtime, it is flagged **REQUIRED RUNTIME
> EXTENSION** and never pretended to be free. Where a construct lowers but loses
> fidelity versus a hand-written v1 pattern, that loss is named in §9.

This spec was written *after* a verification pass that tried to express all 8 original
built-ins and 4 new psychovisual patterns in the grammar and lower each by hand against
the real source. The results changed the grammar: several constructs the earlier drafts
omitted (`subtext`, a `line` text split, an `[expr]` attribute escape, a bare `gate`
timer node, persistent per-loop registers, an explicit slot-flip and a `crossfade`
shape) are **added here** because the originals genuinely need them and they lower
cleanly; the ones that genuinely do not lower (`flash_text`'s crossfade gating reading
minted inner nodes, `super_fast`'s 4-state FSM) are **named as known losses** in §9 and
routed to `raw { }` or a flagged extension. The spec is honest about its limits by
construction.

> **Amended by design review (this session):** the `super_fast` FSM and the proposed
> fourth runtime extension are **dropped** — super_fast is re-authored with randomness
> primitives (§8.8). `pair` divisibility is relaxed to a warning (deliberate polyrhythm is
> allowed). The single-leaf and whole-session open questions are closed (no). A **two-clock
> model (flash vs pattern)** and a **§4.1 mandatory-effects catalog** are added. Driving
> principle, from this review: *the intent defines the full visible behavior in its most
> collapsed form; nothing visual is hardcoded in the compiler.* See the §9 decisions.

> **⚠ AS-BUILT STATUS (this document describes the TARGET grammar; the shipped parser
> implements a subset).** `pattern_parser_v2.cpp` is the source of truth for what parses today.
> Many §3 productions and §8 worked examples use constructs that are **not yet built** — treat
> §8 as design intent, and `builtin_patterns_v2.cpp` as the lowers-today counterpart.
> - **Implemented:** `pattern [repeat N]`; `phase`/`escalate`/`deepen "X" for INTf | for auto`;
>   `description`; `curve NAME from A to B [ease late|linear]` (only `late` weights the dwell);
>   curve→cadence ramp (`every <curve>`, image only) and curve→attribute; `image`/`word`/
>   `caption`/`subtext`; `theme N` (`concept`/`reward` = `theme 0`/`1`; **N≥2 is a hard error** — two-theme engine);
>   `-> NAME` image register; `every INT` (non-divisor = warning); `stagger K`; `chance(p)`
>   (100-bucket); `zoom`/`brightness`/`origin`/`alpha` with `[EXPR]` escape, `fade in|out|inout`,
>   `hold`, and `over section|pattern|flash`; `anim` / `anim every Nth`; `spiral [rate K]`;
>   `every locked`/`spiral locked` **hard-error** (Ext #2 not yet wired).
> - **Designed, NOT built:** `themes{}` header, `pair`, `gate`, `solo`/`idle`, `layer-in`,
>   `flip`, `pick(...)`, `maybe`, `burst`, `every-lap`, `as word|line|once`, `upload`, hand
>   `paint{}`, per-stream attrs on text/caption/subtext, `ease in|out|early`, and a continuous
>   `copy`-handoff crossfade. (A baked `crossfade` keyword was tried and removed — §5.5.)

---

## 1. Purpose & the compile-down contract

The v2 grammar is a friendlier front-end whose only job is to let a non-programmer
hypnosis-pattern author describe **what plays, and how it intensifies over time**, in a
few readable lines, and to **lower that description, with no hidden magic, to the exact
runtime described in `engine-today.md`** — a tree of frame-counter nodes (Action /
OneShot / Parallel / Sequence / Repeat / Offset / Burst) whose leaves fire effects (the
fixed draw ops plus the small scalar-register set `set`/`inc`/`toggle`/`roll`/`pulse`/
`when`/`copy`), feeding a `render { }` block of draw statements over the fixed painter's
palette (alpha, origin, zoom, shadow) and the theme model. `engine-today.md` is the
**floor**: anything the grammar offers must reduce to "a counter tree firing those draw
ops, painted by those statements," or be flagged as a real runtime project. The **hard
invariant** is non-negotiable — for every construct in this document, §6 names exactly
which counter-tree, which effects, and which render statements it becomes.

---

## 2. Object model

Plain-English names a non-expert author would grasp. The word **"voice" is banned**
(it reads as spoken audio for this audience); so are "lane" and "cadence." The model in
one sentence: **a session is an ordered list of phases; inside a phase live curves
(time-drivers) and streams (content), wired together by a few verbs, and painted by an
(almost always auto-generated) render block.**

*Naming note:* this whole language is the **intent** grammar — it captures what the author
*wants*, the layer above the cycler mechanics. "Intent" therefore names the **layer**, never a
field. The per-phase one-line human summary is called **`description`** (it was briefly called
`intent`, which collided with the layer name — fixed).

| Concept | Definition |
|---|---|
| **pattern** | A whole session: a header plus an ordered list of `phase`s, played start to finish. May repeat as a block. |
| **phase** | A named span of time with a duration and a one-sentence `description`. The unit the author thinks in ("the build", "the peak", "settle"). A phase mints a node-id equal to its name; `<phase>.active` / `<phase>.progress` are real reads. May carry the `escalate` or `deepen` qualifier. |
| **curve** | A named number that moves across a phase (`from A to B` with an easing word). The single time-bending primitive: one curve can drive cut spacing, brightness, zoom, spiral rate, or layer count. There is no runtime curve object — a curve is either a compile-time segment unroll (when it drives a beat) or a per-frame progress expression (when it drives an attribute). |
| **stream** | One content channel: `image`, `word`, `caption`, `subtext`, `spiral`, or `anim`-as-subject. Each carries a theme and a beat (`every N`, or `every <curve>`). Streams are visible, not inferred away. A named stream mints a node-id equal to its name. |
| **flash** | One appearance of an image: it is on screen for its beat's N frames, during which it can zoom and fade. A flash has its own 0→100% lifetime (the **flash clock**), distinct from the whole pattern's 0→100% (the **pattern clock**). It is the unit that hard cuts cut between and crossfades dissolve across. |
| **theme** | Which image/word bank a stream pulls from. The engine is **bi-thematic**: exactly two banks are live at once, so the grammar offers exactly two — `concept` (= `theme 0` = `Slot::Primary`) and `reward` (= `theme 1` = `Slot::Alternate`) — plus `runtime` (`Slot::Runtime`, the last-pulled slot). `theme N` for N≥2 is a **hard parse error** (out-of-range index); 3+ simultaneous themes is a deliberate non-goal (§7). |
| **gate** | A named, content-free sub-timer inside a phase whose only purpose is to expose its `.index` / `.frame` to the render block, so the author can say "only during the middle of each cycle." Mints a node-id; fires no draws. This is the surface name for a bare `Repeat`-over-`timer` counter the originals (`simple`, `animation`) depend on. |
| **paint** | The per-frame `render { }` statements — "what is actually drawn." Almost always auto-generated by streams + `escalate`/`pair`/`anim`/`solo`. A hand `paint { }` block is available for override and is the **one** place per-frame conditionals may pool. |
| **verb** | The wiring between streams: `pair` (phase-lock two streams to one beat), `anim` (add an animated draw, three roles), `stagger` (phase-offset a stream). |
| **qualifier** | A phase flavour: `escalate` (all curves rise to a peak — sensory overload) or `deepen` (all curves fall, front-loaded — induction). Pure sugar over `curve` + streams; earns its keyword by making the two headline techniques first-class and symmetric. |
| **escape hatch** | `raw { }` — a verbatim v1 `one { }` + `render { }` subtree, for the one pattern (`super_fast`) with a genuine hand-written FSM that no grammar construct reproduces. |

**The binding rule (normative, stated once):** every named `phase`, `curve`, `stream`,
and `gate` mints a node-id equal to its name; every `anim` / `every-Nth` / captured-`pick`
/ `solo` modifier mints a named scalar register. Auto-generated and hand paint `[expr]`s
may read **only** `<name>.{progress, frame, length, position, index, active}` (the six
real render attributes, confirmed at `render_eval.cpp:44-49`) and bare `<register>`
scalars (and `root` for the pattern root). There is **no** `.beat`, `.lap`, or `.said`
attribute — those intents lower to minted state: `.lap` → the wrapping `Repeat`'s real
`.index`; a beat flag → a `pulse`/`set` register read by name. Curve and attribute
params are **values, never predicates** — the only conditionals in the language live in
a `when` guard (register-vs-literal) or inside a paint block.

**Clocks (normative).** Every time-varying number is anchored to one of three clocks: the
**flash clock** (one image's 0→100% on-screen lifetime — the owning stream node's `.progress`);
the **section clock** (a phase's 0→100% — `<phase>.progress`); or the **pattern clock** (the whole
pattern's 0→100% — `root.progress`). "Zoom in over each flash" rides the flash clock (a sawtooth
that resets each showing); "zoom in across the whole fast section" rides the section clock (one
continuous sweep over the phase); "slowly brighten across the whole pattern" rides the pattern
clock. Same effect, different clock — the single idea behind per-flash, per-section, and
whole-pattern motion (the review's "recursing curves to lower levels"). **Implemented for zoom:**
`zoom Z` rides the flash clock, `zoom Z over section` rides the section clock; `over pattern` is
the natural next extension.

> **ADOPTED (design review): everything is a (sub)pattern.** The analysis confirmed this is
> already true at runtime — `render_eval.cpp`'s `resolve_ident` resolves every node (flash leaf,
> phase, root) through one flat NodeMap + `Cycler::progress()`, and the parser's structural
> defaults already implement the two rules (named time-sections → `Seq` siblings, content →
> `Par` siblings). So this is a naming/spec change with **zero runtime change**. Two honesty
> bounds: `over <ancestor>` beyond `section`/`pattern`/`flash` is new sugar (the generic read
> already works via `[EXPR]`'s `<node>.progress`); and the atomic flash leaf carries an internal
> `_imgN` id, so name-addressing holds for author-named ancestors (phases/curves/`-> NAME`
> streams) but the bare flash is reachable only via the flash-clock default or an explicit
> `-> NAME`. Original note retained below for rationale.
>
> **Refinement detail: everything is a (sub)pattern.**
> The object model has four time-ish nouns — `pattern`, `phase`, `stream`, and the `flash` (one
> image's showing). They may collapse into **one recursive concept: a pattern contains subpatterns**,
> down to the atomic level (a single image shown for N frames is itself just a pattern of one image).
> Worth taking seriously because:
> - **It unifies the three clocks into one mechanism.** "Flash / section / pattern clock" become
>   "the progress of *some enclosing (sub)pattern*"; `zoom Z over section` is then just "anchor to
>   *that* ancestor" — no fixed clock vocabulary at all.
> - **The implementation already works this way.** Clocks are already `<node-id>.progress` for an
>   arbitrary node — `<flash>`, `<phase>`, and `root` are all node ids in the same map. The *runtime*
>   is already a recursive tree (cyclers nest); only the surface vocabulary lags. So this is mostly a
>   naming/structuring change, low implementation risk.
> - **The grammar would mirror the cycler tree** (the compile target) one-to-one — lowering gets even
>   more direct.
>
> **Resolve first:** how a pattern says whether its subpatterns run **in parallel** (today's
> "streams") or **in sequence** (today's "phases") — *without* re-exposing raw `par`/`seq`, the
> low-level wart v2 set out to hide. Likely: keep the defaults (named time-sections sequential,
> content siblings parallel) and let nesting express the rest. Until that is settled this stays a
> direction, not the spec — but implement clock-anchoring generically (`<any-node>.progress`, which
> the parser already does) so adopting it later is free.

---

## 3. Grammar (EBNF-ish)

```ebnf
pattern   ::= "pattern" NAME ("repeat" INT)? "{" header? phase+ "}"
header    ::= "themes" "{" themedecl+ "}"
themedecl ::= ("concept"|"reward"|"runtime"|"theme" (0|1)) "=" STRING ; two themes only (bi-thematic engine); N≥2 is an error

phase     ::= ("phase"|"escalate"|"deepen") STRING "for" duration ("repeat" INT)? "{"
                 "description" STRING
                 curve*
                 (statement | gate)+
                 paint? "}"
duration  ::= INT ("f"|"s") | "rest"                ; f=frames, s→frames (×fps), rest=session − Σ others

curve     ::= "curve" NAME "from" NUM "to" NUM ("ease" EASE)?
EASE      ::= "linear" | "in" | "out" | "late" | "early"
            ; linear=p ; in=p² ; out=1−(1−p)² ; late=p^k front-loaded dwell ; early=1−(1−p)^k back-loaded

statement ::= stream | pair
stream    ::= ("image"|"word"|"caption"|"subtext") ("->" NAME)? theme? slotflip? split? beat? attr* anim? place*
            | "spiral" ("rate" rateval)? ("locked")?
            | "anim" theme? beat? attr*             ; anim-as-subject stream

gate      ::= "gate" NAME "every" INT "of" INT      ; a content-free Repeat(of)-over-timer(every); exposes .index/.frame

theme     ::= "concept" | "reward" | "runtime" | "theme" (0|1)   ; only 0/1 exist; theme N≥2 is a parse error
slotflip  ::= "flip"                                ; alternate the source slot each fire (Effect.slot_reg)
split     ::= "as" ("word"|"line"|"once")           ; SplitType selector for text streams (default: word)
beat      ::= "every" (INT | NAME) ("locked")?      ; INT frames | curve-NAME (ramping) | locked (Ext #2)
attr      ::= ("brightness"|"zoom"|"origin"|"alpha") attrval
attrval   ::= NUM | NAME | "[" EXPR "]"             ; literal | curve-NAME progress | raw render expr (escape)
anim      ::= "anim" ("every" ORD | "every" picksel | "always" | "burst" | "every-lap" | "maybe" | flipref)
flipref   ::= "flip"                                ; always-on anim drawn from the alternating slot
chance    ::= "chance" "(" NUM ")"                  ; per-fire probability (die-roll -> roll+when); random variety without an FSM
picksel   ::= "pick" "(" INT ("," INT)* ")"         ; captured-once modulus (roll)
place     ::= "stagger" INT | "solo" INT ("idle" INT)? | "layer-in" "at" NUM | upload
upload    ::= "upload" ("every" INT ("@" INT)?)?    ; bare flag, or period(@offset)
rateval   ::= NUM | NAME                            ; literal float | curve-NAME (unrolls — §6)
pair      ::= "pair" streamref "with" streamref "every" (INT | NAME) ("locked")?
streamref ::= ("image"|"word"|"caption"|"subtext") ("->" NAME)? theme? split?
crossfade ::= "crossfade" streamref "to" streamref "every" INT   ; copy end→start handoff (§5.5)
paint     ::= "paint" "{" paintline+ "}"            ; rarely written; one place conditionals may pool
```

Notes on what each production is *for* (full lowering in §6):

- **`gate NAME every M of K`** mints the bare counter the earlier drafts omitted —
  `simple`'s middle-half word gate and `animation`'s change/envelope timers need it.
- **`as word|line|once`** restores the SplitType selector. Several originals use
  `text line` (whole-line split) and `text once`; without this they silently became
  `word` splits.
- **`subtext`** is a first-class stream lowering to the `Subtext` draw op — the
  eponymous feature of `sub_text` was unreachable without it.
- **`attr [EXPR]`** is the honest escape: any attribute may take a raw render expression
  (`[0.5 * image.progress]`, `[0.125*root.progress + 0.875*self.progress]`) reading the
  six real attrs of named nodes. The hand-tuned multi-node attribute reads in `simple`,
  `slow_flash`, and `super_parallel` need this; the `NUM`/`NAME` sugar covers the easy
  cases, `[EXPR]` covers the rest without dropping to a full `paint` block.
- **`flip`** (on a stream and as an `anim` role) is the explicit per-fire slot toggle the
  originals do with a `toggle alt` register read as the slot selector.
- **`crossfade A to B every N`** is the start/end handoff shape (`flash_text`). It is the
  one new render shape kept — see §5.5 — because the draft that deleted all named render
  shapes deleted the only construct designed to express it and gave no replacement.
- **`upload every N @K`** restores period/offset control (`simple` fires `upload` at
  frame 16 of every 32; a bare `upload` flag could not place it).
- **`chance(p)`** fires a stream or anim only with probability p (a per-fire die-roll →
  `roll` + `when`). The random-variety primitive that replaces `super_fast`'s FSM (§8.8); with
  different `every N` on separate streams it also lets authors build deliberate **polyrhythms**
  the old grammar could not.

**Deliberately not in the grammar:** named render-shape mini-programs `focus`/`stack`/
`cut` (they smuggle render conditionals into "params"); a separate `section` noun (a
phase *is* a section); top-level `flash`/`lock`/`pulse` keywords (folded into verbs/
qualifiers or flagged extensions); a `solo M cooldown K` shape family (decision held in
§8). `crossfade` is the single named shape that survived, on the strength of `flash_text`.

---

## 4. Render shapes — how "what is drawn" is described and lowered

"What is drawn" is the `render { }` block of `engine-today.md` §6: an ordered list of
draw statements (`image`, `text`, `subtext`, `small_text`, `spiral`), each with an
optional condition and `[expr]` numbers for alpha / origin / zoom / shadow, evaluated
every frame by `render_eval.cpp` against live counters and registers. In this grammar
that block is renamed **paint** for authors and is, in the overwhelming majority of
patterns, **auto-generated** — the author writes streams and qualifiers and never sees
it.

A stream `image concept every 64 zoom focus brightness 0.8` lowers to:

1. a schedule leaf `id image : every 64 : image primary` (the *when*), and
2. a paint statement `image current : zoom [focus-expr] : alpha 0.8` (the *what*).

The mapping is mechanical:

| Author writes | Paint statement emitted |
|---|---|
| `brightness V` / `alpha V` | `: alpha V` (V = literal, or `[A+(B−A)*owner.progress]` for a curve-NAME, or `[EXPR]` verbatim) |
| `zoom V` / `origin V` | `: zoom V` / `: origin V` (same V forms) |
| `image T` stream | `image <reg>` draw statement (reg = `current` or the `-> NAME` register) |
| `word T` stream | `text <split> <slot>` |
| `caption T` | `small_text <slot>` (`render_small_subtext`, `force` defaulted true) |
| `subtext T` | `subtext <slot>` |
| `spiral` | `spiral` (no params; rate set by the schedule effect) |
| `solo M` / `layer-in at T` / `crossfade` | a compiler-generated `when [...]` gate on the statement (the only auto-generated predicates) |

The **one place a per-frame conditional may be authored** is a hand `paint { }` block.
It may read only the six attributes of **named** nodes (phases, named streams, gates,
unrolled curves) plus bare registers — so anything the author wants to gate on must
first be minted by a named construct. This is the load-bearing constraint behind §9's
losses: where an original gates on an inner node the grammar never mints (e.g.
`flash_text`'s `image_repeat`), no hand paint can rescue it.

### 4.1 The mandatory effects (the catalog), by where they act

Every effect lives at one of three places; keeping them straight is most of the model.

- **Frame ops — what happens DURING one flash** (anchored to the flash clock):
  - **zoom** — the built-in zoom action (`origin` → `zoom`): an image grows/shifts across its
    flash. **PRESERVED from today's engine** (`render_image`'s `zoom_origin`/`zoom`); the
    signature motion, not to be lost in a rewrite. **Implemented** (v2 parser): `zoom Z` rides the
    **flash clock** (sawtooth, `Z * <flash>.progress` — resets each showing, the punchy default);
    **`zoom Z over section`** re-anchors to the **section clock** so the zoom sweeps continuously
    across the whole phase (`Z * <phase>.progress`) instead of resetting per flash. Used on
    `slow_flash`'s fast phase: the image zooms once across the entire fast section rather than
    sawtoothing per cut. (Same effect, different clock — §2.)
  - **fade / opacity** — `brightness` (alpha), constant or ramping across the flash.
  - *(future whole-pattern frame ops, on the pattern clock: fluid distort, border blur.)*
- **Border ops — how one flash gives way to the next:**
  - **hard cut** (default) — instant swap; the common case (most originals, and super_fast's
    rapid cutting).
  - **crossfade / dissolve** — smooth handoff. Note (per the review): a dissolve **falls out of
    opacity + overlap** — let the next flash's `brightness` ramp 0→1 while it overlaps the
    previous. `crossfade` (§5.5) is kept only as a convenience name for that combination, not a
    new effect family.
- **Layer ops — several flashes on screen at once:**
  - **layers with per-layer opacity** — N image streams overlaid, each with its own
    `brightness` (the "interleaved hard cuts" of super_parallel / super_fast). Each layer's
    opacity is just that stream's `brightness` attribute (defaulting to the 1, ½, ⅓ ladder by
    declaration order) — so the ladder is **authorable, not hardcoded** (review point #4).

Plus the always-available content ops: **text / subtext / caption**, **spiral** (with a rate),
and **animation** (draw an image as its moving form). This is the complete painter's palette of
`engine-today.md` §2 — nothing outside it can appear, so every intent reduces to this list.

---

## 5. Psychovisual primitives — how the four techniques are authored

These four are the design drivers. Each is first-class and easy, which the original 8
under-exploited.

### 5.1 Associative conditioning — `pair`

Repeatedly landing a stimulus (concept image/word) and an affect (reward image/word) on
the **same beat** to build association.

```
pair image concept with word reward every 48
```

`pair A with B every N` phase-locks the two streams **by construction**: identical period
N, zero relative offset, so they co-fire on the same frame every time. To deepen the bond
over the phase, drive the reward stream's brightness off a curve:

```
curve bond from 0.3 to 1.0 ease in
pair image concept with word reward every 48
image reward brightness bond        ; reward strengthens as conditioning sets in
```

Honest scope: conditioning is **inherently pairwise** — you fuse two banks on a beat
(`concept` = primary, `reward` = alternate), which is exactly the two live themes the
engine holds. Three-or-more *simultaneous* concept-themes is a deliberate non-goal (§7);
chain pairwise phases for transitive associations. Two caveats from this review: (a) pairing **stays locked regardless of phase length** — two
streams sharing a period and a start frame never drift; divisibility (`phase_length % N == 0`)
only affects whether the LAST beat before a phase change is full-length, a cosmetic seam, so the
compiler **warns**, never rejects. Different periods on separate streams are allowed and
encouraged — that is how you author a deliberate **polyrhythm**. (b) `pair` does not enforce that the two streams
are different draw ops — `pair image concept with image reward` parses and clobbers the
shared `current` register unless distinct `-> NAME` registers are used. The intended
image+word and image+image-with-distinct-regs forms are fine; the primitive is just
looser than the intent.

### 5.2 Sensory overload — `escalate`

Escalating multiplicity, speed, brightness, and density across **all** channels at once,
ramping to a peak.

```
escalate "Climb" for 2400f {
  description "Every channel escalates together — faster cuts, brighter, denser text, a second layer joining, wilder spiral — to a peak."
  curve surge from 0 to 1 ease in
  curve pace  from 48 to 6
  image concept every pace brightness surge zoom surge anim every 2nd
  image reward  every pace brightness surge layer-in at 0.4   ; 2nd layer joins past 40%
  word  concept every pace
  caption concept brightness surge
  spiral rate pace
}
```

One driver per axis: `surge` drives brightness/density/spiral intensity; `pace` drives
cut spacing (and, unrolled, the spiral rate). `layer-in at 0.4` is the *growing
multiplicity* that defines overload — a second image leaf gated to appear past 40% of the
phase. Inverting every curve gives `deepen` for free (§5.4); overload and induction are
the same machine run in opposite directions.

### 5.3 Entrainment sync — `locked` (REQUIRED RUNTIME EXTENSION #2)

Visual pulses locked to the audio entrainment frequency so sight and sound drive one
rhythm.

```
image concept every locked          ; flash on the audio beat
pair image concept with word reward every locked
spiral locked
```

The syntax is **reserved and the hook scoped**; it is a **compile-error until the runtime
extension lands** — never a silent hardcoded period. There is zero tempo/beat/audio
symbol anywhere under `src/trance/visual`; the entrainment generator is an independent
integer clock in `src/trance/media` (`entrainment.h`, `Layer::pulse_hz` in Hz on the SFML
audio thread, with its own `pulse_phase`). Two things are needed (see §7): a compile-time
accessor giving the active binaural/isochronic **period in frames** (so `every locked`
can mint a real `Action every <frames>`), and, for true phase coincidence rather than
merely equal average period, a render-readable **`beat.phase`** (0..1) added to
`render_eval.cpp`'s attribute set so a flash can be gated on the audio pulse peak. Equal
periods alone drift in phase because the two clocks are independent. Until #2 lands,
`locked` does not lower.

### 5.4 Induction / deepening — `deepen`

A slow, decelerating, narrowing-focus arc — the sign-flipped twin of `escalate`, for
relaxation onset and progressive deepening. The original 8 never authored this; here it
costs one keyword and one sign.

```
deepen "Settle" for 1800f {
  description "Cuts slow from every 64 to every 96 frames as the image zoom narrows and the spiral winds down."
  curve pace  from 64 to 96 ease out      ; bigger N = slower cuts
  curve focus from 0.6 to 0.15
  image concept every pace zoom focus origin focus
  spiral rate 1.5
  caption concept brightness 0.2
}
```

`deepen` asserts the curves fall and front-loads the easing; it also supplies the
author's description so `pace from 64 to 96` reads unambiguously as "slows down" (the
direction-word problem: `pace` = frames-between-cuts means "bigger = slower," which would
read backwards without the qualifier). Same lowering as `escalate`, opposite direction.

---

## 6. Lowering — every construct → counter-tree + effects + render statements

This is the contract. Every row names the schedule tree, the effects, and the paint it
becomes. No row invents a render read; each mints a real cycler attribute or a real
register.

| Construct | Lowers to |
|---|---|
| **pattern { phase… }** | `OneShot { Sequence { <phase-trees> } }`; a `repeat N` on the pattern wraps the Sequence in `Repeat N`. Each phase opens with an init `Action every 1` firing `themes` + `font` + `spiral_new`, plus captured `roll`s for any `pick(…)` and any persistent loop registers (§6 "persist"). |
| **phase / escalate / deepen "X" for N** | An id'd `Sequence` segment (id = `X`), length forced to N (a trailing `timer` pad is appended if the streams underfill), carrying a `phase "X"` label. `X.active` / `X.progress` become real `resolve_ident` reads. `escalate`/`deepen` add no node — they assert curve direction (rise/fall) and default the easing word. `for rest` = session − Σ(other phases). |
| **curve NAME from A to B ease E** *(driving a beat)* | Unrolls the stream that reads it into a `Sequence` of `Repeat` segments with literal integer `every [L]` lengths sampled A→B; E weights the per-segment dwell counts (`in`→p², `out`→1−(1−p)², `late`→p^k, `early`→1−(1−p)^k, `linear`→p), baked into integer `repeat [count]`. The wrapping Sequence gets `id NAME` so `NAME.progress` reads whole-ramp 0→1. There is no runtime curve object — this is always a compile-time segment unroll. |
| **curve NAME …** *(driving an attribute only)* | Pure paint sugar: the attribute reads `[A + (B−A) * <owner>.progress]` over the owning phase (or owning unrolled Sequence), with E applied as a fixed polynomial on `.progress`. |
| **image/word/caption/subtext T -> R every N** | `id <stream> : every N : <op> <slot> -> <R>`. `<R>` = `current` by default, or a per-stream name-keyed image register via `-> NAME`. `every NAME` → the unrolled ramp Sequence above. `<op>` = Image/Text/SmallSub/Subtext. |
| **as word\|line\|once** | sets the Text effect's `split` (SplitType). Default `word`. |
| **flip** *(on a stream)* | init `set <stream>_alt 0`; each leaf does `toggle <stream>_alt` then the draw reads its slot from `scalars[<stream>_alt]` (`Effect.slot_reg`), so successive fires alternate primary/alternate source. |
| **brightness / zoom / origin / alpha V** | a paint statement param (`brightness`→`alpha`). V = literal `NUM`; or curve-NAME → `[A+(B−A)*owner.progress]`; or `[EXPR]` verbatim (reads the six attrs of named nodes + registers + `root`; `self` resolves to the owning stream's node-id). Maps only to the painter's numeric surfaces. |
| **spiral rate K** *(literal)* | `every 1 : spiral K` — K is a compile-time `float` (`SpiralRot.rate` is `float`; `next_float` at parse). |
| **spiral rate NAME** *(ramped)* | **Unrolled**: a `Sequence` of `every 1 : spiral <literal_k>` effects, one literal rate per ramp segment sampled along the curve (a stairstep, not a continuous expr — the spiral op takes no live params). |
| **spiral locked / every locked** | `Action every <audio-period-frames>` — REQUIRED RUNTIME EXTENSION #2; compile-error until the hook exists. |
| **gate NAME every M of K** | `id NAME : Repeat K { timer M }` — a content-free K·M-frame counter firing no draws. `NAME.index` (0..K−1) and `NAME.frame` (0..M−1) become paint reads (e.g. "middle half" = `NAME.frame >= NAME.length/2`). |
| **anim every 3rd / every pick(…)** | `pulse ctr every N -> <stream>_anim_on` (or `roll <stream>_anim_mod : 2 4 8` in init + `pulse ctr every <mod> -> <stream>_anim_on`) + paint `image <reg> anim if [<stream>_anim_on]`. Verbatim the real `simple`/`accelerate`. |
| **anim always / anim THEME** *(subject)* | the stream's own `image <slot> every N` leaf, rendered `image <reg> anim …` unconditionally — anim-as-subject (the `animation` pattern's intent, minus the co-fire caveat in §9). |
| **anim every-lap** | reads the wrapping `Repeat`'s real `.index` (the `.lap` intent, lowered to a real attr). |
| **anim flip** | always-on anim whose slot is the alternating `<stream>_alt` register (anim drawn from the flipped slot). |
| **anim maybe** | init `roll <stream>_anim_maybe : 0 1` (per pattern loop) + paint `anim if [<stream>_anim_maybe and …]` — a captured 0/1 whole-loop flag (distinct from `every pick`, which is a captured modulus). Restores `flash_text`'s coin-flip animation gate. |
| **anim burst** | a `BurstCycler {length, period, chance_den, cooldown, dur_min, dur_max}`; paint reads `<burstnode>.index` (1 during burst). Lossy vs a true multi-state FSM (§9). |
| **pair A with B every N** | `Parallel { Action(N, A-effects), Action(N, B-effects) }` — equal period, zero relative offset ⇒ co-fire at position 0 (`ParallelCycler` LCM/reset). Compiler **rejects** non-divisible pairs. Pairs two live themes (the engine's max). |
| **crossfade A to B every N** | `id <name> : Repeat 2 { <leaf firing A then B> }` minting an inner `.index`, plus a `copy B -> A` handoff effect and a paint pair `image A if [<name>.index != 0]` / `image B if [<name>.index == 0] : alpha [<name>.progress]`. This is the one shape using the `Copy` effect; see §5.5. |
| **stagger K** | `Offset K`. |
| **solo M idle I** | the stream body becomes `seq { id <stream>_win : every M : <effects> ; timer M ; timer I }` (period = M+M+I), a real dwell window so `<stream>_win.active` distinguishes streams; the solo-alpha paint `when`-predicate (the `1 / 1/2 / 1/3` ladder by declaration order) is **compiler-generated**, never author-written. `idle I` is explicit (default 0); without it, lanes meant to interleave at period `lanes×stagger` will not — see §9 super_parallel. |
| **layer-in at T** | a second image leaf whose paint statement carries `when [<phase>.progress >= T]`. |
| **upload** *(bare)* | `upload` effect present only in the ramp segments the compiler chooses (a monotone threshold is solved to a clean segment edge at compile time; a non-monotone crossing that is not a clean edge is **rejected**). |
| **upload every N @K** | `id _upload : every N @K : upload` — explicit period N and action-frame offset K (Offset/action-frame). Restores `simple`'s `every 32 @16 : upload`. |
| **persist register** *(implicit, via §6.note)* | a number that increments once per pattern loop and survives restarts: init reads/writes a scalar that is **not** reset between loops; `inc <reg>` fires once per loop in the init action. This is how `sub_text`'s `sub_speed` (1,2,3,…) is expressed; gating cadence on it uses `when <reg> == k`. See §9 for the residual loss. |
| **theme N (N≥2)** | **parse error** — only `theme 0`/`theme 1` exist (bi-thematic engine; §7 non-goal). |
| **raw { }** | verbatim v1 `one { }` + `render { }` subtree. The single home of `super_fast_tick`. |

**Discipline restated:** no row invents a render read. Each mints either a real cycler
attribute (`.active`/`.index`/`.progress`/`.frame`/`.length`/`.position`) or a scalar
register written by a schedule effect (`pulse`/`set`/`roll`/`inc`/`toggle`/`copy`) and
read by bare name.

### 5.5 The `crossfade` shape (detail)

`flash_text`'s defining mechanic is a start→end image crossfade: each image period is an
inner `Repeat 2`, the render selects start-vs-end by that inner node's `.index`, fades the
end image in via `alpha [.progress]`, and **hands the previous end to the next start** via
`copy end -> start`. The earlier draft that deleted all named render shapes deleted exactly
this and offered no replacement. `crossfade A to B every N` is the minimal restoration:
it mints the inner `Repeat 2` node (so its `.index` is referenceable), emits the
`Copy` effect (the only construct that does), and generates the two gated paint statements.
It is the one named render shape in the grammar precisely because no combination of
`pair`/`anim`/curve reproduces a `copy`-based handoff.

**Status (revised — composable, no baked keyword).** An earlier pass added a baked
`crossfade` keyword (auto copy→prev + two auto-zoomed layers). It was **removed**: it
hardcoded behavior (and silently auto-injected zoom, double-drawing layers so an image's
zoom reset across its two-flash life). The grammar now favors **composable per-flash
modifiers** instead, and **zoom is purely opt-in** — nothing is auto-injected:
- **`zoom V` / `brightness V`** with a per-flash **fade direction**: `fade in` (`V*.progress`,
  default), `fade out` (`V*(1-.progress)`), `fade inout` (`V*(1 - |2*.progress - 1|)`, a
  0→V→0 triangle).
- `flash_text` is authored as a **per-flash pulse**: `image reward every 64 zoom 1 brightness 1
  fade inout` — the image zooms 0→100% while its brightness fades up then down.

A *continuous* A→B dissolve (no black between beats) still needs a `copy`/`prev` handoff;
that is left as a possible future **composable primitive** (expose `copy` + a `prev` register)
rather than a baked shape — TBD whether it earns the surface area.

---

## 7. Runtime extensions & deliberate non-goals

Two required extensions (numbered **#2** and **#3** — #1 was retired; see the non-goal
below), each scoped as its own piece of work. None is front-end-only; do not pretend
otherwise. Plus one capability we have decided **not** to build, recorded here so it is
not mistaken for deferred work.

**Non-goal — more than two simultaneous live themes.** The engine is bi-thematic *by
design*. `ThemeBank` keeps exactly **two themes live** (primary + alternate) with a third
async-loading and a fourth unloading (`_active_themes[4]`), and every image/text/anim
accessor takes a `bool alternate`, not an index — this is a VRAM-budget decision, not an
oversight. The grammar therefore offers exactly two theme slots: `concept` (= `theme 0` =
primary) and `reward` (= `theme 1` = alternate). **`theme N` for N≥2 is a hard parse
error**, the same as any out-of-range index — not a "coming soon." Associative
conditioning is *inherently pairwise* (fuse A↔B on a beat); three-way simultaneous fusion
is not a meaningful primitive — chain pairwise phases (A↔B then B↔C) instead, which the
phase + theme-rotation model already expresses. "Many themes over a session" is likewise
already covered by the bank's temporal rotation through the load slot. Holding K themes
resident at once would cost K× VRAM and a rewrite of the riskiest threading/loader
subsystem to buy a capability with no authoring need; we are not doing it.

**Extension #2 — entrainment lock (`every locked`, `spiral locked`, `beat.phase`).**
There is no audio/tempo symbol under `src/trance/visual`; the entrainment generator is a
separate integer clock in `src/trance/media` (`entrainment.h`). Needs: (a) a compile-time
accessor exposing the active binaural/isochronic **period in frames**, so a schedule can
mint `Action every <frames>`; and (b), for genuine phase coincidence rather than merely
equal period, a render-readable **`beat.phase`** (0..1) added to `render_eval.cpp`'s
attribute set, so a flash can be gated on the audio pulse peak. Equal periods alone drift
because the audio and visual clocks are independent. Syntax is reserved; using it without
the hook is a compile-error — never a silent hardcode.

**Status (part (a) implemented).** The Director computes the program's beat period in frames
(its highest-amplitude pulsed `EntrainmentLayer`'s `pulse_hz` → `round(global_fps / hz)`) and
threads it into `patternv2::parse`. **`every locked`** now lowers to `Action every <period>` —
a cadence locked to the entrainment beat (a compile-time snapshot of the program's bed) — or
hard-errors "entrainment period unavailable" when the program has no pulsed bed. This is the
"grammar mostly supports it" line the review asked for. **`spiral locked` and `beat.phase`
remain deferred** (part (b)): genuine *phase* coincidence needs a per-frame audio→visual clock
threaded into `render_eval`'s attribute set — the real runtime project, out of scope here.

**Extension #3 — per-segment node addressing for ramps (exact `accelerate`/`escalate`/
`deepen` attribute fidelity).** An unrolled `generate`/ramp Sequence exposes only the
**enclosing** node's `progress()`/`index()` to `resolve_ident`'s six-attr set; the active
sub-segment is an un-id'd generated leaf. So attributes driven off a ramped beat
(`zoom pace`, `spiral rate pace`, brightness surge) read **whole-ramp** progress and drop
the per-image / per-cut micro-wobble that a hand-tuned original reads off the active
segment (e.g. accelerate's `(fast_repeat.index + 8*fast_loop.progress)/48`, slow_flash's
`0.25*slow_main.progress + 0.5*slow_loop.progress`). This already affects today's
`accelerate` (source comment), so it is **not a new regression** — but it means the
grammar genuinely cannot author per-image wobble. Needs either per-segment id'd nodes or a
per-segment length/progress register written by the unroller. Until then, ramped-attribute
reads are whole-ramp-accurate, per-segment-approximate.

**Not an extension, but a blocking predecessor:** an arc model that hides the unrolled
segments makes silent drift *more* likely (this is what produced v1's drift). v2 must not
be built on until **golden per-phase signature tests** exist: a hash of each phase's
unrolled segment lengths (schedule), plus a small set of sampled rendered-frame
perceptual hashes with tolerance (paint). This is the cheapest faithful-enough harness and
is a hard predecessor to the parser, not optional.

---

## 8. Worked examples

The verified versions. Each names its fidelity status; losses are detailed in §9.

### 8.1 Original — `slow_flash` (verdict: lossy-OK)

```
pattern slow_flash repeat 2 {
  deepen "Slow" for 1024f {
    description "Slow concept image flashes every 64 frames; spiral turns gently; caption underneath; big word in the second half."
    image concept every 64 zoom [0.25*Slow.progress + 0.5*slow_image.progress] -> slow_image
    spiral rate 2
    caption concept brightness 0.2
    paint {
      text concept when [Slow.frame >= Slow.length/2]
        : origin [0.5*Slow.progress] : zoom [0.5*Slow.progress]
    }
  }
  phase "Fast" for 512f {
    description "Fast reward flashes every 8; reward words staggered behind on 16; spiral spins quicker."
    image reward every 8
    word reward every 16 stagger 8       ; image + word deliberately NOT phase-locked (matches the real FAST)
    spiral rate 4
    caption reward brightness 0.2
  }
}
```

Fixes applied versus the naive draft: the FAST branch uses **`image … every 8` plus a
separately-staggered `word … every 16 stagger 8`**, *not* `pair` — the real built-in fires
the image every 8 and the word every 16 offset by 8, i.e. **not** phase-locked; `pair`
would have forced a co-timing the original never had. The half-phase big-word gate and its
origin/zoom ramp are written in a `paint { }` block reading `Slow.frame`/`Slow.length`.
The whole-phase + per-64-loop zoom uses the `[EXPR]` escape with a `-> slow_image` id.
**Residual loss (§9):** the odd-lap animation (`slow_repeat.index % 2`) is dropped — the
grammar does not mint that inner Repeat-16 id.

### 8.2 Original — `accelerate` (verdict: lossy-OK, dramatically more readable)

```
pattern accelerate {
  escalate "Ramp" for rest {
    description "Cuts accelerate from every 56 to every 12 frames; spiral, zoom and animation all intensify together."
    curve pace from 56 to 12 ease late          ; late = the (56−L)^6 dwell weight
    image concept every pace zoom pace anim every pick(2,4,8)
    spiral rate pace
    word concept every 8
    upload                                       ; present only while the ramp is above the L>24 band
  }
}
```

45 generated segments + register pokes collapse to one phase + one curve. **Honest
caveats now in the spec (§9):** `zoom pace` and `spiral rate pace` read whole-ramp
`pace.progress` (Ext #3 — matches today's built-in, no new regression); the **slot-flip
per ramp band** (the real keystone: the theme slot alternates per band as L shrinks) and
the **mid-ramp text-mechanic change** (toggle/text_on cadence switch in the final band)
are **not expressible** — there is no production to vary slot or text cadence by ramp
position. Example 8.2 is therefore faithful in feel (accelerating, intensifying) but not
byte-identical; the per-band slot and text-cadence variation are named losses.

### 8.3 Original — `super_parallel` (verdict: clean, with explicit `idle`)

```
pattern super_parallel repeat 12 {
  phase "Interleave" for rest {
    description "Three image streams staggered by 32 frames; whichever is mid-cut shows solo and bright."
    image concept -> a every 16 stagger 0  solo 16 idle 64
      zoom [0.125*root.progress + 0.875*a.progress] origin [0.125*root.progress]
    image concept -> b every 16 stagger 32 solo 16 idle 64
      zoom [0.125*root.progress + 0.875*b.progress] origin [0.125*root.progress]
    image reward  -> c every 16 stagger 64 solo 16 idle 64
      zoom [0.125*root.progress + 0.875*c.progress] origin [0.125*root.progress]
    spiral rate 3.5
    word runtime -> text every 32
    anim concept every-lap
  }
}
```

Fixes versus the naive draft: **`solo 16 idle 64`** makes the lane period
`16+16+64 = 96` (= lanes × stagger = 3 × 32), so the staggers interleave instead of
collapsing mod 32 — the earlier `solo M` left the trailing idle unspecified and produced
period 32. The composite zoom/origin (`0.125*root + 0.875*self`) is written with the
`[EXPR]` escape (the `NUM|NAME` sugar cannot sum two nodes' progress). `word runtime -> text`
is explicitly named so paint can read `text.frame`/`text.length`. `anim every-lap` reads
the wrapping `Repeat`'s `.index`, deleting the `alt_anim` register. **Caveat (§9):** the
solo alpha ladder (`1 / 1/2 / 1/3` by declaration order) is a baked compiler convention,
not author-tunable.

### 8.4 New — associative conditioning (verdict: clean at 2-theme floor)

```
pattern condition {
  themes { concept = "trigger_words"  reward = "pleasure_imagery" }
  phase "Prime" for 256f {
    description "Show the concept alone first, slowly, before any reward."
    image concept every 64
    spiral rate 2
  }
  phase "Fuse" for 5376f {                       ; divisible by 48 (90s would be 5400f — NOT divisible; rejected)
    description "Pair the concept image and reward word on the SAME beat, the bond brightening as it sets in."
    curve bond from 0.3 to 1.0 ease in
    pair image concept with word reward every 48
    image reward brightness bond                 ; forward conditioning: reward fades in across the bond
    spiral rate 2.5
  }
}
```

`pair … every 48` guarantees the concept image and reward word land on the same frame,
repeated to build association — a `Parallel` of two equal-period zero-offset leaves.
The phase length is hand-picked divisible by 48 (the verifier's flagged friction — the
compiler rejects non-divisible pairs so phase-lock cannot silently die). Conditioning is
pairwise; a third theme hard-errors (the engine holds two — §7).

### 8.5 New — sensory overload (verdict: clean; ramp length padded, per-cut wobble = Ext #3)

```
pattern flood {
  escalate "Climb" for 2400f {
    description "Every channel escalates together — faster cuts, brighter, denser text, a second layer joining, wilder spiral — to a peak."
    curve surge from 0 to 1 ease in
    curve pace  from 48 to 6
    image concept every pace brightness surge zoom surge anim every 2nd
    image reward  every pace brightness surge layer-in at 0.4
    word  concept every pace
    caption concept brightness surge
    spiral rate pace
  }
  phase "Peak" for 480f {
    description "Hold at maximum — everything fast, bright, doubled, animating always."
    image concept every 6 brightness 1.0 anim always
    image reward  every 6 brightness 1.0
    spiral rate 6
  }
}
```

`surge` drives brightness/density/spiral, `pace` drives cut spacing, `layer-in at 0.4`
is the growing multiplicity. **Caveat (§9):** `for 2400f` on an escalate phase is
*approximate-or-padded* — a `generate` ramp's natural length is the dwell-weighted sum of
its segments and does not equal an arbitrary budget; the compiler either solves the dwell
weights or appends a trailing `timer` pad (which freezes the final cadence/spiral for the
remainder rather than continuing to climb). Per-cut zoom/alpha wobble is whole-ramp (Ext
#3).

### 8.6 New — entrainment sync (verdict: does NOT lower — Extension #2)

```
pattern entrain {
  phase "Lock" for 1800f {
    description "Flash the concept image and reward word together on the audio entrainment beat."
    pair image concept with word reward every locked
    spiral locked
  }
}
```

`locked` is reserved syntax. This **compile-errors today** ("entrainment period
unavailable") and only lowers once Extension #2 exposes the audio period in frames (and,
for true coincidence, `beat.phase`). No silent fallback period is ever substituted. This
example is included to show the intended surface, not a buildable pattern.

### 8.7 New — induction / deepening (verdict: clean; per-cut wobble = Ext #3)

```
pattern gentle_induction {
  deepen "Settle" for 1800f {
    description "Cuts slow from every 64 to every 96 frames as the image zoom narrows and the spiral winds down."
    curve pace  from 64 to 96 ease out
    curve focus from 0.6 to 0.15
    image concept every pace zoom focus origin focus
    spiral rate 1.5
    caption concept brightness 0.2
  }
}
```

The sign-flipped twin of 8.5: same machine, falling curves, front-loaded easing. `deepen`
supplies the description so `pace from 64 to 96` reads as "slows down." `zoom focus`
reads whole-arc progress (Ext #3); the spiral deceleration is a ~33-step stairstep of
literal rates (unrolled, not a continuous curve). Faithful to how `accelerate` already
works; no new regression.

### 8.8 Original — `super_fast` (verdict: re-authored, FSM dropped — same effect, not same frames)

```
pattern super_fast {
  phase "Blitz" for 2048f {
    description "Rapid image cuts every 8 frames; now and then one animates, a preview slides in, or a word flashes."
    image concept every 8 flip                 ; fast hard cuts, alternating themes
    anim concept maybe                          ; some cuts animate (captured coin)
    image reward -> next every 8 @4 stagger 4   ; a 'next' image previewing in near each cut's end
      zoom [0.125 * next.progress]
    word concept every 8 chance(0.25)           ; a word pops ~every 4th cut
    spiral rate 3
  }
}
```

The old 4-state `super_fast_tick` FSM is gone. Each behavior it produced maps to a randomness
primitive: animation bursts → `anim maybe` / `anim burst`; the next-image preview → a staggered
second stream; occasional text → `chance(0.25)`; the theme toggle → `flip`. This lowers entirely
to today's runtime (fast `Action`s + `roll`/`when` for the dice, `Offset` for the preview) with
**no new runtime capability and no FSM**. It is not the identical frame sequence; at ~133ms cuts
it is indistinguishable, and it is readable and tunable where the FSM was neither.

---

## 9. Open questions & known fidelity losses

**Honest about limits.** The grammar is superior to the original 8 for conditioning,
overload, and induction, but it does not reproduce every original byte-for-byte, and two
originals do not lower in grammar-native form at all.

### Originals that lower with named losses

- **`slow_flash`** — odd-lap animation (`slow_repeat.index % 2`) dropped (inner Repeat-16
  id never minted). Whole-phase + per-loop zoom recovered via `[EXPR]`; per-loop wobble
  beyond that is Ext #3.
- **`accelerate`** — **per-band slot flip** (slot alternates per ramp band as L shrinks)
  and **mid-ramp text-mechanic change** (toggle/text_on cadence switch in the final band)
  are not expressible; there is no production to vary slot or text cadence by ramp
  position. Per-image attribute wobble is Ext #3 (= today's loss). Faithful in feel, not
  byte-identical.
- **`super_parallel`** — the solo alpha ladder (`1 / 1/2 / 1/3` by declaration order) and
  its asymmetry (lane A always alpha 1) are baked compiler conventions, not author-tunable
  without dropping to `paint { }`. Lane period now authorable via explicit `idle`.
- **`simple`** — the bare middle-half word gate is now expressible via `gate counter every
  32 of 4` + paint `text when [counter.index == 1 or counter.index == 2]`; `text … as line`
  restores the line split; `[0.5*image.progress]` restores the attribute expr; `upload
  every 32 @16` restores the placement. Lowers fully with these additions.
- **`sub_text`** — `subtext` stream restores the eponymous op; `persist` registers restore
  the cross-loop `sub_speed` ramp (1,2,3,…) and `when sub_speed == k` cadence selection;
  `flip` restores the per-fire slot toggle; `as word/once` restores the split-type. The
  one residual loss: `sub_text`'s **second unconditional `anim` draw** (animation drawn
  even when the pulse flag is off, via the alt slot) is an extra draw beyond a single
  `anim` modifier — recover it with an explicit `anim flip` stream alongside.
- **`animation`** — `gate change every 32 of 2` and a session-scope envelope gate restore
  the timer nodes; **but** the real `start_end_timer` sits *outside* `repeat 8`, spanning
  the whole run once, while a phase's `repeat 8` wraps everything in it. A pattern-level
  timer **sibling outside the phase/repeat** is the residual gap; and **text+anim co-firing
  on one leaf** (`every 64 : text line primary, anim primary`) is not expressible —
  `pair` co-fires two *streams* as two Parallel leaves, not one leaf with two effects. A
  single-leaf multi-draw construct is an open question.

### Originals that do NOT lower in grammar-native form

- **`flash_text`** — the start/end **crossfade** is now restorable via the `crossfade`
  shape (§5.5), which mints the inner `Repeat 2` id, emits `copy`, and generates the
  gated paint. The captured-boolean animation gate is `anim maybe`; the shared slot-toggle
  read three ways is `flip` + `anim flip`. With these additions it lowers — **but** this
  required adding a named render shape (`crossfade`) and a `copy`-emitting construct that
  the minimalist draft had deliberately deleted; treat `crossfade` as a deliberate,
  bounded re-addition, and the per-frame `image_repeat.index`/`subtext_counter.index`
  gates as only reachable *because* those nodes are now minted by `crossfade`/`gate`. If
  `crossfade` is rejected in review, `flash_text` returns to `raw { }`.
- **`super_fast`** — **re-authored approximately; the FSM is dropped (design decision, this
  review).** We want the *effect*, not byte-identical frames. super_fast's effect is "rapid
  image cuts (~every 8 frames) with occasional random variety — an animation, a sliding
  preview, a blank, a word." That is fully expressible with **fast cuts + the randomness
  primitives** (`chance(p)`, `anim burst`, `anim maybe`, `flip`) — see the worked example in
  §8.8 — and lowers entirely to today's runtime (fast `Action`s + `roll`/`when` for the dice,
  `Offset` for the preview) with **no new runtime capability and no FSM**. At 8-frame (~133ms)
  cuts no viewer can distinguish well-tuned randomness from the old 4-state machine, and the
  grammar version is readable and tunable where the FSM was neither. The native
  `super_fast_tick` and the previously-proposed *fourth runtime extension* (a programmable
  state-machine cycler) are therefore **both removed from scope** — not deferred, dropped.
  (`raw { }` remains available if some future one-off ever truly needs a hand-written FSM, but
  no built-in does.)

### Design decisions (resolved this review)

1. **Single-leaf multi-draw — NO.** Two zero-offset streams that line up are good enough; a
   word landing on the same-theme vs a different-theme image doesn't matter at flash rates
   (~300ms/flash). Not adding a same-leaf combinator. (A genuinely different technique — the
   hypnotic *pattern-interrupt → association window*, where a break in rhythm opens a moment
   for suggestion — is noted as a candidate future psychovisual primitive; it is **not** this.)
2. **Whole-session timer — NO.** The playlist architecture owns session-level concerns; v2
   patterns stay pattern-scoped. `animation`'s session envelope is simply not reproduced.
3. **`crossfade` — kept, but reframed.** A dissolve falls out of *opacity + overlap* (§4.1),
   so `crossfade` is a convenience name, not a new effect family. The mandatory-effects catalog
   is now explicit in §4.1.
4. **Layer opacity — in the grammar.** Per the principle *"the intent defines the full visible
   behavior in its most collapsed form"*: each layer's opacity is its own `brightness`
   attribute (defaulting to the 1, ½, ⅓ ladder), never hardcoded.
5. **`pair` divisibility — warn, don't reject** (§5.1). Pairing stays locked regardless of
   phase length; divisibility is only a cosmetic seam. Different periods on separate streams
   are *allowed* — deliberate polyrhythm is a feature, not an error.
6. **Fourth extension (FSM cycler) — dropped, not built.** super_fast is re-authored with
   randomness primitives (§8.8). Same effect, not same frames — the project's stated priority.

The one genuinely-open item: per-segment ramp wobble (Extension #3) — exactness that already
does not exist in today's `accelerate`, so low priority.

---

*Source of truth re-verified this session: `src/trance/visual/render_eval.cpp:23-51`
(the six render attrs `{progress,frame,length,position,index,active}`, node-id
resolution, `root`); `src/trance/visual/pattern_ast.h:20` (`Slot`), `:32-69` (`Effect`
kinds + the `when` guard comparing a scalar register to a literal — the only conditional
in the schedule), `:53` (`SpiralRot.rate` is `float`). Conceptual floor:
[`engine-today.md`](engine-today.md). Prior design notes: [`roadmap-grammar-v2.md`](roadmap-grammar-v2.md).*
