> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../spec-grammar-v4.md).

# Grammar composition investigation: tasks 01–09

Baseline: fresh upstream `0822c53`. This is an exploration report, not a new
language specification. Production behavior was not changed. Source names below
refer to `src/trance/visual/` unless otherwise stated. Headless probes establish
parser, scheduler and expression behavior; they do not establish displayed pixels,
decoded media coverage or user comprehension.

## 01. Inventory the grammar and its actual intermediate data

**Finding:** a hybrid visual authoring system is feasible without replacing the
runtime. Its first version should generate existing v3 text. The current language
already separates schedule ownership, selection effects and per-frame rendering.
It does not provide a reusable function/module language or arbitrary graph runtime.

| Author form | Lowering | Meaning to preserve in a visual editor |
| --- | --- | --- |
| `pattern NAME for Nf` | `Node::Phase` | Finite owner; parallel child schedules; entry effects |
| `seq` | Phase containing `Node::Seq` | Child schedules in order; entry effects do not consume sequence time |
| `loop N` | `Node::Rep` around pattern | Repeat the same schedule and stateful compiled actions |
| `every Nf` | Action when leaf-only, Phase when nested | Repeated local occurrence with clipped final cut |
| `every ... offset Nf` | `Node::Off` wrapping cadence | Phase shift using suppressed pre-roll; not delayed visibility |
| `every ramp ... steps N` | Sequence of separately parsed segments | Compile-time expansion, segment-local clocks and register scopes |
| `burst` | Controller plus base/burst Phase children | Conditional interruption; sampled local duration; inactive branch stops |
| `image`, text, animation, audio, `copy`, look | Ordered `Effect` list | Entry-time mutation and selection |
| `draw`, image/text params, spiral, warp, volume | Ordered `RenderStmt` list | Per-frame drawing/driving, with activation guards |

Evidence: `pattern_ast.h` (`Node`, `Effect`, `RenderStmt`),
`pattern_parser_v3.cpp` (`parse_pattern`, `parse_cadence`,
`parse_ramp_cadence`, `parse_burst`, `parse_draw`),
`pattern_compiler.cpp` (`compile_impl`). `Node::Par` and other AST details are
internal compiler capabilities, not extra v3 keywords. No arbitrary `if`, user
variables, function parameters or general-purpose `for` statements exist.

The parser adds a OneShot initialization sibling that changes themes, font and
spiral. A visual editor should identify this generated setup separately from
authored effects; otherwise its trace will seem to perform unexplained work.

**Decision:** use owner/container blocks for schedule control, typed value links
for modulators and resource references, and an explicit ordered layer list. A
single undifferentiated node graph hides too many of these distinctions.

## 02. Separate schedule ownership from clock selection

**Finding:** reading an ancestor changes the sampled time value, not the lifetime
of the operation that reads it. A 25-frame pattern with an 8-frame child selects
at frames 0, 8, 16 and 24; the last child is clipped. At frame 25 the next owner
iteration begins, and its child restarts. A curve `over scene` continues across
the four child cuts within an iteration, then restarts with the scene.

Evidence: `PhaseCycler::advance/reset/restart`, `Parser::resolve_clock`,
`Parser::parse_pattern`; existing execution tests `bounded_pattern_clocks`,
`nested_cadence_ownership`, `phase_clock`.

| Clock | Known duration? | Suitable inputs |
| --- | --- | --- |
| Finite pattern/cadence | Yes | Frame, fraction, envelope and curve |
| Sampled burst branch | At entry, not fixed at parse time | Fractional envelope and normalized curve |
| Base branch | No interruption endpoint | Elapsed frame, explicit finite ancestor or finite child |
| Burst controller inside sampled/indefinite owner | No fixed enclosing endpoint | Frame/index; normalized reads must choose another bounded clock |

