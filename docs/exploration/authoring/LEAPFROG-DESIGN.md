> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../spec-grammar-v4.md).

# Leapfrog image composition: design review

This is the earlier design review. Its replacement decision is superseded: Flash
Text now uses scoped image lanes and authored image handles have been removed.
See [current execution and migration evidence](LEAPFROG-EXECUTION.md).

This review separates the user's visual intent from the existing copy/register
implementation. Source inspection covers `builtin_patterns_v3.cpp`,
`pattern_parser_v3.cpp`, `cyclers.cpp`, `compiled_visual.cpp`, and
`render_eval.cpp`. Mathematical claims below concern ordinary source-over
compositing. Runtime fixture results are recorded separately by the execution
reviewer. The scoped-lane example below passed production parser/compiler lint;
its runtime equivalence remains subject to the separate trace.

## Decision

The user's two-lane hypothesis is useful: alternating image lifetimes can remove
the explicit outgoing-image copy. Two **identical opacity curves**, however,
cannot implement a constant-coverage dissolve in the current fixed source order.
Use an opaque lower lane and a fading upper lane instead, replacing each lane
only while the other hides its replacement. Keep timing and image ownership
separate, and do not impose one draw on every generic schedule container.

The existing pattern scope already supplies independent image storage. That
offers a smaller first experiment than introducing a special transition keyword,
dynamic layer sorting, another blend mode, or a second authoring language.

## Why two identical fades are insufficient

For lower image A with opacity a and upper image B with opacity b, ordinary
source-over produces these weights:

```text
A:          a * (1 - b)
B:          b
background: (1 - a) * (1 - b)
```

If the two fades are complementary and both have opacity 0.5 at the midpoint,
the weights are A=0.25, B=0.5, background=0.25. This is a dip in coverage and a
biased blend, not the intended equal dissolve. It is not repaired by adding an
offset. With a=1 for the lower lane, the weights become A=1-b, B=b and background=0.

Changing to additive or normalized blending could make two symmetric fades
work, but changes renderer semantics and introduces color/clamping decisions.
Changing draw order whenever a lane refreshes could work with asymmetric
curves, but makes source order cease to describe stacking. Neither is the
minimal repair for an authoring problem.

## A register-free v3 candidate

The following candidate uses existing syntax. Each nested pattern has its own
implicit `cur`; naming the cadences alone would **not** isolate their images.
The exact fixture is `tests/exploration/leapfrog-design/scoped-lanes.v3`; running
`trance.exe --lint --pattern` against it reported `LINT OK`, with all eight
built-ins also accepted. Lint does not prove its per-frame visual behavior.

```text
pattern leapfrog for 1024f {
  pattern back for 1024f {
    every 128f {
      image secondary zoom (curve 0 -> 0.8)
    }
  }
  pattern front for 1024f {
    every 128f offset 64f {
      image secondary fade inout zoom (curve 0 -> 0.8)
    }
  }
  every 64f { word secondary }
  every 32f { caption primary }
  spiral speed 2
}
```

Expected steady-state behavior, to verify against actual compiled clocks:

| Scene frame | Lower lane | Upper lane | Visible handoff |
| --- | --- | --- | --- |
| 64 | A, halfway through life | Select B, opacity 0 | A |
| 96 | A, opacity 1 | B, opacity 0.5 | Equal A/B |
| 128 | Select C, opacity 1 | B, opacity 1 | B hides replacement C |
| 160 | C, opacity 1 | B, opacity 0.5 | Equal B/C |
| 192 | C, opacity 1 | Select D, opacity 0 | C hides replacement D |

Each image moves from zoom 0 to 0.8 across a 128f lifetime. Images are selected
every 64f in aggregate. Underlying lane assignments alternate rather than
copying one latest selection into an outgoing snapshot.

There are meaningful compatibility boundaries:

* **Startup:** offset pre-roll suppresses actions. The upper lane initially has
  no image, so the lower image starts opaque; old Flash Text fades its first
  incoming image from an empty outgoing state. A deliberate startup reveal can
  be authored, but must not masquerade as exact equivalence.
* **Root and branch re-entry:** schedules reset while image contents persist.
  For a 1024f owner, divisible by 128f, the retained upper image is useful: the
  upper clock wraps from local63 to local64, briefly becomes fully opaque, and
  hides the newly selected lower image. An arbitrary nonaligned owner duration
  or interrupted re-entry can instead jump its phase and reveal retained media.
  This needs a stated state-lifetime policy and a re-entry trace. Phase offset
  must not be confused with a fresh empty delayed layer.
* **Theme replacement:** both candidate lanes are live image selections. Current
  runtime automatically refreshes them when theme generations change. The old
  recipe freezes its outgoing `copy` snapshot, so it can intentionally retain
  the prior theme during a dissolve. A no-copy recipe must choose its desired
  theme-swap behavior explicitly.
* **Animation:** still selections are independent storage, but the animation
  stream is shared. This example does not establish independent animated lanes.
* **Half-open frames:** a 128f curve has samples 0 through 127. The triangle
  reaches opacity 1 at local64, which is the critical replacement boundary;
  the final sample is near zero, not exactly zero. Replacement at the next
  local0 is hidden by exactly zero upper opacity.

## What one draw can reasonably mean

A cadence answers **when** actions occur. A layer answers **which persistent
subject** is rendered and **where** it stacks. Those are different roles.
Multiple sibling cadences can write one image; one cadence can change an image,
word and caption together. A scene can contain an image, spiral and text.
Rejecting these because their common parent has more than one draw removes
ordinary composition rather than simplifying it.

An explicit image-layer authoring block can reasonably require exactly one
rendered image subject with one selection policy, one opacity policy, and one
motion policy. A parent scene can own any number of these blocks. Layer-local
implicit image storage removes destination registers from ordinary authoring.
Existing named registers, draw and copy remain useful low-level operations when
the author deliberately needs multiple views or an outgoing snapshot.

The current nearest-pattern register rule already supports the experiment above.
If a new layer construct is added, it should introduce independent storage and
explicit ordered rendering without silently changing schedule clocks. A bare
keyword that merely looks like a lane while sharing `cur` would repeat the
original confusion.

## Validation required before replacing a built-in

1. Record selection identities, draw identities, opacity and zoom across at least
   three lane periods; prove replacement happens only when hidden.
2. Check source-over effective weights, not only individual alpha curves.
3. Check the first frame, first offset firing, root wrap, and branch exit/re-entry.
4. Switch theme generation in both halves of the dissolve; distinguish frozen
   outgoing content from live refresh and choose the intended contract.
5. Run all eight built-ins after any parser/scoping change. In particular,
   Super Parallel and Animation already require distinct image identities; Flash
   Text is not the only pattern with register-backed multi-image composition.
6. Ensure a second render pass does not select again or change lane state.

The simplest useful outcome is a tested two-lane recipe, an authoring surface
that owns each lane's storage automatically, and a clear startup/re-entry choice.
An unvalidated universal one-draw restriction or an invisible blend-mode change
would add uncertainty rather than resolve it.
