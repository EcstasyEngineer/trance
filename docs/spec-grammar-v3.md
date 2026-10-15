# V3 pattern language reference

V3 is the language used by built-in visuals and custom `.pattern` files. This is
an implementation reference, not a proposal. Start with the
[authoring guide](authoring-v3-patterns.md) for examples. Implementation:
[parser](../src/trance/visual/pattern_parser_v3.cpp),
[scheduler](../src/trance/visual/cyclers.cpp),
[expression evaluator](../src/trance/visual/render_eval.h).

## Clocks and scope

A timed occurrence owns its schedule. Children advance while their owner runs;
when an occurrence starts again, its child schedules start again too. Reading an
ancestor's clock does not change that ownership.

| State | Scope and lifetime |
| --- | --- |
| Schedule position | Local to a pattern, cadence, or burst phase; reset on re-entry |
| Image registers | Names scoped to a pattern; contents persist until replaced or copied |
| Hidden selection state | Per compiled visual: alternation, chance results, animation counters |

Resetting a schedule does not erase image registers or reset hidden selection
state. `anim every Nth` counts selections, not frames.

Render values use the nearest timed occurrence's clock unless explicitly redirected:

```text
pattern scene for 512f {
  every 16f {
    image primary zoom (curve 0 -> 0.4) alpha (curve 0 -> 1 over scene)
  }
}
```

Zoom restarts every 16 frames; alpha follows the 512-frame scene. `over NAME`
resolves a named enclosing pattern, cadence, or branch, searching outward from
the current scope. Raw expressions can read `NAME.progress`. These are read-only
references: they cannot reset, seek, or change the parent.

Frames are half-open. After the first advance, `frame` is 0; the last displayed
frame of an N-frame occurrence is N-1. Progress is `frame / N`, approaching but
not reaching 1. An unstarted clock is safe to inspect, but its value is not a
displayed sample. Activation gates determine visibility.

## Patterns and cadences

```text
pattern NAME for LENGTH [seq] [loop N] { ... }
every LENGTH [-> NAME] [offset LENGTH] { ... }
every ramp Af -> Bf steps N [ease linear|late|early] [-> NAME] { ... }
```

- `for LENGTH` is the duration of one pattern iteration. `loop N` repeats it.
- Children run together unless the pattern says `seq`; sequenced child schedules
  run in source order. Direct effects are entry setup, not extra sequence time.
  Child schedules repeat while their owner keeps advancing; a shorter sequence
  starts again, while a longer sequence is truncated at the owner's boundary.
  A named `seq` pattern's `index` is its active child's zero-based index.
- `every LENGTH` repeats its body. A final occurrence can be truncated by its
  owner. Nested schedules restart at each occurrence boundary.
- `offset` shifts the cadence within its period. It pre-rolls counters with
  actions suppressed: a phase shift, not a general delayed-start/media-preload operation.
- A ramp samples N positive integer segment lengths at parse time and scales
  them to fill the enclosing span. A and B set the relative shape, not exact
  endpoint durations. N must be at least 2 and cannot exceed the span.
  Each segment has a local clock and register scope. Optional segment names are
  `NAME_00`, `NAME_01`, etc.; there is no live variable-length cadence.

`LENGTH` is `Nf`, `beats N`, or `locked`. Pattern/cadence lengths are positive.
`locked` is one beat period; `beats N` multiplies it. The Director computes the
period once as `round(program.global_fps / pulse_hz)`, using the highest-amplitude
pulsed entrainment layer (later layer wins ties). If the period resolves to zero,
`beats` and `locked` are rejected.

This matches a nominal rate, not the audio oscillator's phase. At 60 schedule
frames/s, 8 Hz rounds to an 8-frame period: 7.5 visual events/s, not exact 8 Hz
synchronization. There is no audio-thread clock reference.

Older custom patterns could accidentally rely on child sums/least-common-multiples
overriding `for`, or nested cadences running beside their owners. The declared
duration now wins. Adjust `for` to the intended length; put deliberately independent
lanes beside each other rather than nesting one inside another.

## Burst phases

```text
pattern scene for 2048f {
  burst -> controller period 8f chance 1/12 cooldown 64f duration 64f..128f {
    base { every 8f { image runtime zoom (curve 0.0625 -> 0.1875) } }
    burst -> held { image runtime zoom (curve 0.1 -> 0.7) anim }
  }
}
```

`period` is required. Other controller options are optional and may appear in
any order. No `chance` means interruption is disabled. No duration means one
period. `chance 1/N` rolls at eligible period boundaries, including the initial
boundary. Durations and cooldowns are authored in frames and rounded up to whole
periods. A range samples an inclusive integer number of periods once per entry.
Rounded durations must fit a 32-bit frame count.

The controller has at most one each of `base`, `enter`, and `burst`:

- `base` runs until interrupted and restarts on return from a burst.
- `enter` is one-time burst setup, executed before direct burst effects. It shares
  the burst clock and cannot contain timed child schedules.