Frames are half-open: the first displayed sample is 0 and the last is N-1.
Progress reaches `(N-1)/N`, not 1. A visual preview drawing an endpoint at exactly
1 must distinguish the ideal curve endpoint from an actual schedule sample.

**Proposed interaction:** each curve has a visible clock chip: `this cut`, `held
burst`, or a named ancestor. Owner outlines determine activation and clipping.
Clock cables are read-only and must not imply the ability to reset/seek ancestors.
Moving a block across an owner boundary should preview the changed clock, clip
boundary and scope; it is a semantic edit, not cosmetic grouping.

## 03. Account for state lifetime and re-entry

**Finding:** restarting a phase resets its child schedules, not the entire
visual's state. This is desirable for crossfades and alternating selections, but
means nesting alone does not provide fully encapsulated components.

| State | Storage/lifetime | Composition consequence |
| --- | --- | --- |
| Schedule positions | Cyclers, reset by owner | Re-entry starts local frame at zero |
| Images, source side, generation stamp | `pattern::Registers` per CompiledVisual | Persist until overwritten; ordinary cadence shares parent's registers |
| Copied image | Same registers, generation stamp erased | Stored snapshot persists across theme refresh |
| Alternate/chance/animation counters | Hidden scalar registers | Persist across schedule reset; statement-local generated identities |
| Animation source side | One `Registers::anim_slot` | Last animation selection controls every anim draw |
| Main text/subtext/caption | Shared API paths | Nested patterns do not create text registers |
| Theme audio | One API channel | Child audio replaces parent; lexical exit does not stop it |
| Compiled action divider | Captured counter in compiler lambda | Resetting Cycler does not recreate lambda/counter |

Evidence: `pattern_runtime.h`, `CompiledVisual` constructor, `run_effect`,
`compile_impl` Action case, `PhaseCycler::reset`,
`Parser::parse_alternate/parse_anim/qualify_reg`.

Ordinary cadence and burst branches share the nearest pattern's register scope.
Nested patterns isolate bare `cur`; explicit `Scene.cur` can mutate the ancestor.
Ramp segments are an exception: each gets its own clock AND register scope. A
transform from fixed cadence to ramp therefore changes state identity as well
as timing. An alternating statement copied into each ramp segment starts with
separate toggle state; it is not one alternator traversing a changing cadence.

**Proposed component contract:** declare persistent state, per-entry clocks,
imported/exported still registers and shared-path writes separately. Do not
promise component-local animation/text/audio until the runtime can provide it.

## 04. Preserve three different orders

**Finding:** lexical placement, effect execution and layer compositing are not
one interchangeable order.

1. An owner's direct effects run once at owner entry, before its child schedules.
2. Each owner's effect list retains the order among its direct statements.
3. The flattened render list follows lexical traversal/source order.

For example:

```text
pattern scene for 12f {
  pattern child for 4f { image primary zoom 0.25 }
  image secondary zoom 0.75
}
```

At frame zero the scene selects secondary first, then child selects primary.
The child draw is earlier in the render list; scene's secondary draw is later.
These stills use distinct `cur` registers because a nested pattern creates scope.
Two ordinary sibling cadences using bare `cur` instead share the same register:
both draws observe its final value for that frame, despite independent schedules.

Evidence: `Parser::parse_pattern` gathers `bare` separately from `children`;
`PhaseCycler::advance` calls `_entry` before advancing children;
`push_render` appends immediately; `eval_render` traverses in list order.
Added execution test: `composition_order_and_scope`.

Warp is a render driver: subsequent image draws use its current setting. It is
not a material object wired exclusively to one image. Audio-volume drivers also
write a shared channel. Multiple active volume curves will resolve by statement
order. Reordering blocks can therefore change more than the visual stack.

**Decision:** show an entry-effect rail, a schedule tree and a layer/driver list.
Resource cables must not determine execution order. Other agent review agreed
that Blender-style topological execution would incorrectly imply independence.

