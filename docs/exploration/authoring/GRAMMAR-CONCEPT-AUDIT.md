> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Grammar concept audit

This is an implementation audit and design assessment, not a claim of GPU/media validation. Evidence: `pattern_parser_v3.cpp`, `pattern_compiler.cpp`, `cyclers.cpp`, `compiled_visual.cpp`, `render_eval.h/.cpp`, `api.cpp`, and `builtin_patterns_v3.cpp`. The specification agrees with much of the implementation; source comments describing visual intent are not observations of actual execution. See [the complete recipe map](BUILTIN-GRAMMAR-MAP.md) for exact eight-source decompositions and the detailed usage matrix.

The language has useful general mechanisms beyond these eight recipes. Its main conceptual burden is that one surface statement can simultaneously select persistent content, change shared animation state, advance hidden counters, and install a per-frame draw. Clock ownership, image storage ownership and hidden-state ownership therefore need separate explanations.

## Complete construct inventory

Recipe shorthand: A accelerate; S slow_flash; T sub_text; F flash_text; P parallel (source name `simple`); M super_parallel; V animation; B super_fast. A dash means absent from all eight authored sources, not unsupported.

| Intent | Surface syntax | Actual operation, state and lifetime | Recipes |
| --- | --- | --- | --- |
| Bound a scene/section | `pattern NAME for LENGTH {…}` | Owns a clock and lexical image-register scope; duration wins over child duration. Children clipped at boundary, restart on re-entry. Register/hidden content survives clock reset. | All |
| Repeat section | `loop N` | Repeats declared pattern iteration N times, not just its image selection. | S, F |
| Arrange sections sequentially | `seq` | Child schedules run in source order, repeat/truncate within parent's duration; direct effects remain entry setup. Default child scheduling is parallel. | S |
| Periodic cuts/lane | `every LENGTH [-> NAME] [offset LENGTH] {…}` | Entry effects once per cadence turn; local clock and child schedules restart. Shares nearest pattern's image scope. Named cadence is a clock, not an image container. | S,T,F,P,M,V,B; named F; offset M,V |
| Accelerate/decelerate cuts | `every ramp Af -> Bf steps N [ease …] [-> NAME] {…}` | Parse-time samples N integer durations normalized to enclosing span. A/B describe shape; not promised first/last durations. Each expanded segment gets a separate clock/register scope; hidden selection policy now follows the authored statement across segments. | A |
| Interrupt baseline with holds | `burst [-> NAME] period Nf [chance 1/N] [cooldown Nf] [duration Af[..Bf]] {…}` | Rolls at eligible period boundaries; rounds duration/cooldown to periods. One active phase. Cooldown begins on exit; no immediate same-boundary re-entry. Range sampled once per entry. No chance disables interruptions. | B |
| Baseline/hold/setup | `base [-> NAME] {…}`, `burst [-> NAME] {…}`, `enter {…}` | Base restarts after hold; burst owns sampled hold clock; enter fires once before hold effects, cannot own timed schedules. Only active branch advances/renders. Base endpoint unknown. | B base/burst; enter absent |
| Select image and draw | `image CONTENT [-> REG] PARAMS [anim…] [chance P]` | Pull on entry to persistent destination (`cur` default), then ordered draw each active frame. Selection miss retains old image. Live selections refresh after theme changes. | All; named destinations F,M,V |
| Reuse/snapshot image | `draw REG PARAMS [anim]`; `copy SOURCE -> DEST` | Draw does no selection. Copy snapshots before subsequent effects; destination stops being an automatically refreshed live pull. Unset registers start empty; read-never-written warns. | F |
| Main text / phrase | `word CONTENT PARAMS [chance P]`; `line …` | Entry selects shared main text path, split word/whole line. Draw each active frame. Word/line are not separate image-like storage. | word A,S,F,M,B; line absent |
| Other text | `caption CONTENT…`; `subtext CONTENT…` | Separate shared caption/subtext paths. Caption selection forces refresh; all support chance and visibility. No text registers/snapshots. | caption S,F,P,V; subtext T |
| Animation selection | `anim CONTENT` | Loads shared animation on entry without image selection or any draw. | — |
| Animation draw request | Image suffix `anim [every Nth]`; draw suffix `anim` | Image suffix also loads animation on every firing. Nth adds persistent per-statement pulse counter and draw gate; draw suffix only requests already loaded animation. Shared slot determines stream for all animated draws. | A,S,T,P,M,V,B |
| Spiral draw/motion | `spiral [speed MOD]` | Draw spiral; speed advances accumulated rotation in elapsed playback reference frames, once per output frame, not per eye/pass. Speed omitted does not set a new speed. | All |
| Spiral appearance | `look {spiral [type=N] [width=N]}` | Entry-time shared spiral settings; not per-frame numeric modulation. | — |
| Image distortion | `warp [amplitude MOD] [wavelength MOD] [speed MOD]`; `drunk MOD` | Per-frame shared wave-sampling driver used by later image draws. Drunk is shorthand for amplitude, wavelength .15, speed 2. Values and phase are stateful, not scoped image textures. | — |
| Theme audio | `audio CONTENT [loop] [volume MOD]`; `audio stop` | Entry plays/replaces one shared audio slot. Loop continues past lexical block; stop explicit. Literal volume sets once, curve/expression drives each active frame, clamps 0..1; omitted volume retains prior gain. | — |

