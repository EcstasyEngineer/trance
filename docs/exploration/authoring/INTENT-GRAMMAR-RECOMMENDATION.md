> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Intent grammar recommendation

Recommendation: keep the scheduler, but make media intent explicit and give each authored media statement a stable identity. The eight recipes do not justify exposing image registers, hidden animation counters, opacity-expression replacement, and parser-generated clock scopes as the normal authoring model. A large runtime rewrite is unnecessary; the main problems are the surface vocabulary and ownership boundaries.

This is a proposal, not implemented syntax. Evidence is the checked-out `builtin_patterns_v3.cpp`, `pattern_parser_v3.cpp`, `pattern_compiler.cpp`, `compiled_visual.cpp`, and `render_eval.cpp`; no new playback or server testing was performed for this recommendation. The companion [builtin map](BUILTIN-GRAMMAR-MAP.md) gives the recipe evidence. Source reasoning below is distinguished from claims requiring a runtime change.

## Seven independent questions

| Question | Author-facing concept | Existing mechanism / boundary |
| --- | --- | --- |
| Which content? | Source: primary, secondary, random side, alternating side | Image selection effects; two real theme sides only |
| When choose again? | Selection cadence | `every`, ramp, phase entry; holding is absence of another selection |
| When does selected content move? | Animation policy: still, always, every N entries | `anim` / hidden pulse; currently one shared animation selection, not independent layer streams |
| When is it present? | Visibility window or probabilistic accent | `show` / text chance; distinct from choosing content |
| How opaque? | Opacity curve or attack/hold/release envelope | Alpha expression; distinct from a visibility window |
| How does it move spatially? | Zoom and zoom baseline, on a stated clock | Existing zoom/origin; `origin` is not x/y position |
| How does it hand over? | Cut or image dissolve | Existing ordered copy/draw/new-image effects; outgoing snapshot ownership matters |

Time belongs to the surrounding pattern/cadence/phase. It should not determine the identity or lifetime of a media lane's selection counter. A sampled burst has a usable lifetime; the indefinite base does not. Keep that distinction rather than inventing normalized time where there is no endpoint.

## What the eight actually need

| Recipe | Essential intent | Machinery the ordinary author should not need to infer |
| --- | --- | --- |
| Accelerate | 140 shortening cuts over 2048f; probabilistic side switching; every-fourth motion; scene approach plus per-cut punch; quarter-cut word accent; spiral speedup | Ramp expansion, independent hidden counters per segment, raw arithmetic coupling `origin` and zoom |
| Slow Flash | Slow primary phase then fast secondary phase; repeat pair twice; local slow zoom versus whole-fast-phase zoom; alternating motion cuts | Difference between schedule ownership and transform clock |
| Sub Text | 64f image cuts, every-third motion, 32f secondary subtext | Hidden motion counter and text path defaults |
| Flash Text | Image dissolve every 64f, outgoing zoom continuing from 0.4 to 0.8; incoming 0 to 0.4; independent words/captions | Snapshot before replacement, outgoing-before-incoming draw order, empty first outgoing image |
| Simple / Parallel | 64f image cuts and every-third motion; 32f captions | Same image idiom as Sub Text, with another text role |
| Super Parallel | Three 96f image lanes offset by 0/32/64f, opacity 1/0.5/0.33; first lane's motion accent; 32f words | Separate image registers and source-over order; offset pre-roll is not delayed startup |
| Animation | Always-moving base with a still visitor; 8f rise, 16f hold, 8f fall, then absence; offset 48f | An envelope is not a whole-cycle triangle; the absence and full-opacity hold are essential |
| Super Fast | 8f still cuts interrupted by 64–128f moving holds; continuous movement during each hold; independent word accents | Selection once on phase entry; only the active branch advances; interrupted base restarts |

The unused labels `cut`, `beat`, and `rapid` are removable from these particular recipes after checking references. The `a/b/c` and `cur/prev` image identities carry real state and must not simply be deleted. The scene names read by `over accelerate` and `over fast` carry real timing meaning.

## Decisive defects in the current vocabulary

1. **One spelling of chance has three meanings.** Image chance skips reselection while an old image can remain visible. Text chance also hides the text. Alternate chance controls whether the side toggle flips while image selection continues. Name these `select chance`, `visible chance`, and `switch chance` in the authoring surface. Burst eligibility is another distinct probability. Do not pretend they are one interchangeable parameter.
2. **Animation frequency is hidden state, not a time interval.** Make it a first-class media policy, separate from cadence and visibility. Today `parse_anim` emits animation loading on every entry; the every-N counter only gates drawing animation. Those loads and the counter also run when image-selection chance misses. `runtime` resolves separately for still-image and animation effects. A clearer design must deliberately choose whether it preserves or changes each behavior.
3. **Ramp expansion leaks into media identity.** `parse_ramp_cadence` reparses the body and mints different hidden counters/toggles for each segment. Source reasoning predicts a failure to count successive cuts. The parent reviewer then traced the actual `CompiledVisual`: Accelerate has no animated cuts on scene iterations 1–3 and all 140 cuts animated on iteration 4; an ordinary cadence counts every fourth entry correctly. A deterministic ramp alternate selects only secondary on its first pass and only primary on its second, instead of alternating per cut. Preserve authored statement identity across expansion while retaining each segment's local clock; do not require an extra visible register declaration to repair this.
4. **Accepted modifiers can do nothing.** `word`/`line` parse alpha/fade/env but their render path does not consume alpha; caption/subtext parse zoom but do not consume it. Begin with compiler warnings and inspector restrictions for compatibility, rather than suddenly rejecting previously accepted files. New syntax should have target-specific valid properties.
5. **Composition rules are inconsistent.** Multiple visibility windows combine with AND; alpha, fade and env overwrite the same expression, so later clauses win. Offer one opacity policy plus a separate visibility policy, with an explicit diagnostic for conflicting opacity controls.
6. **Units and names overpromise.** Ramp `56f -> 12f` values are relative weights rescaled to fit the span, not promised endpoint frame durations. Show cut count and spacing shape separately. Rename `origin` to “zoom baseline” in the editor and explain it relative to zoom; do not draw it as an x/y-position control. “Brightness” is an alpha alias, not a color-brightness effect.

