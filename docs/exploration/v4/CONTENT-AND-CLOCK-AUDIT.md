> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# V4 content, clocks, and transition audit

This audit traces the current production sources, rather than inferring behavior from editor labels. It is an independent assumption review; it does not claim that the proposed V4 implementation already exists.

## What `runtime` actually means

`runtime` is an unfortunate name for **choose either theme randomly on this selection**. It has no relationship to elapsed time, clock names, animation progress, or pattern scheduling.

`pattern_parser_v3.cpp::content_to_slot` maps the word to `Slot::Runtime`. `compiled_visual.cpp::resolved_slot` calls `random_chance()` whenever an effect executes. `common/util.h` defines that as a uniform two-way choice. `VisualApiImpl` forwards the resulting Boolean to the primary or secondary `ThemeBank` lane. It does not choose a random scene or image category; selection of an actual item within that lane remains the theme bank's job.

Recommended V4 vocabulary:

| Current selector | Proposed label/token | Actual intent |
| --- | --- | --- |
| `primary` | Primary theme / `primary` | Always use the first active theme lane. |
| `secondary` | Secondary theme / `secondary` | Always use the second active theme lane. |
| `runtime` | Either theme / `either` | Independently choose one of those two lanes for each selection. |
| `alternate` | Alternate themes / `alternate` | Flip a private remembered side before each selection. |

Keep `random` for the proposed composition kind; naming content `either` avoids overloading it. These selectors are content properties, not additional pattern kinds.

### Every built-in's actual content usage

| Built-in | Image selector | Text selectors | Animated selection |
| --- | --- | --- | --- |
| Accelerate | Alternate, flip probability 0.5 | Primary word | Every fourth image selection attempt |
| Slow Flash | Primary slow section; secondary fast section | Primary caption; secondary word | Every second slow image |
| Sub Text | Runtime/either | Secondary subtext | Every third image |
| Flash Text | Secondary in both independent lanes | Secondary word; primary caption | None |
| Simple | Runtime/either | Primary caption | Every third image |
| Super Parallel | Primary back/middle; secondary front | Runtime/either word | Every second back image |
| Animation | Runtime/either base; secondary visitor | Runtime/either caption | Always animate base |
| Super Fast | Runtime/either rapid and held images | Primary word | Always animate held image |

Therefore runtime occurs in **five of eight** built-ins, not just Super Fast. These are source occurrences, not a claim that every text firing changes its phrase's source: `change_text` continues existing split words until the phrase is consumed before fetching another theme phrase.

### Alternate and chance are different operations

The private alternate bit starts at zero; the parser emits a Toggle before selection. The first actual selection consequently uses secondary, then primary. Comments that write `A B A B` do not specify this initialization accurately unless A means secondary.

`alternate chance 0.5` gates the **flip**, not the image selection. Every image selection still occurs; the chosen side can remain unchanged. With a fair flip coin the resulting side sequence has the same distribution as independent either-theme draws, although random-number consumption and deterministic seeded traces differ. With a lower probability it produces longer runs on each side.

Ordinary trailing image `chance` gates replacement of the stored image. Text `chance` also gates rendering for that occurrence. The current parser quantizes these probabilities to 100 buckets and clamps the bucket count to 1 through 99: zero and one are not true never/always. A V4 scalar probability should explicitly support the endpoints and reject values outside zero through one. Retaining existing built-in 0.25/0.5 behavior requires no expression language.

### Existing animated theme mismatch

The image effect and its appended animation-load effect each independently resolve `Slot::Runtime`. This gives a 50% chance of selecting the still fallback from one theme and loading animation from the other, before accounting for animation availability. Alternate shares a hidden selector bit and does not have this particular mismatch.

V4 should resolve the side **once per image occurrence** and reuse it for that occurrence's still fallback and animation. Separate image and text operations can remain independent. Tests should inspect both API arguments; checking only successful compilation cannot catch this.

There is a separate existing limitation: `Registers::anim_slot` is shared by animated draw statements, and actual animation stream state is shared per theme lane. Three base composition kinds do not by themselves provide independent playback streams for arbitrary simultaneous animations. Do not advertise universal media isolation merely because schedule nodes nest.

## Clock names

All eight built-ins were inspected for named references:

