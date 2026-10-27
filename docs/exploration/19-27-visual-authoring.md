> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../spec-grammar-v4.md).

# Visual authoring: a hybrid schedule and value editor

Exploration against upstream `0822c53`. Tasks 19–27. This document proposes an authoring interface; it does not change the player, grammar, or renderer. The [standalone prototype](visual-authoring-prototype.html) implements a deliberately small subset.

## 19. Beginner, power-user, and LLM workflows

The most useful common object is an inspectable effect recipe with bounded time, explicit state changes, and generated v3 source. The three audiences need different entry points into that same object.

| Audience | Starting interaction | Needed feedback | Completion criterion |
| --- | --- | --- | --- |
| Beginner | Choose “change stills and pulse visibility,” set cut and pulse lengths | Nested owners, plain-language entry actions, static visible intervals | Can explain which image is chosen, when it changes, and what appears between pulses |
| Power user | Add parallel or sequenced lanes; connect named clocks to curves | Owner-relative timeline, effect order, register writes, clipping, source diff | Can distinguish moving a schedule from changing the clock a curve reads |
| LLM | Propose source or a structured edit to a supported recipe | Parser diagnostics, capability manifest, execution trace, exact source and unresolved warnings | Produces lintable source plus assertions about observable behavior, then passes independent runtime checks |

An LLM should not receive an unrestricted canvas serialization as its primary language. V3 already expresses containment concisely. Give it the current grammar, supported visual subset, named owners/registers, and an operation such as “change the zoom curve to read scene rather than cut.” Preserve the exact result and validate it through the same parser/compiler as user-authored text. Treat arbitrary raw expression success cautiously: `render_eval.h::detail::resolve_ident` returns zero for missing names/attributes.

## 20. How puzzle blocks fit

Scratch/Blockly-style containment is a strong fit for the schedule tree. A `pattern ... for` is a bounded container; `seq` chooses serial children; ordinary siblings run concurrently; `every` is a repeated timed container; burst base/enter/burst branches are distinct slots. “On entry” is an ordered action rail inside an owner, rather than an arbitrary statement pretending to occupy sequence time.

Conventional blocks become misleading if they imply ordinary imperative loops. V3 has no author-facing `if`, mutable variable assignment, or general `for` loop. `loop N` repeats a timed pattern, and `show` gates rendering without becoming a scheduling conditional. A friendly label may say “Visible during frames 0–3,” but its expanded source must say `show 0f..4f`; presenting an `if` block would promise semantics the grammar does not implement.

Nested blocks alone make expression-heavy work cumbersome. A deeply nested zoom expression, visibility expression, and ancestor binding should not require widening every owner box. Keep default values inline, expose a selected draw's value graph on demand, and retain a text escape hatch.

## 21. How typed nodes fit

Node graphs fit render values: read a clock, compute a curve, drive a draw parameter. They are a poor sole representation of scheduling and mutation. A spatial cable does not naturally convey re-entry, parent clipping, lexical register scope, per-entry randomness, or the execution order of `copy` and image selection.

Do not imply that Trance already processes arbitrary image textures like a Blender shader graph. V3 has selected still registers and ordered draw statements, alongside shared animation/text/audio state. It does not expose a general image-compositor node language or arbitrary additional theme sides. The initial graph should edit expressions and draw inputs, with composition order visible separately.

## 22. Decision: schedule blocks plus typed value graphs

Use three coordinated views of one parsed document:

1. **Owner tree:** bounded pattern/cadence/branch containers and explicit parallel/sequence lanes. This determines activation and child resets.
2. **Entry-effect rail:** the owner's ordered select/copy/audio/look actions. Entry effects are separate from child schedule order.
3. **Render sheet:** source-ordered draw layers; selecting a layer opens its typed clock/value graph.