## Candidate A: small, compatible improvement

Retain `pattern`, `every`, sequence, offset and burst. Organize inspector controls by the seven questions above. Keep exported `.pattern` syntax valid and preserve existing execution except for separately approved bug fixes. Give media statements persistent semantic identity internally so changing a fixed cadence to a ramp does not silently create a different animation-counter lifetime.

This is the preferred immediate path. It improves all eight, exposes unsupported controls honestly, and requires no invented renderer. `anim every Nth` deserves its own motion control, not an appended string hidden beside zoom. `env` should appear as an opacity envelope with its absent tail visible on a timeline. Registers remain available in advanced source, but the ordinary dissolve editor owns the outgoing/current image pair.

## Candidate B: coherent replacement surface

If changing author syntax is worthwhile, use typed media blocks inside the existing schedules. For example, these are illustrative rather than executable:

```text
every 64f {
  image runtime {
    motion every 3 entries
    zoom 0 -> 0.5 over occurrence
  }
}
```

```text
every 64f offset 48f {
  image secondary {
    motion still
    opacity attack 8f hold 16f release 8f
    zoom 0.5 -> 0.625 over occurrence
  }
}
```

For Flash Text, a typed image `transition dissolve` block would contain both incoming and outgoing zoom curves, and lower to the existing snapshot-before-select and outgoing-before-incoming recipe. It must preserve the existing 64f phase, two-stage zoom, empty initial outgoing state, live incoming refresh, and frozen outgoing snapshot. It must not imply text dissolves or independently captured animation streams.

This alternative is more legible and easier to validate than a flat arbitrary modifier list, but it needs a real parser/migration change. Do not implement it as a second browser-only language whose accepted combinations exceed the product parser. Nor should every new intent become a special builtin-specific keyword: the same opacity envelope serves visitors, image fades, and future compatible targets.

## Keep, consolidate, or move out of the basic palette

| Family | Decision |
| --- | --- |
| Constant/curve/expression; easing; explicit ancestor clock | Keep one modulator mechanism. Curves and clock choice are essential in the eight; raw expressions remain advanced. |
| `show`, `fade`, `env`, alpha/brightness | Keep visibility and opacity as two concepts; consolidate fade/env under opacity; hide misleading alias; detect ignored/conflicting uses. |
| Image selection, draw, copy, register qualification | Keep underlying general mechanisms; hide ordinary lane storage and dissolve snapshots behind media intent. |
| Animation always/Nth/standalone | Expose motion policy first-class; standalone load is advanced shared-stream control. Do not imply motion equals reselection. |
| Fixed/ramp/offset/sequence/loop | Keep; clarify duration, relative ramp weights, entry, and phase shift. Unused clock names add no value. |
| Burst period/chance/cooldown/duration/base/enter | Keep one interruption controller with explicit normal/hold phases. `enter` is advanced setup, not another independent animation layer. |
| Word/line/caption/subtext | Keep content role/splitting distinction; show only properties actually honored by each render path. Shared text paths remain a limitation. |
| `beats`/`locked` | Optional timing input; describe rounded nominal cadence, never audio-phase synchronization. Not needed by the eight. |
| Look/type/width; warp/drunk; theme audio/volume/stop | Separate advanced appearance/audio sections. These are implemented capabilities outside the eight; absence from builtins alone is not grounds to remove them. Prefer one warp mechanism over a special `drunk` preset spelling. |

## Migration boundary and practical agreement

Agent 16 agrees on separating authoring intent from engine artifacts and retaining macro/local clocks, snapshot ownership, and offset semantics. The motion-accent control attaches to an image stream; it must not introduce another rendering lane. Its preferred meaning is every N image changes, using the chosen content side. Changing current every-entry counting or independently selected runtime sides requires an explicit semantic decision. Agent 17 agrees on stable authored media identity and recommends warning before hard rejection of ignored modifiers. All three reviewers agree on the narrow ramp repair: key hidden policy state by authored source position plus operation role, while preserving segment-local image registers and clocks. Agent 17 owns that implementation and targeted assertions; no broad syntax rewrite is required.

A source-format-only migration can preserve existing schedule nodes, frame quantization, occurrence gates, effect order, image snapshots, draw order and shared animation/text resources. It cannot simultaneously claim exact equivalence and change ramp counters, animation loading, random-side resolution, or chance meaning. Treat those as explicit semantic corrections with targeted evidence. Independent animated lanes, copyable text, audio-accurate beats and true 2D positioning remain runtime work, not grammar cleanup.

