> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# What the eight built-ins actually author

Source of truth: [builtin_patterns_v3.cpp](../../../src/trance/visual/builtin_patterns_v3.cpp), `builtin::pattern_source_v3`. Semantics checked against [spec-grammar-v3.md](../../spec-grammar-v3.md) and `pattern_parser_v3.cpp`. This is a source/implementation interpretation, not a fresh playback test. “Intent” below describes the authored visual recipe; the runtime still depends on available theme media.

The pre-fix ramp mismatches below are now repaired and covered by the production recorder: Accelerate animates 35 of 140 cuts on every tested scene, and deterministic ramp alternation flips on each cut. That repair passed the complete Release build and then-current CTest suite. See [the debate and decisions](INTENT-FORUM.md).

## Usage matrix

Numbers are built-in IDs: 1 Accelerate, 2 Slow Flash, 3 Sub Text, 4 Flash Text, 5 Simple, 6 Super Parallel, 7 Animation, 8 Super Fast. A dash means absent from all eight, not unsupported by the language.

| Authored construct/modifier | Built-ins | What it supplies |
|---|---|---|
| `pattern NAME for Nf` | 1–8 | Bounded scene/phase and named clock; all authored lengths are literal frames. |
| Nested `pattern` | 2, 4, 6, 7 | Sequential slow/fast sections or independent concurrent image lanes. |
| `seq` | 2 | Slow then fast rather than simultaneous phases. |
| `loop N` | 2 | Repeat the slow/fast section pair twice. |
| Fixed `every Nf` | 2–8 | Recurring media/text selection and a fresh local motion clock. |
| `every ramp Af -> Bf steps N ease early` | 1 | Progressively tightening cuts, sampled/scaled across the scene. |
| Cadence `offset Nf` | 4, 6, 7 | Stagger image lanes; phase the visiting still. |
| Schedule `-> NAME` | 1 (`cut`), 8 (`rapid`) | Optional clock labels; neither name is read elsewhere in these built-ins. |
| Controller `burst`, `base`, inner `burst` | 8 | Exclusive rapid-cut and held-image phases. |
| `period`, `chance 1/N`, `cooldown`, `duration Af..Bf` | 8 | Burst eligibility, random entry, recovery time and sampled hold length. |
| `image primary` | 2, 6 | Explicit primary-side imagery. |
| `image secondary` | 2, 4, 6, 7 | Explicit secondary-side imagery. |
| `image runtime` | 3, 5, 7, 8 | Resolve a theme side when selection fires. |
| `image alternate chance P` | 1 | Stateful probabilistic side switching, followed by selection every cut. |
| Authored image destinations, `copy`, `draw` | Removed | Historical implementation machinery, replaced by independent nested image lanes in 4, 6 and 7. |
| `zoom (curve A -> B)` | 2–8 | Motion across the nearest local timed occurrence. |
| `origin (curve A -> B over NAME)` | 1 | Whole-scene projection origin movement. |
| Parameter `curve ... over NAME` | 1, 2 | Read scene/phase progress while cuts keep their own timing. |
| Raw `[EXPR]` | 1 | Add scene approach and local cut punch in one zoom value. |
| Literal `alpha` | 6 | Source-over transparency of the second and third image layers. |
| `fade inout` | 4 | Upper image lane rises and falls over its full 128f lifetime above an opaque lower lane. |
| `env in Nf hold Nf out Nf` | 7 | A still fades in, holds, fades out, then disappears for the remaining cycle. |
| Fractional `show A..B` | 1 | Word visible only during the first quarter of each cut. |
| Trailing text `chance P` | 1, 8 | Probabilistic text selection **and visibility**. |
| Image suffix `anim` | 7, 8 | Draw animated content throughout an occurrence, with still fallback. |
| Image suffix `anim every Nth` | 1, 2, 3, 5, 6 | Make every Nth selection a motion accent; this counts selections, not frames. |
| `word primary` / `secondary` / `runtime` | 1, 8 / 2, 4 / 6 | Main word stream, refreshed at the surrounding cadence. |
| `caption primary` / `runtime` | 2, 4, 5 / 7 | Small caption stream. |
| `subtext secondary` | 3 | Secondary subtext stream. |
| `spiral speed N` | 2–8 | A scene-wide spiral layer with constant rotation speed. |
| `spiral speed (curve ... over NAME)` | 1 | Increase spiral rotation with the whole-scene acceleration arc. |
| `brightness` alias; literal `zoom`/`origin`; parameter `ease early`/`late` | — | Not authored in these eight. Ramp `ease early` is a different use of easing. |
| Ordinary image selection `chance` after parameters/animation | — | No built-in probabilistically skips image selection. Alternate-toggle chance is separate. |
| Standalone `anim CONTENT`; alternate without chance | — | No standalone animation switching or animation draw-only recipe here. |
| `line`; text transform/opacity/fade/env modifiers | — | No whole-phrase selection, text crossfade or explicit text styling here. |
| `look`, spiral `type`/`width`, `warp`, `drunk`, `audio`/`stop`/`volume`/audio `loop` | — | None of the eight authors these appearance, distortion or sound effects. |
| `locked`, `beats N` | — | No beat-relative duration in these recipes. |
| `enter`; named inner burst; fixed burst duration; controller options omitted/defaulted | — | Super Fast uses an unnamed direct hold and authors all controller options. |
| `fade in`/`out`; fractional `env`; `env` without hold; frame/expression/repeated `show` | — | Other visibility shapes are unused. |
| Curves with explicit `ease linear`; raw non-progress clock attributes/operators/functions | — | Expressions use only multiplication/addition and scene/local progress. Default linear curves are used extensively. |

