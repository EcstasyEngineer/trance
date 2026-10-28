# Independent implementation review

Focused source review of the new native/JavaScript text frontends, native static lifetime helper, child loop scheduling, capture consolidation, compiler ownership changes and numeric evaluator. No production changes or concurrent builds were made by this reviewer. Parent-owned build and test results are separate evidence.

## Findings sent for repair

1. **Text frontend bounds must precede recursion.** At review time, native `Reader::composition` and both formatters recursively traversed arbitrary nesting without the existing native AST depth bound. The 64-level and four-MiB native checks run only after text has already been parsed into JSON. Deep text can therefore exhaust the stack before the intended rejection. Enforce input size, composition depth and child counts in the frontends; bound formatting too. Test depth-limit success and next-level rejection through the public parser.

2. **Finite subnormal numbers need consistent rendering conversion.** Text `number()` uses `strtod` specifically to retain finite subnormals, while render `Eval::primary()` still used `std::stod`. The latter can throw `out_of_range` on ERANGE for a representable subnormal. Counterexample to exercise: `v4; parallel for 2 { image primary opacity 1e-320; }`. Its numeric value is accepted by authoring and native scalar validation, so rendering must evaluate it without throwing. Use consistent consumed-pointer and finite-value checks, and test actual evaluation. This was a source-derived finding, not a separately executed native probe.

## Checks without further consequential findings

- Static wait lifetime uses effective sequential completion, capped by the owner; loop and cadence owners run to their cap. Finite repeats multiply the effective iteration span. No RNG is sampled during analysis.
- A bounded parallel owner may safely have sampled descendants because its own completion remains the fixed cap. A sampled duration that determines the gating owner's lifetime is rejected.
- Parallel loop restarts each completed child; sequence loop restarts from the first child. `prepare` resets scheduling while authored selection state remains in external registers.
- Foreground parent completion still clips descendants, including descendants that repeat forever through the JSON path.
- Capture consolidation preserves requested slot and adds explicit readiness, avoiding the previous generation-minus-one sentinel. Compiler ownership uses unique pointers through construction and publishes clock handles after successful compilation.
- Text property/source restrictions match the eight-source census, with explicit wrappers, exact rational exit literals, fixed selector policy, two-curve zoom sums, image triangles and envelopes. Unsupported fields are rejected instead of dropped. Explicit default local scope and independent mode are canonical normalization cases.

An explicitly authored zero phase currently makes a static lifetime unknown even though zero has no phase effect; this is a conservative rejection, not incorrect scheduling. No broadening is required for the eight built-ins.

## Limits

This pass did not execute new native probes, inspect decoded media/GPU behavior, or review the separately assigned Director/F2 boundary changes. Confirm repairs and full acceptance tests before treating either finding as resolved.
