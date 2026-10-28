# Minimal V4 text: structure and exact AST mapping

Design review, 2026-10-28. This is a proposal, not implemented syntax. Scope is
the eight canonical built-ins plus the separately requested child policies.
Evidence: `src/trance/visual/builtin_patterns_v4.cpp`, native V4 parser,
`docs/spec-grammar-v4.md`, and the two October 28 authoring reviews.

## Recommendation

Use three explicit composition forms: `parallel`, `sequence`, and `cycle`.
Keep braces for composition ownership and semicolons for output leaves.
No implicit wrapper insertion, leaf timing sugar, general arithmetic, variables,
functions, macros, or derived public scripting model. Singleton parallel wrappers
are meaningful: they own cadence and clocks. Super Fast's chance-exit owner must
also survive unchanged.

The concise surface below was agreed with the compact reviewer. `cycle` is a
narrow spelling of the only selector configuration used by a built-in, not a
new scheduler. Unknown or unsupported AST properties must make formatting fail;
never silently drop them to fit the minimal surface.

| Text | Existing JSON AST |
| --- | --- |
| `v4;` | `version:4` |
| outer composition | `root` |
| `parallel`, `sequence` | `kind:parallel`, `kind:sequential` |
| `cycle cooldown N` | `kind:random`, `select:{order:round_robin,allow_self:false,first:0,cooldown:N}` |
| nested composition or output | ordered `children` entry containing `node` |
| `id NAME` | `id:NAME` |
| `for N` / `for sample(A,B,S)` | integer duration / `{min:A,max:B,step:S}` |
| `repeat N` | positive finite `repeat` |
| `every N` | integer `cadence` |
| `every ramp(A,B,S,early)` | cadence ramp object with from/to/steps/ease |
| `phase N` | `offset:N`; same pre-roll, not a delay |
| `exit P every N: COMPOSITION` | child edge `exit:{probability:P,check_every:N}` |
| `image SOURCE`, other media leaf | `op`, `source` |
| `baseline`, `flip`, `chance` | `zoom_baseline`, `flip_probability`, `selection_probability` |
| `visible(A,B)` | `visibility:[A,B]` |
| `animation N` | `animation:{every:N}` |
| `envelope(A,B,C)` | `envelope:{in:A,hold:B,out:C}` |
| `A -> B`, optional `@scene` / `@NAME` | curve with explicit scope local / scene / ancestor NAME |
| `sum(CURVE,CURVE)` | two-element sum, no arithmetic precedence |
| `triangle(A,B,C)` | opacity triangle from/peak/to |

Slash is exact probability syntax, never division. All frame quantities are
integers. Scalar numbers should admit exponents and retain numeric roundtrip
precision. Curve easing is linear only; only cadence ramp needs `early`.
The narrow output vocabulary is image, word, caption, subtext, spiral.
Only actually used property/output combinations belong in this first surface.

Canonical formatting may normalize whitespace, numeric spelling, and object
member order. It must preserve array order and every authored wrapper. The eight
fixtures have explicit local scopes, so the default local spelling lowers to an
explicit `scope:local` and reproduces them structurally without inventing ease
members. Omitted timing and properties stay omitted.

## All eight canonical examples

These are complete proposed documents, preserving the current AST structure.
They are review fixtures, not claimed parser test results.

### Accelerate

```text
v4;
parallel id accelerate for 2048 {
  parallel every ramp(56,12,140,early) for 2048 {
    image alternate zoom sum(0 -> 0.4 @scene, 0 -> 0.1)
      flip 0.5 baseline 0 -> 0.4 @scene animation 4;
    word primary visible(0,0.25) chance 0.5;
  }
  spiral speed 1 -> 4 @scene;
}
```

### Slow Flash

```text
v4;
sequence id slow_flash for 1536 repeat 2 {
  parallel for 1024 {
    parallel every 64 { image primary zoom 0 -> 0.5 animation 2; }
    parallel every 64 { caption primary; }
    spiral speed 2;
  }
  parallel id fast_section for 512 {
    parallel every 8 { image secondary zoom 0 -> 0.8 @fast_section; }
    parallel every 16 { word secondary; }
    spiral speed 4;
  }
}
```

### Sub Text

```text
v4;
parallel id sub_text for 1024 {
  parallel every 64 { image either zoom 0 -> 0.375 animation 3; }
  parallel every 32 { subtext secondary; }
  spiral speed 4;
}
```

### Flash Text

```text
v4;
parallel id flash_text for 1024 {
  parallel every 128 { image secondary zoom 0 -> 0.8; }
  parallel every 128 phase 64 {
    image secondary zoom 0 -> 0.8 opacity triangle(0,1,0);
  }
  parallel every 64 { word secondary; }
  parallel every 32 { caption primary; }
  spiral speed 2;
}
```