| Real v3 form | Visual representation | Critical preserved meaning |
| --- | --- | --- |
| `pattern scene for 192f` | Bounded scene owner | Declared frame budget wins over children |
| `every 48f -> cut` | Cadence contained by scene | Cut clocks reset within each scene iteration |
| `image primary -> still alpha 0` | Select still on entry + associated invisible draw | Alpha zero hides drawing; selection still fires |
| `every 12f -> gate` | Nested gate cadence | Gate restarts at each cut, even if periods do not divide |
| `draw still ... show 0f..4f` | Draw existing register + visibility window | No repeated image selection at gate entry |
| `curve 0 -> 0.5 over cut` | Read cut clock → linear curve → zoom | Clock binding changes interpolation, not ownership |
| `copy cur -> prev` | Ordered snapshot action | Must execute before replacing the outgoing still |
| `seq` | Sequential child lane | Direct owner effects remain entry setup |
| `burst ... { base ... enter ... burst ... }` | State controller with distinct branch slots | Only active branch advances; enter has no timed children |

The grammar group independently confirms that direct owner effects execute before child schedules even if their source appears after a child, while render ordering follows the flattened source order. Therefore a visual editor cannot simply sort all statements by screen position. Source mapping must retain both orderings, or restrict the editable subset to a canonical form and preserve unsupported source unchanged.

**Debate result:** reject a graph-only editor because topological cable order would imply mutation semantics it cannot guarantee. Reject a block-only editor as the entire interface because clock dependencies disappear into nested fields. Adopt the hybrid, while keeping graph editing optional for simple recipes. Drag-and-drop interaction is a later usability mechanism, not the initial proof that the model is useful.

## 23. Typed ports, clocks, state, and semantic moves

These are proposed **editor types**, not additions to v3 syntax:

| Port/state | Valid connections or presentation | Reject or annotate |
| --- | --- | --- |
| `ClockRef` | Enclosing named owner → curve clock | Sibling/private/out-of-scope clocks |
| `Frames`, `Fraction`, `Scalar` | Window endpoints, envelopes, arithmetic | Mixed frame/fraction window endpoints |
| `StillImageRef` | Scoped selected/copy register → draw | Unwritten register; inaccessible sibling scope |
| `Visibility` | Window/expression → draw visibility | Treating visibility as scheduling or selection suppression |
| `ThemeSelector` | Primary/secondary/runtime; alternate where supported | Invented third theme side; alternate as a third pool |
| Shared animation/text/audio | Visible shared-resource badges and last-writer trace | Independent animation streams implied by separate still registers |

Every selected draw should show two distinct labels: “Owned by gate” and “Curve reads scene.” A clock inspector should display frame, declared/sample length, active state, and reset owner. State inspection should show last write, writer owner, selection count, and whether content is a live selection or snapshot. Unknown/random/media-dependent results should be explicit rather than invented thumbnails.

Moving a block between owners is a semantic change. Before accepting it, show a small diff of reset boundary, inherited clock, qualified registers, action frequency, and clipping. Renaming a label must update resolved bindings, not perform textual substitutions inside arbitrary expressions. Duplicating a cadence can add a competing writer; duplicating a named pattern changes scope. A “group these blocks” operation is not automatically behavior-preserving.

Animation deserves a shared-resource warning at the location of the competing selection: current `draw ... anim` consumes shared animation state, not one stream per register. Text and theme audio likewise have shared paths. The grammar investigation also identified a snapshot-display caveat when live animation is involved; storage persistence alone is insufficient evidence of a stable outgoing visual. The editor should not advertise unrestricted crossfade recipes until those actual render paths are validated.

## 24. Text/visual roundtrip

The production editor should parse once into an authoring document that retains source spans, comments, stable document-local node IDs, ordered statements, and resolved owner/clock/register references. Runtime `pattern::Node` plus `RenderStmt` are lowering products, not automatically a lossless authoring AST: they encode execution and can expand authored constructs. Keep editor layout metadata separate from executable grammar and session data until a versioned persistence policy exists.

Roundtrip policy:

- Supported constructs have structured editing, deterministic serialization, and source-to-selection mapping.
- Unsupported but valid constructs remain opaque source islands with clear visual boundaries. Preserve their bytes; avoid structure edits crossing their scope until resolved.
- Raw expression source remains accessible even if the editor cannot graph it. Lint success cannot substitute for full static name/type validation.
- A failed import preserves the user's draft. No silent dropping of comments, chance, audio, bursts, or unfamiliar parameters.
- A dirty source draft pauses conflicting block edits until imported or explicitly discarded.

The prototype intentionally uses a much stricter policy: it imports only its exact canonical template with whitespace variation and supported numeric values. It is not a v3 parser, and it does not claim arbitrary text↔graph conversion. Unsupported text remains editable and intact; structured controls pause until a successful import or Reset. Download requires a successful subset parse.

## 25. Concrete effect scenarios and expected outcomes

| Intent | Existing grammar route | Specific assertion or limit |
| --- | --- | --- |
| Change image slowly while pulsing it rapidly | Select every cut; nested gate draws stored still | Number of selections follows cut count, not pulse count |
| Smooth scene zoom with rapid pulses | Gate-owned draw, curve `over scene` | Visibility still resets with cut/gate; zoom does not |
| Restart motion on each image | Curve `over cut` | At each cut start zoom returns to its authored start |
| Reveal another theme between pulses | Secondary underlay followed by gated primary draw | Off window removes foreground; it does not draw a black layer |
| Hold moving animation during a burst | Burst `enter`/direct selection, draw animation in sampled phase | Validate shared animation writer and sampled branch clock; not separate animation registers |
| Preserve outgoing still for crossfade | Ordered `copy`, new selection, source-ordered draws | Test real media/render behavior; snapshot storage is not full compositing proof |
| Accelerating cuts | `every ramp ... steps ...` within bounded owner | Endpoints shape relative durations; they are not literal first/last segment promises |
| Nominal beat cadence | `beats N`/`locked` with session beat context | Rounded schedule period; no sample-accurate audio-phase binding |

For LLM use, pair each requested effect with assertions like “four selections in 192 frames,” “foreground hidden at gate frame four,” and “switching zoom to scene changes frame 48 zoom without changing selected-image count.” This separates intended behavior from source that merely parses.

## 26. Standalone interactive prototype

Open [visual-authoring-prototype.html](visual-authoring-prototype.html) in a modern browser. It has no network dependencies, media loading, timer-driven playback, or flashing preview. It edits the real syntax for a bounded still-selection/cadence/window/curve recipe. The graph is an inspectable value flow with a selectable clock, not an arbitrary cable editor. Schedule containment is fixed to this recipe; fields make its consequences explorable.

Default emitted source:

```text
pattern scene for 192f {
  image secondary -> backdrop
  every 48f -> cut {
    image primary -> still alpha 0
    every 12f -> gate {
      draw still zoom (curve 0 -> 0.5 over cut) show 0f..4f
    }
  }
}
```

Expected default trace: four image selections, sixteen visible windows, sixty-four visible foreground frames. At scene frame 4 the foreground is absent and backdrop remains. At frame 48 a cut-relative zoom is zero; changing the binding to scene yields 0.125 without changing the gate. Progress uses half-open frames: frame 191 has scene progress 191/192 rather than 1.

Change cut to 50 while leaving period 12 and duration 192 to expose non-dividing cadence resets: gates restart at 0, 50, 100, and 150 rather than continuing a global modulus. A final visible window clips to its cut/scene boundary. Set visible greater than period to see a validation error. Import a `burst` or arbitrary raw expression to see conservative unsupported-source preservation.

The timeline computes this subset locally; it does not invoke C++ and cannot certify media loading, fallback selection, GPU blending, engine clock integration, or all grammar programs. Nominal Hz and seconds are intentionally absent because the file alone does not establish the session schedule frame rate. There is no claim that a layer's absence makes an OS desktop-overlay hole: it reveals lower draws or the cleared render target.

### Verified prototype checks

