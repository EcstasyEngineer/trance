# V4 grammar and value-model elegance review

Reviewed 2026-10-28. Analysis only; production sources are unchanged. This is a
design and correctness review, not a proposed language contract.

## Findings first

- **V4 is currently an authored JSON AST.** There is no consolidated text syntax
  or V4 text parser/serializer in the production path. `.pattern` is a filename
  convention, not a text-language discriminator.
- **Authored expressions are already excluded.** Numbers, structured curves,
  sums, triangles and envelopes lower to internal expression strings. The
  remaining general expression evaluator is an implementation detail inherited
  from the previous architecture.
- **A real scalar-lowering bug is verified.** Tiny constants can become fully
  opaque values because generated scientific notation is misread. This deserves
  a focused correctness issue independently of any grammar redesign.
- **Offset is used by four built-ins**, not only Flash Text. Removing it globally
  would change Parallel, Super Parallel and Animation as well.

## Current path and authority

[`spec-grammar-v4.md`](../spec-grammar-v4.md:3) describes the current authored
contract. [`pattern_parser.cpp`](../../src/trance/visual/pattern_parser.cpp:5)
routes JSON to the native frontend and rejects V3 text. The native frontend
validates and produces occurrence nodes, effects and ordered render statements;
[`pattern_compiler.cpp`](../../src/trance/visual/pattern_compiler.cpp:24) builds
the runtime tree. The browser's `parse`, `validate`, `text` and source editing
all operate on the same JSON shape, although browser validation is intentionally
incomplete.

The historical expression review, experimental schema and old roadmap must not
be treated as current authority. In particular,
[`authoring-roadmap.md`](../authoring-roadmap.md:1) is marked archived and links
historical issues #63, #64 and #66; their live issue status was not fetched in
this subreview. The experimental schema still names a removed Python semantic
validator in its description. The native implementation and current V4 spec
are the appropriate basis for new tracking work.

## What the eight built-ins actually need

Census taken from `tools/authoring_server.py::builtins_v4()`, which extracts the
canonical C++ sources, rather than from historical translations.

| Built-in | Structured value features | Offset |
| --- | --- | --- |
| Accelerate | Four linear curves; local and scene clocks; one two-term zoom sum; cadence ramp uses `early` easing | None |
| Slow Flash | Two linear curves; one reads `fast_section` while cadence changes images | None |
| Sub Text | One local linear curve | None |
| Flash Text | Two local linear curves; upper image has one triangle opacity | 64 on cadence 128 |
| Parallel | Two local linear curves | 32 on cadence 64 |
| Super Parallel | Three local linear curves | 32 and 64 on cadence 96 |
| Animation | Two local linear curves; one integer-frame envelope | 48 on cadence 64 |
| Super Fast | Two local linear curves; chance exit and sampled hold duration | None |

There are 18 structured curves: 14 local, three scene, one named ancestor. All
18 use the default linear easing. This does not justify deleting non-linear
curves from the public language, but it does justify keeping them secondary in
the authoring UI. It also shows why removing sums or ancestor clocks outright
would alter shipped effects. The one cadence ramp genuinely uses non-linear
easing, independently of value curves.

Exact anchors: sum at
[`builtin_patterns_v4.cpp:22`](../../src/trance/visual/builtin_patterns_v4.cpp:22),
ancestor curve at
[`165`](../../src/trance/visual/builtin_patterns_v4.cpp:165),
triangle at [`301`](../../src/trance/visual/builtin_patterns_v4.cpp:301),
envelope at [`577`](../../src/trance/visual/builtin_patterns_v4.cpp:577).

## Four concrete opportunities

### 1. Keep values typed through rendering

Before: JSON number/curve -> string construction in `Parser::scalar` ->
per-frame recursive-descent expression parsing -> number. Boolean render guards
and frame envelopes also become strings. The evaluator implements precedence,
ternaries, boolean operators, comparisons, power, modulo and function calls that
authors cannot use in V4.