## 05. Check still/animation selection and theme fallback

**Finding:** primary and secondary are two live theme sides. `runtime` selects a
side when an effect fires. `alternate` is statement-local switching state; it is
not a third side and is not a synonym for secondary.

`ThemeBank::get_image` prefers a still from that side's allowed pool, then that
side's animation frame, then the side's last good image. `get_animation` prefers
its animation frame and otherwise latches one still substitute until a selection
request or generation change. Explicit inheritance broadens the selected theme's
pool; it does not authorize unrestricted cross-theme selection.

An image's chance miss retains the existing still register. Its `anim` load and
optional Pulse are emitted as separate unguarded extras, so missing an image
selection does not necessarily suppress animation loading/counting. `anim every
Nth` counts entry selections, not elapsed frames, and still loads animation on
entries whose draw gate chooses a still.

Evidence: `Parser::parse_draw/parse_anim/parse_chance`, `run_effect`,
`ThemeBank::get_image/get_animation/change_animation`,
`VisualApiImpl::render_animation_or_image`, `anim_draw_for`.

**Material display caveat, source-observed:** Copy preserves stored snapshot
bytes and prevents automatic refresh. However, `VisualApiImpl::render_image`
substitutes the current lane's live animation whenever the lane is animation-only.
It has no snapshot flag or source generation argument. A copied outgoing frame
tagged with that side can therefore display new live content after a theme swap.
The source itself documents this crossfade edge. A headless capture test proves
storage preservation, not this API-level displayed-content guarantee. A real
render-level reproduction remains required before changing behavior.

**Proposed UI wording:** `Select theme content`, `Draw live animation`, `Draw
captured image` and `Snapshot` should be distinct concepts. For animation-only
themes, the last two currently need a capability note rather than a promise of
immutable displayed pixels.

## 06. Second-order failure matrix

| Composition | Current consequence | Evidence level / action |
| --- | --- | --- |
| Nested cadence longer than owner | Partial cut; restart at owner boundary | Existing execution assertions |
| Nested cadence inside burst | Stops inactive; restarts on burst re-entry | Existing execution assertions |
| Ancestor curve in short child | Child selects locally, motion reads ancestor | Existing execution assertions |
| Two cadence images both default `cur` | Shared still value, independent schedules | Added parser/compiler/execution assertions |
| Two nested patterns both default `cur` | Separate still resources | Added assertions |
| Move direct owner effect after child in source | Still executes before child entry | Added assertions |
| Offset cadence first appearance | Clock/draw active before first real capture | Added execution assertions; media emptiness inferred |
| Fixed cadence converted to ramp | Extra register/hidden-state identities per segment | Source-observed; user-visible diff needed |
| Nested sibling anim sources | Last load controls both anim draws | Source-observed; strobe agent independently confirmed |
| Chance on image with anim | Still capture can miss while animation load happens | Source-observed; targeted media trace remains |
| Audio under short child | Continues after child exit | Source-observed; explicit stop needed |
| Snapshot across swap into animation-only lane | Stored snapshot can display live replacement | Source-observed; render-level reproduction TODO |
| Raw clock typo | Accepted; evaluates zero | Executed standalone probe |
| Two top-level patterns | First parsed; second ignored | Executed standalone probe |
| Copy before first initialization | No warning if source written later anywhere | Executed standalone probe; path-sensitive diagnostic TODO |
| Noninteger power of negative base | May produce NaN | Executed standalone probe |
| Deeply nested ramps | Multiplicative body expansion | Source-observed cost model; bounded benchmark TODO |

This matrix separates scheduler composability, resource isolation and external
media timing. A valid schedule can still be a poor reusable component.

## 07. Executable stress probes and their limits

Modified `tests/phase_execution_test.cpp` with two targeted cases:

- `composition_order_and_scope`: actual parser, compiler and Cycler advances;
  tests source-order render list versus parent-before-child effects, short child
  repetition and shared/isolated register qualification.