- `burst` runs for its sampled duration, or until an enclosing owner ends.
  Direct effects fire once per entry. Use `every Nf` to repeat effects.
- Only the active branch advances children and renders them. Child schedules
  restart on re-entry; unfinished children are cut off at the branch boundary.
- Cooldown starts on burst exit. Even at zero cooldown, the exit boundary cannot
  immediately re-enter a burst; the next eligible boundary is one period later.

The outer `-> controller` names the controller span. Inner `burst -> held` names
the sampled local phase. These are different clocks. A bare burst curve follows
the sampled duration, so held media moves throughout its hold. A curve inside
`every 8f` instead follows that cut unless it says `over held`.

A base has elapsed time but no knowable interruption time. Normalization against
its `progress` or `length` is rejected. Use a timed child or read a bounded
ancestor. Frame windows such as `show 0f..8f` can use elapsed base time.
Fractional envelopes work in sampled bursts; frame-valued `env` and `every ramp`
need a statically known duration: use a fixed-duration burst or timed child pattern.

A nested controller inside an indefinite base or sampled phase also lacks a
fixed enclosing span. Its elapsed `frame` and state `index` remain available,
but normalized controller `progress`/`length` reads are rejected. Read the named
sampled branch (`over held`) or a bounded ancestor instead of its controller's
static maximum.

**Migration from early v3:** direct branch effects used to repeat on the controller
period and nested schedules could run outside their branch. They now belong to
the phase. Add an explicit `every` wherever repetition is intended.

## Rendering and content

Effects change state at schedule entry; render statements read it each frame.
`image` selects on entry and draws the selected register during the active
occurrence. `draw` only draws an existing register. Render statements retain
source order: later image layers composite over earlier ones.

| Statement | Behavior |
| --- | --- |
| `image CONTENT [-> REG] PARAMS [anim [every Nth]] [chance P]` | Select image (default register `cur`) and draw |
| `draw REG PARAMS [anim]` | Draw existing image; no selection |
| `copy SOURCE -> DEST` | Snapshot image register before subsequent effects |
| `word CONTENT PARAMS [chance P]` | Select/render word |
| `line CONTENT PARAMS [chance P]` | Select/render complete phrase |
| `caption CONTENT PARAMS [chance P]` | Select/render small caption |
| `subtext CONTENT PARAMS [chance P]` | Select/render subtext |
| `anim CONTENT` | Select animation without drawing or pulling image |
| `spiral [speed MOD]` | Draw spiral, optionally driving rotation speed |
| `look { spiral [type=N] [width=N] }` | Set spiral shape/width on entry |
| `warp [amplitude MOD] [wavelength MOD] [speed MOD]` | Set image-sampling wave displacement |
| `drunk MOD` | Warp amplitude MOD, wavelength 0.15, speed 2 |
| `audio CONTENT [loop] [volume MOD]` | Play file from theme audio pool |
| `audio stop` | Stop grammar-driven theme audio |

`CONTENT` is `primary`, `secondary`, or `runtime` (choose a side at effect firing).
There are two live theme sides. `alternate [chance P]` is also accepted by `image`
and standalone `anim`: it keeps statement-local toggle state. Without chance it
flips each selection; with chance it flips probabilistically while still selecting
every time. It does not mean `secondary`.

Ordinary image `chance` skips selection on a miss; the existing register can still
draw. Text chance also gates visibility. Chance is quantized to 100 buckets and
clamped to 1..99 percent; 0 and 1 do not mean guaranteed exclusion/inclusion. Omit
chance for an unconditional effect. Burst's `chance 1/N` is a separate mechanism.

Registers use the nearest pattern scope. `Scene.cur` addresses an enclosing
pattern's register; sibling scopes are not directly addressable. Cadences and
burst branches share their enclosing register scope; ramp segments currently
create their own. Reading a register never written anywhere in scope warns.
Registers start empty. `copy` makes a snapshot; later automatic theme refreshes
update live selections but preserve snapshots.

Animation selection is shared: the last animation load determines which theme's
streamer `anim` draws use. This is not a bank of animation registers. If animation
is unavailable, the renderer falls back to that draw's still with the same
zoom/origin/alpha values.

Main text, subtext, and captions have separate shared paths, not copyable registers.
Image-style text crossfades are unsupported. `word`/`line` rendering uses origin/zoom;
their parsed alpha/fade/env values are not applied by that path. Subtext/captions
use alpha/origin, not zoom. `show` gates visibility on any draw.

Theme audio has one dedicated playback slot; another `audio` replaces it. `loop`
continues until replacement or `audio stop`, including after its lexical block
ends. Volume is 0..1: a literal applies on entry; a curve/expression applies during
rendering. See [audio.md](audio.md).

## Expressions

Draw parameters are `zoom MOD`, `origin MOD`, `alpha MOD` (alias `brightness`),
`fade in|out|inout`, `show WINDOW`, and `env in X [hold Y] out Z`.