After: a small internal value variant for constant, curve, sum, triangle and
envelope, with clock references resolved during compilation; structured render
guards for active state, visibility and selection flags. This keeps the existing
authored contract while allowing the general text evaluator to be retired once
all its internal callers have moved.

The current evaluator occupies roughly lines 24-266 of the 306-line
[`render_eval.h`](../../src/trance/visual/render_eval.h:24). The scalar emitter is
roughly lines 127-177 of the 515-line
[`pattern_parser_v4.cpp`](../../src/trance/visual/pattern_parser_v4.cpp:127).
These are measured existing spans, not a promised net LOC reduction. A replacement
must include guards and envelopes to avoid retaining two evaluators indefinitely.
Compile clock handles once if feasible; do not introduce a general user expression
tree or a new scripting language merely to remove expression strings.

**Verified correctness defect motivating the boundary:** `num(double)` uses
17-digit default stream formatting, which can emit an exponent, while the
evaluator's numeric scanner accepts only digits and decimal points and does not
require full consumption.

| Authored opacity | Generated internal string | Actual evaluator result |
| --- | --- | --- |
| `0.00001` | `1.0000000000000001e-05` | `1` |
| `0.0000001` | `9.9999999999999995e-08` | `10` |
| `100000000000000000000` | `1e+20` | `1` |

This was tested using the actual `pattern_parser_v4.obj`, `cyclers.obj`, and
`render_eval.h` in a small MSVC harness. No substitute evaluator was used. The
normal product lint accepts all three without warnings; it samples results and
checks magnitude, rather than checking exact numeric preservation.
Relevant source: [`num:62`](../../src/trance/visual/pattern_parser_v4.cpp:62),
[`numeric scanner:210`](../../src/trance/visual/render_eval.h:210),
[`run:106`](../../src/trance/visual/render_eval.h:106),
[`lint:1517`](../../src/trance/main.cpp:1517).

The narrow repair is to make internal numeric serialization and numeric parsing
agree, with exact-output regression cases for constants and curve endpoints.
It need not wait for typed-IR cleanup. The local probe source and executable
remain in the task's temporary `trance-scalar-audit-4jczo1tu` directory; the table
above is the durable result, not a dependency on that directory surviving.

### 2. Add concise text as a view over the same AST

Before, a simple leaf uses wrappers such as
`{"node":{"op":"image","source":"primary","zoom":{"curve":{"from":0,"to":0.5,"scope":"local"}}}}`.

A syntax sketch, deliberately not a committed grammar:

```text
parallel for 128f {
  image primary zoom 0 -> 0.5
}
```

The basic curve form should carry its default local clock and linear easing
implicitly. Optional clock/easing clauses should appear only when needed.
Retain explicit sum, triangle and frame-envelope forms for the few built-ins
that need them, without requiring arithmetic precedence or bracket expressions.
Retain AST edge metadata for random exits; surface it as an attached child clause
rather than exposing `node` objects to an author.

Implement one parser to the existing AST and one canonical formatter from it.
The composer and runtime should keep that AST as their interchange form. Avoid a
second scheduler, new timing meaning, compatibility with V3 expressions or a
special syntax for every built-in. Required evidence is AST -> text -> AST
equivalence and matching native schedule/render traces. Settle completion policy
and the user's precise curve examples before freezing a grammar.

### 3. Consolidate editor output capabilities

Before: output capabilities occur in the JS `properties` list, browser validation,
the inspector and native per-output checks. One actual drift example is that
native `visibility` is accepted on an image, but `G.properties({op:'image'})`
does not offer it in the Add property menu. The browser also offers sums for
opacity, while the recursive native sum parser intentionally passes `triangle=false`
to its terms: a triangle cannot be nested in an opacity sum.

After: one small declarative editor capability map drives Add property,
compatible modes and basic checks. Keep native validation authoritative and use
small positive/negative compiler cases to check the map. This is a focused table,
not a framework to generate the entire compiler from JSON Schema.