There is no authored `selectionchance` keyword in the eight or the current grammar. A UI field with that name describes ordinary trailing image `chance`; it must not be conflated with the `chance` attached to `alternate`, or the controller's `chance 1/N`.

## Per-pattern decomposition, including exact snippets

### 1. Accelerate — shorten cuts while approaching the image

| Exact authored snippet | Intended visible effect / necessary distinction |
|---|---|
| `pattern accelerate for 2048f` | One bounded overall approach and acceleration arc. |
| `every ramp 56f -> 12f steps 140 ease early -> cut` | 140 progressively shorter local image occurrences. The endpoints specify relative ramp shape: segment lengths are scaled to fit 2048f, so this does **not** promise a literal first 56f/last 12f cut. `ease early` is the cubic early-changing shape. `cut` is not subsequently read. |
| `image alternate chance 0.5` | Intended: select an image every cut, changing theme side with probability 0.5 rather than skipping the image. **Confirmed pre-fix mismatch:** ramps reparse each segment with distinct hidden toggle state. An execution trace with deterministic toggling produced all-secondary cuts in scene one and all-primary in scene two, instead of alternating cut by cut. |
| `zoom [0.4 * accelerate.progress + 0.1 * this.progress]` | Add a gradual whole-scene approach to a small per-cut zoom punch. Two independent clocks are the point of the arithmetic. |
| `origin (curve 0 -> 0.4 over accelerate)` | Projection origin creeps with the scene. The matching base term in zoom prevents origin from overtaking zoom and shrinking the image grid. This is renderer geometry, rather than another creative “layer.” |
| `anim every 4th` | Intended: every fourth cut becomes a live-motion accent. **Confirmed pre-fix mismatch:** each compiled ramp segment had its own animation counter. CompiledVisual execution produced no animated cuts in the first three complete 2048f scenes, then all 140 animated cuts in scene four. This is a behavior bug, not merely difficult syntax. |
| `word primary show 0..0.25 chance 0.5` | On half the cuts, show a primary-side word only in the opening quarter; the accent tightens with the cut duration. `show` and text chance both gate visibility. |
| `spiral speed (curve 1 -> 4 over accelerate)` | Background spiral rotation accelerates over the complete scene, not separately on every cut. |

### 2. Slow Flash — slow primary phase, then fast secondary phase, twice

| Exact authored snippet | Intended visible effect / necessary distinction |
|---|---|
| `pattern slow_flash for 1536f loop 2 seq` | A 1536f slow/fast pair repeated twice: 3072f authored total. `seq` makes the child schedules successive. |
| `pattern slow for 1024f` | Slow section lasts 1024f. |
| `every 64f { image primary zoom (curve 0 -> 0.5) anim every 2nd }` | Primary image refreshed every 64f, zoom restarted for each image, every second image animated. |
| `every 64f { caption primary }` | Primary caption refreshed alongside slow cuts. Separate cadence permits independent text selection, though its period matches the image lane. |
| `spiral speed 2` | Slow section's spiral speed. |
| `pattern fast for 512f` | Fast section lasts 512f. |
| `every 8f  { image secondary zoom (curve 0 -> 0.8 over fast) }` | Rapid secondary-image cuts, but zoom progresses smoothly across the **whole fast section**. A default local curve would restart every 8f and change this effect. |
| `every 16f { word secondary }` | Secondary words refresh every two fast image cuts. |
| `spiral speed 4` | Faster spiral during the fast section. |

