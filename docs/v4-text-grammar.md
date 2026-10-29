# Compact V4 text

V4 text is a small spelling of the same JSON AST used by the native compiler.
It covers the features exercised by the eight built-ins and the requested
subpattern timing controls. It does not expose every advanced JSON feature. The
composer defaults to text; switching an unsupported AST to text reports an error
and preserves the document. JSON remains an editable interchange format.

```text
v4;
parallel id example for 1024 {
  parallel every 64 {
    image primary zoom 0 -> 0.5;
  }
  spiral speed 2;
}
```

Frames are whole content ticks. Whitespace is insignificant. Blocks use braces;
outputs end with semicolons. Numbers are finite decimal/scientific literals,
and a rational such as `1/12` is a probability, not an arithmetic expression.

## Composition

`parallel`, `sequence`, and `cycle` map to the AST's parallel, sequential and
round-robin random compositions. `cycle cooldown N` fixes round robin, first
child zero and self-selection off. Other random selector policies remain JSON
features. Keep explicit wrapper patterns: they own duration and modulation clocks.

Header clauses are `id NAME`, `for N`, `repeat N`, `every N`, `phase N`, and
`loop_subpatterns true|false`. Use `for inherit` to calculate duration from
subpatterns, keeping that relationship when their durations change.
Use `every ramp(from,to,steps,early)` for the cadence ramp used by Accelerate.
Phase is the existing clock offset, not a media preload or inactive delay.

Chance exits attach to the selected child edge:

```text
v4;
cycle for 256 cooldown 64 {
  exit 1/12 every 8: parallel {
    parallel every 8 { image either zoom 0.0625 -> 0.1875; }
  }
  parallel for 96 {
    image either zoom 0.1 -> 0.7 animation 1;
  }
}
```

`loop_subpatterns true` on parallel/sequence repeats subpatterns to the owner's
boundary; omitted or `false` lets them complete independently. Inherit derives
the finite sum (sequence) or maximum (parallel) from timed subpatterns at every
level. Omitting `for` on a nested pattern retains the existing parent-window
behavior. See the [timing contract](spec-grammar-v4.md#timing) for restrictions.
The former `child_mode` and sampled-duration syntax are rejected.

```text
v4;
sequence for inherit {
  parallel for 200 { image primary; }
  parallel for 300 { image secondary; }
}
```

This lasts 500 frames. Editing the second duration to 400 makes it 600 frames;
the authored parent remains `for inherit`.

## Outputs and values

The text vocabulary is `image`, `word`, `caption`, `subtext`, and `spiral`.
Media outputs take a theme source; images support primary, secondary, either
and alternate. Only built-in-used property combinations are available in text:

- Images: `zoom`, `baseline`, `opacity`, `envelope(in,hold,out)`, `animation N`,
  and alternate-source `flip P`.
- Words: selection `chance P` and `visible(start,end)`.
- Spirals: `speed`.
- Caption/subtext: the theme source.

Linear curves use `from -> to`; local scope is implicit. Append `@scene` or an
ancestor ID for a different clock. The single built-in sum is
`sum(curve,curve)`. Image opacity also accepts `triangle(from,peak,to)`; an image
envelope is exclusive with opacity. These are fixed structured values, not
general functions, user variables or arbitrary expressions.

All eight literal examples and expected ASTs are checked by browser/native
fixtures. Canonical formatting is stable; unsupported fields, duplicate clauses
and trailing source are rejected rather than discarded. V3 source remains
unsupported. Limits are four MiB source, 64 composition levels and 4,096 children
per composition, enforced before recursive processing.