Anchors: [`v4-grammar.js:62`](authoring/v4-grammar.js:62),
[`v4-app.js:27`](authoring/v4-app.js:27), native
[`visibility:312`](../../src/trance/visual/pattern_parser_v4.cpp:312) and
[`sum recursion:144`](../../src/trance/visual/pattern_parser_v4.cpp:144).

### 4. Generate the static built-in catalog at packaging time

Before: the 789-line static browser catalog stores both source text and parsed
AST, duplicating the canonical 722-line C++ source. The local server extracts
from C++; the static site builder merely copies the checked-in cache.

After: reuse the existing extraction function during static packaging and emit
the browser catalog from canonical C++ sources. It is a small change to the
packaging boundary, with no runtime redesign. The browser can parse each source
once if maintaining both source and AST in the transport ceases to be useful.
Keep a parity test either way. The copies are currently equal; this is prevention
of drift, not a claim that deployed built-ins differ.

Anchors: [`authoring_server.py:26`](../../tools/authoring_server.py:26),
[`build_composer_site.py:20`](../../tools/build_composer_site.py:20),
[`authoring_server_test.py:42`](../../tests/authoring_server_test.py:42).

## Flash Text: opacity, phase and a no-black contract

Flash Text is indeed the two-image dissolve. The lower lane uses default opaque
opacity. The upper lane uses triangle opacity, offset 64 frames within a
128-frame cadence. Native recording-renderer tests distinguish it from Parallel
and Super Parallel. Offset pre-roll advances clocks while suppressing selections;
it does not preload the upper image or insert an inactive delay.

For full-frame images with ordinary source-over blending, an opaque lower image
and upper opacity `t` already produces `(1-t)A + tB`, with full coverage. Fading
the lower image to `1-t` while fading the upper to `t` produces coverage
`1-t(1-t)` over a black background: only 75% at the midpoint. Opposing opacity
curves alone therefore do not establish the requested absence of black.

A simpler one-transition dissolve can use an opaque base, a top-image ramp and
an explicit handoff at the end. Repeating that forever requires defined ownership
of incoming/outgoing captures, selection cadence and boundary behavior. That is
different from the present two independently refreshing, phase-shifted lanes.
Treat a new transition recipe or primitive as a design option to compare against
the clarified desired appearance, not as a proven drop-in simplification.

Offset's other uses are at
[`Parallel:403`](../../src/trance/visual/builtin_patterns_v4.cpp:403),
[`Super Parallel:482`](../../src/trance/visual/builtin_patterns_v4.cpp:482) and
[`505`](../../src/trance/visual/builtin_patterns_v4.cpp:505), and
[`Animation:586`](../../src/trance/visual/builtin_patterns_v4.cpp:586).
Renaming phase separately from a future true delay may improve clarity; removing
phase outright is not supported by this census.

## Verification and remaining decisions

Passed during this review: `node tests/authoring_v4_test.cjs`, existing Release
`v4_parser_test.exe`, and `v4_native_execution_test.exe`. The latter exercises the
production parser, compiled visual and recording renderer, including 4,225
Parallel frames, distinct Flash Text/Super Parallel behavior, private captures,
coherent image/animation sides and immutable second rendering passes. Also
verified exact static-catalog parity with native source extraction and the scalar
counterexamples above. Existing binaries were used; no full rebuild was claimed.

These results do not establish decoded media, GPU pixels, wall-clock timing or
physical headset behavior. The blend algebra describes standard full-frame
source-over composition; black pixels inside media or uncovered geometry are a
separate matter.

The supplied text wall and crossfade clarification are now incorporated in the
[review handoff](authoring-review-2026-10-28.md). Before freezing a consolidated
grammar, review concrete curve/syntax examples and decide explicit versus
inferred completion independently of formatting. Compare the desired Flash Text
appearance with the current recipe. Keep the
useful small value vocabulary; do not restore arbitrary expressions without a
specific effect that cannot be expressed cleanly otherwise.