- `offset_capture_and_owner_clipping`: 13-frame owner, 8-frame cadence, 2-frame
  offset; expects selections at 2, 10, 15, 23 over two owner iterations, while
  local render clock follows `(owner_frame + 6) % 8`.

The existing suite already exercises 25/8 nondivisible cuts, 16/6 nested cadence,
ramp nesting, sampled bursts across 8192 ticks, inactive parents, repeated render
evaluation and phase reset. Reusing these tests is stronger than inventing a
second simulator. Its recorder suppresses real media and only emulates minimal
scalar/animation state; it is not the production `run_effect` implementation.

Standalone probe: `tests/exploration/grammar/diagnostic_probe.cpp`.
Recorded output: `tests/exploration/grammar/diagnostic-probe-results.txt`.
It was compiled with the installed MSVC C++17 compiler, `/W3 /WX`, against actual
`pattern_parser_v3.cpp` and `cyclers.cpp`, then executed. It intentionally prints
findings rather than turning permissive behavior into regression requirements.
No new CMake preset or build script was added.

To reproduce the standalone probe after configuring the normal build tree, use
the installed Visual Studio x64 developer shell:

```powershell
cl /nologo /std:c++17 /EHsc /W3 /WX /I src /Fobuild/windows-msvc/tests/ /Febuild/windows-msvc/tests/grammar_diagnostic_probe.exe tests/exploration/grammar/diagnostic_probe.cpp src/trance/visual/pattern_parser_v3.cpp src/trance/visual/cyclers.cpp
./build/windows-msvc/tests/grammar_diagnostic_probe.exe
```

| Probe | Observed result |
| --- | --- |
| `pattern scene for 8f {} trailing_words` | Accepted, zero warnings |
| First pattern then a second top-level pattern with image | Accepted; zero generated draws |
| `copy cur -> prev image primary` | Accepted, zero warnings |
| `image primary zoom [this.progres]` | Accepted, zero warnings |
| `image primary anim every 0th` | Accepted, zero warnings |
| `missing.progress` | 0 |
| `mystery(0.75)` | 0.75 |
| `1 trailing` | 1 |
| `(-1)^0.5` | NaN |
| `1 / 0` | 0 |

The zero animation interval creates a Pulse with nonpositive modulus, which
keeps its gate false; the selection can still load animation. It does not behave
like a useful zero-period cadence and should get an authoring diagnostic.

Integration result reported by the parent after these edits: full supported
Release preset rebuilt and all seven CTests passed, including the changed
`phase_execution_test`. Live playback-progress QA also passed. The parent report
owns command logs and any context-specific GPU/media coverage limits. Do not
infer decoded-media or GPU verification from this standalone probe.

## 08. LLM authoring contract and diagnostics

**Recommendation:** make an LLM submit an explicit intent contract before
generation, and verify the generated result with the same parser/compiler.
Required fields:

```text
Goal: held image with periodic reveal and slow zoom
Owner: scene, 240 schedule frames, repeats
Content: primary side; still preferred; inherited pool allowed
Selection: once per scene entry
Motion clock: scene; zoom 0 to 0.2
Visibility clock: repeated 12-frame child; visible first 4 frames
State: held capture persists; no text/audio/animation-channel writes
Expected trace: one image selection at frame 0; reveal every 12 frames
Fallback: theme fallback as implemented, not an authored black frame
```

The contract distinguishes a desired effect from its implementation and allows
checking that an LLM has not implemented flashing by reselecting media every pulse.
Do not expose internal Set/Toggle/Roll or `render` blocks as author syntax.

Current diagnostics identify parser errors and never-written register reads, but
do not establish definite initialization, full expression syntax, known attribute
names, finite numeric values, full source consumption, or shared-resource safety.
`--lint` samples a few positions; it cannot certify all transition behavior.

Proposed diagnostics, in priority order:

1. Reject unconsumed source and incomplete expression consumption.
2. Reject unknown attributes/functions; report exact authored span and owner.
3. Detect possible reads before a first write, while allowing intentional
   first-entry empty crossfade snapshots with an explicit acknowledgement.
4. Flag competing same-register/same-animation/text/audio writes in overlapping
   scopes, identifying both writers and precedence.
5. Reject zero/out-of-range animation counter values and nonfinite sampled values.
6. Warn that cadence-to-ramp conversion changes register and hidden-state identity.
7. Budget ramp expansion, nesting and total expanded render statements.

Provide an event trace with tick, owner path, entry kind, resolved content side,
register target, selection/copy and active draws. Seeded scheduling randomness
helps reproduce a trace, but asynchronous media readiness and shuffle state need
their own evidence; one RNG seed alone is not complete visual determinism.

## 09. Minimal visual-authoring IR and handoff

**Proposal, not implemented runtime:** use a small authored document alongside
generated v3. Existing lowered `Node`/`RenderStmt` is insufficient to reconstruct
an editable block layout: sugar is expanded, statements split between effects and
draws, source spans are largely absent, and parser-minted ids are not durable.

```text
VisualDocument(version, stable authored ids, source map)
  owners: tree of Pattern | Cadence | Ramp | Burst(Base, Enter, Burst)
  each owner:
    duration/sequence/loop settings
    ordered authored statements and child placements
    explicit resource declarations/references
  statements:
    SelectAndDraw | Draw | Copy | Text | Animation | Audio | Look | Driver
  numeric inputs:
    Literal | Curve(start,end,ease,clockRef) | RawExpression
  visibility:
    Windows(clockRef,halfOpenRange) | RawPredicate
  layout:
    owner/block positions, folded state, optional value-node positions
```

Compile authored statements to existing v3 text, then use `patternv3::parse` to
validate and generate production data. Keep schedule order, entry-effect order
and compositing order explicit in the preview rather than deriving them from
screen coordinates or cable topology. Stable authored ids support diagnostics and
undo; parser ids remain an implementation detail and are regenerated on import.

Do not serialize raw `Node` as the user's durable document. Do not add a second
runtime evaluator for the visual editor. Raw expression and raw source blocks
are necessary escape hatches until structured coverage is complete; preserving
unrecognized text is safer than a falsely successful visual round trip.

**Round-trip acceptance:** for the supported subset, export -> real parse ->
real compiled event trace must preserve selection times, qualified resource
relationships, active render order and evaluated values at transitions. A text
formatter equality check or a picture of connected nodes is insufficient.

**Cross-review:** the visual-authoring investigation agreed that mutation order
must be an explicit rail and moving owners needs a semantic diff. The strobe
investigation independently confirmed the shared animation-selector constraint.
Review of the prototype found that tiny numeric values could stringify in
scientific notation unsupported by the real grammar; its author tightened zoom
precision validation before handoff. This is one reason generated-source
validation must remain part of visual-editor QA.

### TODO / boundaries for the next implementation

- Repeat the integrated supported-preset suite if implementation changes follow
  this exploration; the current additions have passed the integrated run.
- Reproduce animation-only snapshot display substitution through the real API;
  decide whether a snapshot/live distinction belongs in render metadata.
- Add permissive-parser fixes and their negative tests as separate scoped changes.
- Add media recording tests for image chance plus anim/Pulse, and competing sibling
  animation selectors, before offering isolated animation components.
- Benchmark bounded nested-ramp expansion and establish editor/runtime import limits.
- Prototype owner blocks, curve-clock chips and ordered layers using existing v3;
  keep the experiment separate from the product until user trials justify shipping.
- Test at least three real tasks with a power user and an LLM: held pulse, nested
  crossfade and theme-alternating burst. Score trace correctness and editing effort,
  then choose which structural blocks deserve first-class reusable components.