`LENGTH` inventory: positive integer `Nf`, `beats N`, `locked`. All eight use frames; beat/locked forms are unused. Beat period is rounded schedule-fps/pulse-rate, computed from chosen entrainment pulse, not audio-phase synchronization. Comments are `#` to end of line. Names are identifiers; braces delimit scopes; same-line statements are legal. One top-level pattern is product syntax; the authoring draft can retain multiple roots/unknown text without making them valid.

The parser also inserts initial theme cycling, font selection and spiral creation ahead of every authored composition. These are runtime effects, not additional exposed author keywords. A composition with no authored render statements receives the implementation's default image/spiral/text render fallback. Internal effect/AST names must not be advertised as extra supported syntax.

## Content, storage and probability

`primary` and `secondary` select the two live theme sides. `runtime` independently chooses a side when each effect fires. It is not a third theme or a fixed composition setting. `alternate [chance P]`, accepted for image/standalone animation only, maintains hidden statement-local toggle state. With no chance it flips on each firing; the first toggle from zero selects secondary. Probability on alternate controls whether the side flips; selection still happens. At .5, resulting sides are independent fair choices in distribution; describing it as perceptually smoother than independent random choice is unsupported.

In `image alternate chance .25 … chance .5`, the first chance gates flipping, the second gates still selection. They are separate rolls, and a side can flip even when the still selection misses. Ordinary image chance does not suppress rendering or animation side effects. Text chance suppresses both selection and visibility. Ordinary chance uses 100 buckets rounded/clamped to 1..99%; authored 0/1 are not guaranteed never/always. Burst `chance 1/N` is a different boundary interrupt mechanism, permitting guaranteed 1/1.

`-> REG` chooses persistent image storage, not a clock or theme. Bare names resolve to nearest pattern (or expanded ramp segment) image scope. `Ancestor.REG` reads/writes an enclosing named scope; sibling scopes cannot be directly addressed. Ordinary cadence and burst branches share image scope. Two draws of the same register see the latest stored value; merely naming two cadences does not isolate their images. `copy` is the explicit old/current handoff for F's dissolve.

## Every numeric/visibility modifier

| Form | Meaning and interaction | Recipes |
| --- | --- | --- |
| `zoom MOD` | Perspective zoom value; not pixel scale. Very large near-plane values can produce tiled projection artifacts. | All |
| `origin MOD` | Zoom baseline, not x/y placement. API computes effective zoom as `origin + (zoom-origin)*program.zoom_intensity`. Changing only zoom can therefore alter the relation to baseline. | A |
| `alpha MOD`, `brightness MOD` | Same alpha expression; brightness is an opacity alias, not independent luminance. | alpha M; alias unused |
| `fade in/out/inout` | Alpha p / 1-p / triangular `1-abs(2p-1)` over whole local clock. Last alpha-producing clause wins, not multiplies. | fade in F; other forms absent |
| `show A..B`, `show Af..Bf`, `show [EXPR]` | Binary draw visibility, inclusive start/exclusive end; repeated clauses AND. Does not delay selection or pause animation state. Fractions require bounded clock; frame windows can use elapsed indefinite base time. | fractional A only |
| `env in X [hold Y] out Z` | Piecewise alpha rise, full hold, fall, then zero tail. Operands fractions or frames; rise/fall positive, sum fits clock. Frame values require static clock length. Replaces alpha rather than multiplying it. | V only |
| Numeric constant | Fixed value, optionally parenthesized. | All |
| `curve A -> B [ease linear/late/early] [over NAME]` | Linear interpolation; late uses p³, early uses 1-(1-p)³. Reads nearest occurrence unless named enclosing clock specified. Does not change schedule ownership. | All; explicit curve easing absent |
| `[EXPR]` | Live numeric/condition expression; `this`/`self` nearest occurrence, enclosing names explicit. | A numeric only |