### 3. Sub Text — steady images with a secondary text stream

| Exact authored snippet | Intended visible effect |
|---|---|
| `pattern sub_text for 1024f` | 1024f overall scene. |
| `every 64f { image runtime zoom (curve 0 -> 0.375) anim every 3rd }` | Regular runtime-selected images, gentle local zoom, motion on every third selection. |
| `every 32f { subtext secondary }` | Secondary subtext refreshed twice per image. Styling is implicit engine defaults: alpha 0.25, origin 0.375. |
| `spiral speed 4` | Constant spiral layer. |

### 4. Flash Text — image dissolve, with word and caption accents

| Exact authored snippet | Intended visible effect / necessary distinction |
|---|---|
| `pattern flash_text for 1024f` | Complete 1024f scene. Despite its name, the authored dissolve is between **images**, not text. |
| `pattern back for 1024f` | Independent lower image storage and scene-length owner; drawn before the upper lane. |
| `every 128f { image secondary zoom (curve 0 -> 0.8) }` | Opaque lower image selects every 128f and zooms across that lifetime. |
| `pattern front for 1024f` | Independent upper image storage. |
| `every 128f offset 64f { image secondary fade inout zoom (curve 0 -> 0.8) }` | Upper image selects half a cycle apart, fading in/out across its 128f zoom. Lower changes while upper is opaque, upper changes while transparent. |
| `every 64f { word secondary }` | Secondary word accents match the image refresh rate. |
| `every 32f { caption primary }` | Primary captions refresh twice per image. |
| `spiral speed 2` | Constant spiral layer. |

The creative operation is “dissolve into the next image while motion continues.” The actual builtin now expresses it with independent offset lanes. The first image starts opaque and both retained lanes follow theme refreshes. From frame 64, image contributions and zoom match the old snapshot recipe through tested aligned scene repeats. Two identical triangle fades would expose 25% background at midpoint.

### 5. Simple — steady zooming images and captions

| Exact authored snippet | Intended visible effect |
|---|---|
| `pattern simple for 2048f` | 2048f scene. |
| `every 64f { image runtime zoom (curve 0 -> 0.5) anim every 3rd }` | Regular runtime-selected images with restarted local zoom; every third selection animates. |
| `every 32f { caption primary }` | Primary captions refresh twice per image. |
| `spiral speed 3` | Constant spiral layer. |

### 6. Super Parallel — three staggered overlapping image lanes

| Exact authored snippet | Intended visible effect / necessary distinction |
|---|---|
| `pattern super_parallel for 1152f` | Scene contains twelve 96f lane cycles. Children run concurrently by default. |
| `pattern back for 1152f { every 96f { image primary zoom (curve 0 -> 0.875) anim every 2nd } }` | Opaque primary lane, refreshed every 96f with local zoom and every-second-selection motion accent. |
| `pattern middle for 1152f { every 96f offset 32f { image primary alpha 0.5 zoom (curve 0 -> 0.875) } }` | Second primary lane, shifted one third of a cycle, drawn at 0.5 alpha above the first. |
| `pattern front for 1152f { every 96f offset 64f { image secondary alpha 0.33 zoom (curve 0 -> 0.875) } }` | Secondary lane shifted two thirds of a cycle, drawn last at 0.33 alpha. |
| `every 32f { word runtime }` | Word refresh every one-third lane cycle. |
| `spiral speed 3` | Constant spiral layer. |

The three nested pattern scopes retain independent current images. Alpha and source order make them visible as a composite rather than the last opaque draw hiding the others. `offset` pre-rolls clocks with entry actions suppressed; it is a phase shift, not a media preload or generic delay. The recipe's intent is a staggered stack; startup state and source-over weighting remain engine details that a simplification must account for.

### 7. Animation — animation alone, then a visiting still overlay