A modulator is a constant, `curve A -> B [ease linear|late|early] [over NAME]`, or
`[EXPR]`, optionally surrounded by parentheses. `over` belongs to curves only;
in a raw expression name the ancestor directly.

| Form | Meaning |
| --- | --- |
| `curve A -> B` | `A + (B-A)*p` |
| `ease late` / `ease early` | Replace p with p cubed / `1-(1-p)^3` |
| `fade in` / `out` / `inout` | Alpha p / 1-p / `1-abs(2*p-1)` |
| `show A..B` / `show Af..Bf` | Fractional / frame window, A inclusive, B exclusive |
| `show [EXPR]` | Visible when expression is nonzero |
| `env in X hold Y out Z` | Alpha rise, hold, fall, then zero for remaining tail |

`show` cannot mix frame and fractional endpoints; a known clock bounds its window.
Multiple `show` clauses combine with AND. `env` accepts frames or fractions per
operand; rise/fall must be positive and the total must fit the clock. Later
alpha/fade/env parameters replace earlier alpha expressions.

Expressions support `?:`, `or`, `and`, comparisons, `+ - * / % ^`, unary `- !`,
and `min`, `max`, `abs`. `this` and `self` name the nearest clock. Attributes are
`progress`, `frame`, `length`, `position`, `index`, and `active`. Precedence from
low to high: conditional, or, and, comparison, addition, multiplication, power,
unary, primary.

Evaluation is permissive: unknown identifiers/attributes resolve to zero, and
expression parsing is not a complete static type/name check. Prefer named curves
and check raw expressions with lint and playback. Lint success is not proof of
schedule correctness or visible motion.

## Grammar

This compact grammar describes author syntax; the semantic constraints above
still apply. `NAME` is an identifier, `REG` is `NAME` or `NAME.NAME`, `UINT` an
unsigned integer, and `NUMBER` a numeric literal. Files use `#` line comments.

```ebnf
pattern      = "pattern" NAME "for" length { "seq" | "loop" UINT } "{" body "}" ;
body         = { pattern | cadence | burst | effect } ;
length       = UINT "f" | "beats" UINT | "locked" ;
cadence      = "every" length [ "->" NAME ] [ "offset" length ] "{" body "}"
             | "every ramp" NUMBER "f" "->" NUMBER "f" "steps" UINT
               [ "ease" ease ] [ "->" NAME ] "{" body "}" ;
burst        = "burst" [ "->" NAME ] burst_option { burst_option }
               "{" { branch } "}" ;
burst_option = "period" UINT "f" | "chance 1/" UINT | "cooldown" UINT "f"
             | "duration" UINT "f" [ ".." UINT "f" ] ;
branch       = ( "base" | "burst" ) [ "->" NAME ] "{" body "}"
             | "enter" "{" { effect } "}" ;
content      = "primary" | "secondary" | "runtime" ;
image_content = content | "alternate" [ chance ] ;
chance       = "chance" ( NUMBER | "(" NUMBER ")" ) ;
effect       = "image" image_content [ "->" REG ] { param }
               [ "anim" [ "every" UINT [ "st" | "nd" | "rd" | "th" ] ] ] [ chance ]
             | ( "word" | "line" | "caption" | "subtext" ) content { param } [ chance ]
             | "draw" REG { param } [ "anim" ] | "copy" REG "->" REG
             | "anim" image_content | "spiral" [ "speed" mod ]
             | "look" "{" { "spiral" { ( "type" | "width" ) "=" UINT } } "}"
             | "warp" { ( "amplitude" | "wavelength" | "speed" ) mod } | "drunk" mod
             | "audio" ( "stop" | content [ "loop" ] [ "volume" mod ] ) ;
param        = ( "zoom" | "origin" | "alpha" | "brightness" ) mod
             | "fade" ( "in" | "out" | "inout" )
             | "show" ( NUMBER ".." NUMBER | NUMBER "f" ".." NUMBER "f" | "[" EXPR "]" )
             | "env in" env_len [ "hold" env_len ] "out" env_len ;
env_len      = NUMBER [ "f" ] ;
mod          = value | "(" value ")" ;
value        = NUMBER | "[" EXPR "]"
             | "curve" NUMBER "->" NUMBER [ "ease" ease ] [ "over" NAME ] ;
ease         = "linear" | "late" | "early" ;
```

## Limits

No user `set`, `inc`, `roll`, or `when` statement; no explicit `render` block;
no `beat` modulator, general scheduling expression, or parent-clock mutation.
Internal AST/effect names are not extra DSL syntax. More than two simultaneous
themes, text registers, shared named alternation, and sample-accurate audio
synchronization are not implemented.

`--lint` parses, compiles, and samples expressions. Phase execution tests exercise
actual advances, entry effects, and clock-derived render values. Neither validates
decoded media, GPU output, subjective pacing, or all possible programs. See
[validation coverage](architecture-maturity.md).