Envelope, fade and show serve distinct intent: opacity visit with hold/absence; whole-clock opacity transition; binary presence. A visibility window can preserve a nonzero alpha shape, but multiple alpha/fade/env clauses overwrite one another. On word/line alpha/brightness/fade/env are accepted yet ignored by the render dispatcher. Caption/subtext honor alpha and origin but ignore zoom. These accepted no-ops make a uniform-looking parameter palette misleading. Recommend contextual controls plus diagnostics before any compatibility-breaking parser rejection.

Expressions: conditional `?:`; boolean `or`, `and`; comparisons `== != < > <= >=`; arithmetic `+ - * / % ^`; unary `- !`; grouping `()`; functions `min`, `max`, `abs`; attributes `progress`, `frame`, `length`, `position`, `index`, `active`. Precedence low to high is conditional, or, and, comparison, addition, multiplication, power, unary, primary. Fractions use local frame/N, so an N-frame occurrence displays p=0 through (N-1)/N, never 1. Unknown names/attributes evaluate to zero; expression acceptance is permissive, not a complete static syntax/type guarantee. There are no user variables, assignment, general if/when/roll/set, render blocks, arbitrary beat modulators, or parent-clock mutation.

## Animation's consequential coupling

`image primary anim every 4th` does three things on *each statement firing*: select still; load shared animation; advance its hidden pulse counter. The fourth firing opens the animation draw gate until that statement next fires. The first three draw stills, but animation loads still happened. A trailing image chance only guards the first action. N counts firings, not frames and not successful still selections. `draw REG anim` does neither load nor count. If animation unavailable the draw falls back to that register's still.

Animation is shared rather than register-local: a later load changes what all animated draws use. Also, `image runtime anim` resolves runtime independently for the still and animation effects; they need not choose the same theme. Alternate-based image/animation loads share the toggled selector instead. No promise of independent animation layers should be made without a runtime extension.

The ramp expander reparses each segment. Before repair it minted distinct hidden counters/toggles: actual production CompiledVisual recording found accelerate's first three 2048-frame passes had **zero** animated cuts, while its fourth had **all 140** animated cuts. Normal every-fourth cadence correctly animated two of eight cuts. Deterministic ramp alternate held secondary for the entire first pass and primary for the next; ordinary alternate flipped on each cut. These are observed runtime traces through the recording API, not GPU/media results.

The agreed narrow parser repair keys hidden state by absolute authored operation position and role, retaining generated segment clocks and still-register scopes. `animation_accent_composition_test.cpp` asserts accelerate animates 35 of 140 cuts each pass, ordinary cadence remains correct, normal/ramp alternate both flip per cut, nested ramp copies share policy, separate sibling statements remain independent, separate chance sites do not collide, and a second output pass does not advance state. Animation loads still happen every cut: that separate semantic coupling is intentionally preserved. See the specification's explicit migration note; relying on the old expansion artifact changes behavior.

## Design recommendation and scope

Preserve the small set of user intentions: section/lane timing, choose content, keep old/current images, animate occasional cuts, shape opacity, move/warp, and render ordered layers. Present those intentions directly in the composer; expose raw grammar as the precise advanced representation. Group selection chance, side choice and animation cadence separately rather than scattering identical chance labels across unrelated state machines. Label origin **Zoom baseline**, brightness **Opacity alias**, and animations **Shared animation stream**.

Give an authored media statement a stable hidden-state identity across schedule expansion while retaining segment-local motion clocks. This makes `anim every Nth` and alternate follow consecutive cuts as the recipe comments intend; the narrow repair implements that rule with explicit regression evidence and documented compatibility impact. It does not require inventing a general variable language or rewriting scheduling. Simplifying image animation to load only on animated cuts could reduce side effects, but changes shared-stream behavior and therefore needs a separate deliberate design/migration decision.

Features absent from the eight are not automatically accidental complexity: audio, line text, distortion, fixed bursts and beat-relative lengths express additional intents. More suspicious costs are accepted no-op modifiers, inconsistent hidden-state identity under ramp lowering, probability endpoints that cannot mean never/always, ambiguous brightness/origin names, and animation selection coupled to still-selection syntax. These deserve targeted repair before enlarging the palette. A stronger redesign can place current/outgoing storage and animation cadence under explicit image layers, but must not imply independent animated streams or text snapshots that the runtime lacks.