### Parallel

```text
v4;
parallel id parallel for 2048 {
  parallel every 64 { image primary zoom 0 -> 0.5 animation 3; }
  parallel every 64 phase 32 {
    image secondary zoom 0 -> 0.5 opacity 0.5 animation 3;
  }
  parallel every 32 { caption primary; }
  spiral speed 3;
}
```

### Super Parallel

```text
v4;
parallel id super_parallel for 1152 {
  parallel every 96 { image primary zoom 0 -> 0.875 animation 2; }
  parallel every 96 phase 32 { image primary zoom 0 -> 0.875 opacity 0.5; }
  parallel every 96 phase 64 { image secondary zoom 0 -> 0.875 opacity 0.33; }
  parallel every 32 { word either; }
  spiral speed 3;
}
```

### Animation

```text
v4;
parallel id animation for 1024 {
  parallel every 64 { image either zoom 0 -> 0.625 animation 1; }
  parallel every 64 phase 48 {
    image secondary zoom 0.5 -> 0.625 envelope(8,16,8);
  }
  parallel every 32 { caption either; }
  spiral speed 3;
}
```

### Super Fast

```text
v4;
parallel id super_fast for 2048 {
  cycle cooldown 64 {
    exit 1/12 every 8: parallel {
      parallel every 8 { image either zoom 0.0625 -> 0.1875; }
    }
    parallel for sample(64,128,8) {
      image either zoom 0.1 -> 0.7 animation 1;
    }
  }
  parallel every 8 { word primary chance 0.25; }
  spiral speed 3;
}
```

`exit` is deliberately an edge prefix: `parallel exit ... every ...` would make
`every` ambiguous between the selector's chance grid and the child's cadence.

## Narrow authorized extension: child completion

One optional composition clause `children independent|loop|wait` maps to one
`child_mode` enum. Independent is omitted by default. These modes are mutually
exclusive, and are for parallel and sequence, not an opportunity to expand cycle.

- Independent preserves all eight existing schedules. A finite child ending
  before its owner can produce an inactive-tail warning, not an implicit repeat.
- Loop repeats the entire child sequence for sequence; for parallel it restarts
  each finite child independently. The explicit owner boundary clips work.
- Wait gives finite child compositions authority over the iteration end: sum
  for sequence, maximum for parallel. Direct leaves follow their owner and do
  not gate completion. Require at least one finite child composition; omit `for`
  on the wait owner to avoid two conflicting duration authorities.

Wait is a native semantic extension, not text desugaring or a browser calculation.
The final review boundary is deliberately static: disallow the wait owner's own
duration, cadence and phase, and initially its own repeat. Require statically
exact finite child completion; reject sampled duration anywhere in the gating
subtree, chance-exit-dependent completion, and unresolved/unbounded lifetimes.
No predictive sampling or extra random draws occur.

Use effective completion, not simply authored durations. A sequence capped at
2000 with two 600-frame children finishes at 1200. For an ordinary finite
sequence, resolve min(cap,sum(effective children)); cadence-controlled finite
owners use their cap. An independent parallel owner uses its explicit cap.
Include finite child repeats and recursively resolved wait nodes. Loop children
need their explicit owner boundary. Retain separate authored modulation duration
where existing independent sequence semantics require it; early completion must
not silently change its curves. A wait owner uses its resolved effective span for
its occurrence clock. Native trace tests must establish correspondence, including
the nonzero-phase descendants that remain permitted.

These validation bounds are a proposed first implementation contract, not an
assertion that the current engine supports wait. They deliberately leave dynamic
wait durations and owner phase/cadence interactions outside the initial scope.

## Parser and runtime boundary

The native compiler remains the sole scheduler. Text parsing produces the
existing authored JSON AST, which then uses the current semantic parser/compiler.
Do not lower straight into render statements: that loses authored wrappers and
makes browser roundtripping depend on reverse engineering compiled effects.

For the current static browser composer, a small JS syntax parser/formatter and
a corresponding native syntax frontend are the pragmatic boundary. Both implement
the same small syntax and share exact AST/text fixtures. Syntax duplication is
not scheduler duplication. Browser validation can flag simple shape errors but
must not claim native timing validity. A shared WASM syntax implementation is a
future packaging alternative, not a prerequisite that justifies a new build stack.

Required acceptance evidence: each canonical AST formats and parses back to deep
equality; each text fixture lowers through the native compiler and matches the
original schedule/render trace under the same seed; browser/native syntax agree
on valid and invalid fixtures. Separately test completion policies at boundaries,
especially loop clipping, wait leaf ownership, sampled draws, and phase clocks.

No production edits, tests, commits, or publishing were performed for this review.
