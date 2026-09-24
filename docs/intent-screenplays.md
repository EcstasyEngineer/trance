# Built-in visual intent

The built-ins are editable v3 recipes in
[`builtin_patterns_v3.cpp`](../src/trance/visual/builtin_patterns_v3.cpp).
Their current source is authoritative. The older handwritten visuals are useful
design references, not a frame-for-frame compatibility requirement.

This page retains the distinct visual ideas from the original intent audit
without retaining its obsolete claims about what v3 cannot express. The original
implementation can be inspected with:

```sh
git show ae7d94c:src/trance/visual/visual.cpp
```

## Current recipes and historical ideas

| Pattern | Current recipe | Historical idea worth preserving when re-authoring |
|---|---|---|
| `accelerate` | A 2048-frame, 140-step accelerating cadence; overall approach plus small per-cut zoom; every fourth selection animates; probabilistic theme changes and short word windows. | The fast end should last long enough to feel sustained. Keep the scene's slow approach distinct from each cut's motion. |
| `slow_flash` | Two repeats of a slow primary section and a fast secondary section. Slow images have 64-frame local zoom; fast images change every 8 frames and follow the whole fast section's zoom. | A caption/text call-and-response in the slow section, followed by blinking words in the fast section. Current text is simpler. |
| `sub_text` | Runtime-selected images every 64 frames, every third selection animated, secondary subtext every 32 frames. | Alternating image themes, a word-by-word foreground phrase, and repeated subtext that slows across passes. Those older behaviors are not all present in the current recipe. |
| `flash_text` | Secondary-image crossfades using `copy cur -> prev`; the outgoing layer continues zooming while the incoming layer fades in. Words and captions run on separate cadences. | Text reveals during only part of the dissolve, periodic theme changes, and occasional animated dissolves. Image crossfade does not imply independent text crossfade support. |
| `simple` | One image per 64 frames with local zoom, every third selection animated, and captions every 32 frames. | A resting rhythm with a whole phrase visible only during the middle half of a longer cycle. The current recipe has no large-text layer. |
| `super_parallel` | Three 96-frame image layers offset by 0/32/64 frames, with alpha 1/0.5/0.33; an animation accent on the first layer and independent words. | Alternate layered passages with brief solo windows where one image is fully visible. The current recipe keeps the layers composited. |
| `animation` | An animated base with a still overlay using an 8-frame rise, 16-frame hold, 8-frame fall and the rest absent, offset within a 64-frame period. | The animation must have intervals alone on screen. A whole-period triangular fade would leave the still overlay present for almost every frame. |
| `super_fast` | Rapid 8-frame cuts with local zoom, interrupted by animated holds lasting 64-128 frames with a continuous local zoom. Words run independently; spiral speed is constant. | The original also previewed the next cut, silenced words during holds, and flipped the subsequent theme on burst entry. These are optional design differences, not requirements of the clock repair. |

These are source-based descriptions, not a claim that the current visuals have
been perceptually compared with the originals on every output.

## The useful distinction: selection, movement, visibility

A held image can still move. A frequently selected image can have no movement.
A text selection can change while its draw is hidden. Treat these as three
independent decisions:

- **Selection:** when an effect fetches the next item.
- **Movement:** which clock the numeric transform follows.
- **Visibility:** when the corresponding draw paints.

SuperFast exposed a failure in this distinction. Its burst needed one media
selection plus motion across the entire sampled hold. The repaired phase owns
its children and its local clock. The rapid base explicitly contains 8-frame
cuts, each with a fresh zoom curve. A still fallback then has the same authored
motion as the animation request.

For a scene-wide buildup over repeated cuts, reading both clocks is useful.
The existing `over NAME` modifier and `NAME.progress` expressions provide that
without allowing children to mutate a parent's timer.

## Language support versus possible future work

The public language already has `show` visibility windows, `env` envelopes,
`line` for phrase selection, `alternate` for statement-local theme alternation,
image `copy`, nested schedules, and named ancestor-clock reads. Prefer composing
these features before adding another keyword.

The current parser has no general text-register system, `spell` type-out verb,
shared named alternator, or public shadow/font-cadence controls. Some underlying
runtime operations exist, but that does not make their proposed syntax shipped.
Revisit such additions only for a concrete recipe that remains awkward with the
current language.

The old extension numbers, drift ledger, and issue-based implementation plan
have been retired. Current syntax belongs in the
[language reference](spec-grammar-v3.md); runtime behavior belongs in
[engine-today.md](engine-today.md). Historical ideas here are choices for future
visual design, not an implicit backlog.
