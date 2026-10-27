> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../spec-grammar-v4.md).

# Visual pattern authoring exploration

The main experiment is now the [full grammar drag-and-drop composer](authoring/README.md),
with all eight complete built-ins, movable nested blocks, structured properties,
source import/export and real compiler validation. Run `python tools/authoring_server.py`
and open `http://127.0.0.1:8766/`.

The [earlier fixed-recipe prototype](visual-authoring-prototype.html) remains as
a small visibility/clock demonstrator. It did not satisfy the full authoring
request and has been superseded by the composer. Source syntax is unchanged;
ramp animation/alternation state was corrected following the
[intent audit](authoring/INTENT-FORUM.md). The separately recovered audio-preset
work is restored in the player.

## Recommendation

Use **nested schedule blocks with a separate render-value graph**. Containment
expresses ownership, repetition, sequencing and clipping. Wires express values
and read-only clock bindings. A distinct entry-effects rail shows media selection
and other mutations. Preserve render order as an explicit layer list.

Treat selection, movement and visibility as separate authoring decisions. A
strobe is a useful demonstration: select a still once, draw it repeatedly through
a visibility gate, and decide explicitly whether the hidden intervals expose an
underlay. A visibility gate alone does not promise a black output.

The existing grammar provides enough primitives to explore this without adding
a `strobe` keyword. It does not provide independently owned animation/text/audio
resources for every nested block. A graph that implies arbitrary independent
media streams would promise more than the runtime implements.

The block interaction the design borrows from is commonly called block-based
programming; [Blockly](https://docs.blockly.com/guides/get-started/what-is-blockly/)
provides a customizable block editor. Blender's
[node model](https://docs.blender.org/manual/en/latest/interface/controls/nodes/parts.html)
uses input/output sockets. These are interaction references, not dependencies
chosen for this implementation. A web editor embedded into the existing native
ImGui application has an integration cost that has not been accepted here.

## The 27 subtasks

Three parallel investigators each completed nine explicit investigations,
exchanged critiques, and produced the following artifacts. These are 27 bounded
subtasks, not a claim that 27 agents ran simultaneously.

| Tasks | Investigation | Deliverable |
| --- | --- | --- |
| 01–09 | Grammar/AST inventory; ownership/clocks; state lifetime; effect/render order; media contracts; composition failure matrix; executable probes; LLM contract; visual IR | [Grammar and composition](01-09-grammar-composition.md) |
| 10–18 | Prior strobe evidence; existing-grammar recipe; still/animation behavior; underlay semantics; timing quantization; nested behavior; executable tests; controls/preview; recommendation | [Strobe investigation](10-18-strobe.md) |
| 19–27 | Author workflows; blocks; nodes; hybrid choice; types/state; round-trip contract; scenarios; working prototype; staged plan | [Visual authoring design](19-27-visual-authoring.md) |

## What the exploration established

- Short nested schedules repeat within their owner and reset at owner re-entry;
  the owner clips unfinished children. Reading a different clock does not move a
  schedule into that clock's ownership.
- Entry effects and render layers have different ordering rules. Parent entry
  setup runs before child entry even when the parent's statement follows the
  child in source; render layers retain lexical ordering.
- Nested patterns isolate image-register names, while sibling cadences share
  their enclosing pattern's register scope. Two innocent-looking default `cur`
  assignments can therefore collide.
- Offset pre-roll suppresses media effects. A phased draw can initially refer to
  an empty register: phase shift is not media preloading.
- Strobe recipes exercise the real parser, compiled effects and render dispatch
  with recording APIs. This establishes draw requests and selection counts,
  rather than physical display brightness or animation decoder behavior.
- Parser acceptance is too permissive to serve as the whole editor contract.
  The [diagnostic probe](../../tests/exploration/grammar/diagnostic_probe.cpp)
  records accepted trailing input, misspelled clock attributes and other cases;
  [captured results](../../tests/exploration/grammar/diagnostic-probe-results.txt)
  distinguish those findings from desirable acceptance behavior.
- Snapshot storage does not guarantee snapshot display across animation-only
  theme swaps. The concrete renderer can substitute the current lane frame for
  an old captured image. This is source-observed and requires a targeted
  integration reproduction before being called a verified visual regression.

## Validation

The fresh upstream source was rebuilt with the supported Windows Release preset
and tests enabled. The six original CTests passed, including actual execution
of the GPU-labelled ThemeBank tests, and isolated live playback/MCP replacement
QA passed. After adding exploratory coverage, the full seven-test suite passed.

```powershell
cmake --preset windows-msvc -DTRANCE_BUILD_TESTS=ON
cmake --build --preset windows-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
python tests/qa_playback_progress.py build/windows-msvc/Release/trance.exe
```

The new `strobe_composition_test` target reads the example recipes under
[`tests/exploration/strobe`](../../tests/exploration/strobe). The expanded
`phase_execution_test` checks scope, entry/render ordering and offset clipping.
The diagnostic probe is deliberately outside CTest: permissive behavior is a
finding to fix, not a contract to freeze.

Prototype browser QA uses Playwright with installed Edge. Provide Playwright via
the normal Node module resolution path (or `NODE_PATH`), then run:

```powershell
node tests/exploration/visual_authoring_qa.cjs build/windows-msvc/Release/trance.exe
```

This checks real line breaks, supported source round-trips, visibility, clock
binding, rejected draft preservation, invalid controls, clipped timelines,
mobile layout and browser errors. With the executable argument it also lints
18 generated selector combinations using the real player. Browser trace math is
a model of the advertised subset, not a replacement for compiled execution.

Final exploration checks passed: seven CTests after the sixth strobe fixture,
526 recorded strobe ticks with repeated output passes, browser QA with 18 real
linted candidates, and whitespace/diff checks. The prototype was also rendered
and visually inspected. Its default static trace agrees with the compiled
recorder (five total selections including the backdrop, sixteen windows and
sixty-four positive-alpha foreground ticks). The browser check additionally
verifies that dirty drafts disable structural edits and unsupported numeric
precision cannot emit exponent literals the grammar does not support.

No physical headset, refresh-rate accuracy, real-media perceptual equivalence,
or complete grammar round-trip was established by these checks.

## Next work / handoff

1. Harden validation: require complete source/expression consumption, validate
   names and arity, reject non-finite results and invalid selection ordinals.
   Return structured source diagnostics shared by text, visual UI and MCP.
2. Add a bounded compiled preview/trace service with explicit media and random
   inputs. Use its trace as the authority for the visual timeline.
3. Specify a source-preserving authoring document with stable IDs, ownership,
   ordered effects/layers and typed clock references. Keep unsupported source
   intact; avoid reverse-engineering a visual document from the lowered runtime.
4. Implement the small editor subset represented by this prototype inside a
   deliberately chosen UI boundary. Expand only after actual authoring sessions.
5. Reproduce and decide snapshot/animation ownership semantics before advertising
   independently reusable animated layers. Add integration coverage for the
   agreed behavior.

These are proposed follow-ups. This exploration does not silently ship those
changes, migrate saved sessions, restore obsolete upstream history or publish a
release.