| Exact authored snippet | Intended visible effect / necessary distinction |
|---|---|
| `pattern animation for 1024f` | 1024f scene. |
| `pattern motion for 1024f { every 64f { image runtime zoom (curve 0 -> 0.625) anim } }` | Always-animated subject, reselected every 64f with local zoom. Still fallback receives the same motion if no animation is available. |
| `every 64f offset 48f` | Phase-shift the overlay visit relative to the underlying 64f motion cycle. |
| `pattern visit for 1024f` with `image secondary env in 8f hold 16f out 8f zoom (curve 0.5 -> 0.625)` | A separate secondary still selection visits above the animation: rise, full-opacity hold, fall, then zero alpha for the unused tail of the 64f cycle. It has its own modest zoom range. The nested `visit` scope retains its image independently of `motion`. |
| `every 32f { caption runtime }` | Runtime-selected caption twice per image cycle. |
| `spiral speed 3` | Constant spiral layer. |

The full-opacity hold **and the real absence** are the visual intent. `fade inout` would make a triangle over the entire cycle and leave nearly permanent double exposure. At displayed integer samples, the source's 8/16/8 envelope yields 33 zero-alpha, 17 full-alpha and 14 intermediate samples per steady 64f cycle; continuous nominal leg lengths and displayed sample counts differ.

### 8. Super Fast — rapid cuts interrupted by animated holds

| Exact authored snippet | Intended visible effect / necessary distinction |
|---|---|
| `pattern super_fast for 2048f` | 2048f scene. |
| `burst -> rapid period 8f chance 1/12 cooldown 64f duration 64f..128f` | At eligible 8f boundaries, a one-in-twelve roll may interrupt rapid cuts for a sampled 64–128f hold. Cooldown is 64f after hold exit; durations are sampled/rounded in whole periods. `rapid` names the controller but is not read. |
| `base { every 8f { image runtime zoom (curve 0.0625 -> 0.1875) } }` | Normal state: new runtime image every 8f, each with a small restarted zoom punch. |
| `burst { image runtime zoom (curve 0.1 -> 0.7) anim }` | Interruption: select once, then hold animated content with continuous zoom across the sampled lifetime. Direct branch effects fire on entry, not every controller period. |
| `every 8f { word primary chance 0.25 }` | Independent primary word accents on one quarter of 8f occurrences. This sibling lane continues during the hold. |
| `spiral speed 3` | Constant spiral layer continues during either phase. |

Only the active controller branch advances or draws. The rapid-image lane stops behind a hold and restarts when base returns; simultaneous hidden cutting would be a different effect. A random hold of a sampled duration needs that hold's own clock; base has no known interruption endpoint.

## Where intent and implementation diverge

- Names after `pattern` provide mandatory structure and possible clock handles. Only `accelerate` and `fast` are explicitly read as named motion clocks. Optional `-> cut` and `-> rapid` add no referenced behavior to these recipes.
- “Choose still or motion,” “select a theme side,” “when to replace content,” “how to transform it,” and “when to show it” are different choices. Current `image ... anim every Nth` packs several together. Animation state is shared by the runtime, not stored independently in each image register.
- A runtime image selection and its animation load can independently resolve `runtime` side; using one content word does not establish a same-side image/animation pair. Ordinary image chance guards the still pull, but the following animation load and every-Nth counter pulse are unguarded. None of these eight uses trailing image chance, but it matters when proposing a uniform authoring block.
- There are only two theme sides. Runtime selection and alternation are policies over those sides, not additional independent media banks.
- Independent nested image scopes replace authored register and copy handoff machinery. Source order still defines compositing order. A general schedule can coordinate multiple subjects; a dedicated image lane convention need not impose a global draw cap.
- `alpha` is opacity (`brightness` is an alias), not a separate luminance adjustment. Main words use implicit zoom/origin 0.75; captions default to alpha 0.2/origin 0.5. Parsed word/line alpha/fade/env are not applied by their render path, so text/image parameter uniformity cannot be assumed.
- Curves normally read the nearest timed occurrence; `over` and named-expression reads preserve a longer motion arc while cuts restart. A simpler presentation must retain that distinction.
- Progress is frame/N over displayed samples 0 through N−1; authored curve endpoints are approached rather than necessarily displayed. Ramp normalization, offset startup, envelope samples, random selection and fallback are concrete equivalence boundaries, not proof that the present syntax is necessary.
