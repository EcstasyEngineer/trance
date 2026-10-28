# Minimum V4 text: lexical and syntax review

Analysis only, 2026-10-28. The feature boundary is the eight production sources
in `src/trance/visual/builtin_patterns_v4.cpp`, plus the separately requested
parent/child completion modes. Existing JSON capabilities do not expand that
boundary. This proposal is a text view of the same semantic AST and scheduler.

## Recommendation

Use explicit `parallel`, `sequence` and `cycle` blocks. Keep every authored
wrapper, including single-output cadence wrappers and Super Fast's outer
chance-exit owner. Their local clocks and lifetimes are meaningful. Do not
flatten them into scheduled output shorthand.

The document starts with `v4;`. Blocks have braces; output statements end with
semicolons. Whitespace, including newlines, is insignificant. All timing numbers
are content frames: no unit suffix, seconds conversion or inferred duration.
Identifiers use the current ASCII identifier rules. Numbers are finite decimal
literals; their parser must consume the whole token. A fraction such as `1/12`
is a probability literal only, never division. No strings, arbitrary expressions,
variables, functions, imports or aliases are necessary for the baseline.

Composition clauses have one spelling each:

```text
parallel id example for 1024 {
  parallel every 64 phase 32 {
    image secondary zoom 0 -> 0.5 opacity 0.5 animation 3;
  }
}
```

`sequence` maps to `kind: sequential`. `cycle cooldown 64` maps to `kind: random`
with `order: round_robin`, `allow_self: false`, `first: 0` and the stated cooldown.
Those fixed selector fields are implicit because the eight built-ins never
exercise another value. Cycle is an honest description of that behavior; it
does not add a new semantic composition kind.

Other clauses are `repeat N`, `for sample(min,max,step)` and
`every ramp(from,to,steps,early)`. The ramp constructor admits only `early` in
this restricted baseline. `phase N` maps to existing `offset`; it advances phase
without preloading images and is not a delay. Root and iteration durations stay
explicit. A chance exit belongs visibly to its edge:

```text
cycle cooldown 64 {
  exit 1/12 every 8: parallel {
    parallel every 8 {
      image either zoom 0.0625 -> 0.1875;
    }
  }
  parallel for sample(64,128,8) {
    image either zoom 0.1 -> 0.7 animation 1;
  }
}
```

Putting `exit` in the group header would make the two meanings of `every`
needlessly ambiguous. The edge prefix keeps AST placement explicit.

## Measured feature boundary

| Built-in | Required exceptional feature |
| --- | --- |
| Accelerate | Early cadence ramp; scene/local curves; two-curve zoom sum; zoom baseline; probabilistic alternate flip; word visibility and chance |
| Slow Flash | Sequence; finite repeat; named ancestor curve |
| Sub Text | Subtext; either source |
| Flash Text | Local triangle image opacity; phase |
| Parallel | Constant image opacity; phase |
| Super Parallel | Three ordered image lanes; two phases |
| Animation | Integer-frame image envelope; phase |
| Super Fast | Fixed round-robin selector; cooldown; rational chance exit; sampled duration; word selection chance |

The output vocabulary is exactly `image`, `word`, `caption`, `subtext`, `spiral`.
Image sources require all four existing choices; word needs primary, secondary
and either; caption needs primary and either; subtext uses secondary. Numeric
parameters remain editable values, not hardcoded built-in recipes.

There are 18 explicit curves: 14 local, three scene and one named ancestor.
Every curve is linear. Write `a -> b`, optionally followed by `@scene` or an
ancestor identifier. Omit the local annotation canonically. There is no easing
clause for scalar curves. A fixed two-curve `sum(curve,curve)` suffices for the
one zoom sum; it does not imply recursive sums, arithmetic precedence or a
general expression grammar. `triangle(a,b,c)` is an image-opacity value only.
`envelope(in,hold,out)` is a separate image property, exclusive with opacity.

Keep the concrete output/property applicability boundary: image zoom, baseline,
opacity, envelope, animation and alternate-source flip; word selection chance
and visibility; spiral speed. Caption/subtext have no authored modulation in
this baseline. Linear zoom/baseline/speed, constant speed/opacity, sum zoom and
triangle opacity cover the actual structured values.

Use the short property spellings `baseline` for `zoom_baseline`, `flip` for
`flip_probability`, `chance` for `selection_probability`, and `visible(a,b)` for
`visibility: [a,b]`. These are the only accepted text spellings, not additional
aliases. Every other property uses the spelling shown above.

Exclude `line`, stochastic selector order, self-selection, nonzero first child,
weights, forever repeat, nonlinear scalar curves, late cadence easing, image
selection chance, text opacity/envelopes, recursively nested values and generic
property bags. None has a caller in these eight sources. This restriction
governs the new text authoring surface; existing advanced JSON remains valid.

## Completion extension

Add one mutually exclusive `child_mode independent|loop|wait` clause to parallel
and sequence compositions. Independent is the implicit default. Do not add
independent booleans for looping and waiting, leader choice, any/all completion,
a frame calculator or a duration-inference system. Phase remains orthogonal.

Loop must mean sequence-list restart for sequential ownership and independent
completed-lane restart for parallel ownership, both bounded by the parent.
For the first wait implementation, admit only statically finite gating children:
derive the sequential sum or parallel maximum of their effective finite spans.
Direct outputs follow their owner and do not gate its completion. Reject sampled
gating subtrees rather than drawing randomness to predict completion. A wait
owner has no `for`, `every` or `phase` clause. This is a narrow validated mode,
not a general inference feature or a manual calculate-frames UI. Warn on
established inactive tails in independent mode without silently stretching a
child or changing its modulation clock.

## Round-trip contract

Normalize only documented defaults: omitted local curve scope, fixed cycle
policy fields, and omitted independent mode. Preserve authored IDs, exact
numeric values, duration/repeat, edge placement, node order and every wrapper.
Canonical formatting has fixed clause order and stable indentation. Parsing
must reject duplicate clauses, unknown constructs and trailing input.

The formatter must fail explicitly on JSON forms outside the text subset,
leaving the document intact. It must never silently drop a property or coerce
random selection into a cycle. Acceptance requires all eight AST -> text -> AST
fixtures, stable formatting and equivalent native schedules/render values.
Source comparison alone cannot establish runtime equivalence.

The structural review supplies complete examples for all eight built-ins using
this agreed block/value vocabulary. The main tradeoff is deliberate: retaining
wrappers costs a few lines but avoids introducing a second set of timing rules.
