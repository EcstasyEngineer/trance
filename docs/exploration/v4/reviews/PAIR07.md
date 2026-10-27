> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Pair 07: Parallel (Simple) and Super Parallel

Independent review of the proposed three-kind AST, restricted adapter, and current
production recipe sources. This review owns only this report; fixes below were
dispatched to the grammar implementer.

## Verdict

Both recipes fit parallel composition with cadence properties and output leaves.
Neither needs an exclusive selector, named controller, authored image register,
or extra composition kind. This validates their structural translation, not a
complete native V4 runtime or frame-identical randomized content.

| Property | Parallel / Simple | Super Parallel |
| --- | --- | --- |
| Root duration | 2048 frames | 1152 frames |
| Image timing | every 64 frames | three 96-frame lanes, offsets 0/32/64 |
| Text timing | primary caption every 32 frames | either-theme word every 32 frames |
| Image motion | local zoom 0 to .5 | each lane local zoom 0 to .875 |
| Image opacity, in stack order | 1 | 1, .5, .33 |
| Animation accent | every third selection attempt | every second selection attempt on first lane only |
| Spiral | speed 3 | speed 3 |

The checked-in `simple.json` corresponds to the production UI recipe named
Parallel; the C++ constant and source pattern retain the older Simple name.

## Actual checks

- Imported both generated ASTs, validated them, lowered them, and sent both
  lowered sources to the actual Release `trance.exe --lint` through the authoring
  server helper. Both passed.
- Examined lowering: each image cadence gets a separate nested V3 pattern,
  preserving private held-image storage. Source order remains back, middle,
  front, text, spiral. Offsets remain inside each lane's `every`.
- Inspected V3 parser ownership: image state belongs to the nearest pattern;
  cadence alone does not create independent image ownership. The adapter rejects
  two direct image leaves in one V3 pattern rather than silently aliasing them.
- Ran the current seven `test_pattern_v4.py` contract tests after dispatched
  fixes. All passed, including regression counterexamples.

For an uninterrupted first occurrence, the Super Parallel selection boundaries
are 0/96/192..., 32/128/224..., and 64/160/256..., respectively. Each lane selects
12 times before the root cutoff at 1152; the offset lanes' final occurrences are
clipped by that cutoff. This is a source-derived schedule, not a recorded render
trace. Simple has 32 image selections and 64 caption selections per scene.

## Counterexamples found and fixes dispatched

1. **Generated root clock collision.** A root without an ID and a child explicitly
   named `v4_scene` originally lowered both to the same V3 clock name. A scene
   curve in the child could then resolve to the nearer child. The implementer
   changed root allocation to avoid every authored ID. Verified reproduction now
   produces `v4_scene_root` for the root and refers to that distinct clock.
2. **Alias and lexical collisions.** Validator IDs originally allowed `scene`,
   `local`, `this`, `self`, and Unicode Python identifiers, despite special clock
   aliases and V3 lexical restrictions. The implementer now rejects reserved
   aliases and requires ASCII identifiers. Verified `scene` and `café` reject.
3. **Cadence destroys sequential lowering.** A valid sequential node with two
   eight-frame children and cadence 16 originally lowered as a V3 sequence
   containing one `every 16f` whose children run together. Actual compiler lint
   accepted that incorrect translation. The implementer now rejects
   sequential-plus-cadence in the restricted adapter. This AST remains valid V4;
   generic native occurrence scheduling must implement it faithfully.

The third counterexample is particularly important: parser acceptance is not
enough to establish arbitrary nesting semantics. All nine nesting pairs passing
AST validation demonstrates legal structure only.

## Content and resource limits

`either` means a fresh theme-side choice at selection time; it does not choose a
clock. For Simple's image, the adapter uses a fair alternate flip so still
fallback and animation read one resolved side. This preserves the fair side
distribution while deliberately changing seeded RNG consumption from V3 runtime
selection. Super Parallel's fixed-theme image lanes avoid that difference; its
either-theme words continue using the existing runtime text selector.

Only one animated lane exists in either recipe. These examples therefore do not
prove that several simultaneously animated images have independent decoders.
The existing shared animation stream and live text channels remain explicit
limitations. Adding more nested patterns does not isolate those resources.

The three-kind design is sufficient for these two recipes. Keep automatic image
identity, local occurrence clocks, stable authored accent identity, ordered
stacking, offset activation, and parent clipping as semantic guarantees. Removing
those semantics would simplify the data shape by losing the behavior being
modeled.