| Authored name/reference | Needed by existing built-in? | Reason |
| --- | --- | --- |
| `rapid` controller name | No | Super Fast never refers to `rapid.*` or `over rapid`. Remove this authored name. |
| `cut` ramp occurrence name | No | Accelerate uses `this.progress`, not `cut.progress`. Can be anonymous. |
| `accelerate` scene name | Yes under current expression syntax | Zoom reads `accelerate.progress`; origin and spiral read `over accelerate`. |
| `fast` nested section name | Yes under current expression syntax | Slow Flash's fast image zoom uses `over fast`, spreading motion across the section instead of resetting at every 8f image. |
| All other built-in pattern names | Labels only | No expression or modulator references them. |

An anonymous controller can retain an internal identity and functioning local clock. Removing public naming is distinct from deleting elapsed-time semantics. Preserve local occurrence time and ancestor-relative curves in the V4 AST. A user-facing curve scope such as current image/current section/whole scene could replace most authored names; a compiler may resolve those scopes to internal node identities. Accelerate's additive whole-scene drift plus per-cut motion remains a concrete requirement, not removable accidental complexity.

## Burst is not presently a generic random chooser

Super Fast is the only built-in using the burst controller. Its behavior in `BurstCycler::advance` is:

1. Rapid images run on their local 8f cadence while the base is active.
2. At eligible controller-relative 8f boundaries, roll an exact 1-in-12 chance. This is **not per frame** and no roll occurs during a held animation.
3. A success chooses an integer duration of 8 through 16 period ticks, inclusive: 64, 72, ..., 128 frames.
4. The hold selects one image/animation on entry and advances its own normalized motion over the sampled lifetime. Rapid image selection is suspended.
5. Hold completion deterministically returns to a restarted rapid section. The return frame cannot immediately trigger another interruption. A 64f cooldown elapses before further eligible checks.

The chance is an integer denominator under the current parser; no runtime expression is involved. Sampling a uniform frame count between 64 and 128 instead of nine period-aligned durations would change its distribution and transition grid.

### Peer-reviewed three-kind formulation

The independent model audit proposed a concise equivalent: an exclusive controller whose child order is round robin; a rapid child with a probabilistic exit checked every 8f; a held child that exits on completion after a sampled 64..128f duration in 8f increments. Both children restart on entry. A controller cooldown of 64f applies after transitions, with initial eligibility immediate. The hold lasts at least the cooldown, so the next return can start the intended rapid cooldown without adding a default/interrupt phase type or a special return target.

For exact Super Fast preservation, checks must use the controller-relative grid and skip the transition frame. A plain random child selection on completion is insufficient: the rapid child has no predetermined end, and the held child must return to rapid. Round robin over these two children supplies that return naturally. An `allow self` option matters for proper random choice, not this two-child round-robin recipe; with one child, the implementation must define whether a permitted self-transition restarts it or simply continues it.

Parallel/sequential/random can therefore be the only **composition kinds**, while cadence, finite or sampled lifetime, offset, selection order, switching chance, check interval, and cooldown remain necessary scheduling properties. Consolidating node kinds should not erase those behavioral requirements.

## Required validation evidence for V4

- Compile and inspect all eight translated built-ins, including five either-theme users.
- Observe the same theme-side Boolean for an animated image's still selection and animation load; keep distinct operations independently selectable.
- Check scalar odds at 0, 1, and a fractional value; distinguish draw selection chance from alternate-flip chance and controller switch chance.
- Record Super Fast transitions, rapid selection counts during holds, duration quantization, first roll eligibility, return-frame suppression, and cooldown boundary.
- Verify Accelerate retains whole-scene drift plus per-cut zoom; Slow Flash retains section-relative fast zoom.
- Exercise nested random inside parallel/sequential and both around random, including inactive branch suppression and restart on re-entry.
- Verify two simultaneous animated lanes explicitly or document the current shared-stream limitation.

Source anchors: `src/trance/visual/builtin_patterns_v3.cpp`, `pattern_parser_v3.cpp` (`content_to_slot`, `parse_alternate`, `parse_anim`, `parse_chance`, `parse_burst`), `compiled_visual.cpp` (`resolved_slot`, `run_effect`), `cyclers.cpp` (`BurstCycler::advance`), `api.cpp`, `pattern_ast.h`, and `src/common/util.h`.