- JavaScript model checks passed 540 parameter combinations covering short/partial scenes, dividing and non-dividing cuts/gates, all three clock bindings, exact generated-source subset roundtrip, and per-frame agreement between timeline intervals and inspected visibility. Default assertions check sixteen windows, sixty-four visible frames, and the frame-48 cut-versus-scene zoom difference. Inputs with a visibility window longer than the gate, unsupported source, and zoom precision beyond six decimal places are rejected; `0.000001` roundtrips.
- Independent browser checks passed: source roundtrip, dirty-draft preservation, Reset resuming controls, invalid gate/precision handling, manual visibility/clock inspection, and clipped `101/29/11/3` scene/cut/period/visible settings. The 390px viewport had no horizontal page overflow; browser runtime errors were absent. A full-page screenshot was visually inspected.
- Eighteen selector variants generated by the prototype passed the real product's `--lint`. These exercise theme selectors, clock binding and optional backdrop; they are parser/compiler/expression checks, not decoded-media checks.
- The real parser/`CompiledVisual` strobe recorder validated the default source's five total selections (four foreground plus one backdrop), sixteen visible windows, sixty-four accent-visible frames, cut-relative zoom and draw order. The full seven-test CTest suite passed, including the 526-tick strobe/default-equivalence coverage added by the companion investigation.

## 27. Staged implementation and critique

1. **Establish authoring document and trace contracts.** Keep current production UI/runtime untouched. Parse supported fixtures through the real parser, record stable schedule/effect/render associations, and compare the prototype's deterministic subset against C++ execution. Exit when image selections, activation, half-open clocks, resets, and clipping agree across non-dividing boundaries.
2. **Ship a bounded recipe editor.** Still selection, fixed cadences, explicit registers, linear/eased curves, visibility windows, and source export/import. Add keyboard move/add/remove commands before drag mechanics. Exit when users can explain clock ownership and off-window output and unsupported source survives untouched.
3. **Add sequencing and snapshot-aware composition.** Show owner entry rails and draw layer order together; inspect crossfades with actual still and animation-only themes. Exit when source reorder, grouping, and copies have an explicit semantic diff and real rendered-media tests.
4. **Add burst/ramp/chance authoring.** Present branch-local clocks, sampled durations, cooldown, selection randomness, and shared-resource writers. Deterministic replay requires controlled randomness from a real runtime trace contract; the current prototype does not supply it. Exit when traces identify what is predicted versus observed and never present a random sampled future as certain.
5. **Integrate LLM edits and product UX.** Restrict generated structural operations to resolved scope, run lint and execution assertions, and compare source before applying. Test beginner tasks and power-user escape paths. Adopt an additional node library or Blockly only after the document model survives these cases; library choice does not solve clock/state semantics.

The main design risk is adding a second language whose visual appearance promises behavior the engine lacks. The main usability risk is a wall of technical state. Hide advanced inspectors until selected, but keep ownership, chosen clock, and “on entry versus every frame” visible at all times. The prototype's fixed recipe is useful to test those ideas; it is insufficient evidence that arbitrary compositional patterns can already roundtrip or that a complete visual editor is inexpensive.

## Implementation evidence

- `src/trance/visual/pattern_parser_v3.cpp`: `parse_draw`, `parse_params`, `parse_show`, register qualification, named-clock lowering, and authored statement→effect/render split. `image ... alpha 0` still creates an image-selection effect; `draw` does not.
- `src/trance/visual/pattern_compiler.cpp`: `compile_impl` maps phase owners and `every` lowering into concrete cyclers; `Phase` receives entry actions separately from child schedules.
- `src/trance/visual/render_eval.h`: `detail::resolve_ident` and `detail::Eval` implement read-only clock expression evaluation and permissive unknown-name behavior.
- `src/trance/visual/cyclers.cpp` and the phase execution tests are the execution authority for reset/advance behavior. The companion composition and strobe investigations examine those paths independently.
- [Current grammar reference](../spec-grammar-v3.md) and [authoring recipes](../authoring-v3-patterns.md) supply constraints and examples checked against those implementation paths.
