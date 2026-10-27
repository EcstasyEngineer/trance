> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Independent review 07: scoped resources

Scope: current `grammar.js` and `app.js`, actual v3 parser/compiler through the local authoring server at `http://127.0.0.1:8766`. Reviewed `CLAUDE.md`, current grammar scope reference, and parser resource resolution. No playback was started.

## Verdicts

- **PASS, bounded syntax coverage:** all exercised resource syntax survives parse/serialize byte for byte, and replacing each existing field with its existing value leaves every byte unchanged. This includes nested patterns, `Scene.register` ancestor reads and writes, both qualified copy operands, named cadences and burst branches, `over NAME`, raw named-clock expressions, ramp clock names, comments, and register names that also spell parameter keywords.
- **PASS:** actual resource field edits can add, change, and remove destinations and change draw/copy references without silently dropping the other syntax.
- **PASS:** cadence registers share their enclosing pattern; nested patterns create independent register scopes. Real compiler lint rejects references to sibling pattern registers and missing named ancestor clocks, and reports never-written registers as warnings.
- **PASS:** duplication reparses into distinct editor node IDs, preserves scoped source, and compiles. Repeated human names remain legal; compiler resolution searches nearest enclosing scopes. Duplication does not claim to generate unique human names.
- **PASS with an authoring caveat:** moves preserve resource spelling and correctly invalidate prior validation. Moving a bare-reference statement across pattern boundaries can change its resource binding. Grouping in a cadence preserves its enclosing register scope; grouping in a new pattern changes it. The editor does not attempt reference rewriting.
- **Not established:** exhaustive whole-language coverage, actual image contents, frame-by-frame execution, selection lifetime, or media rendering. This review exercises source preservation and product compilation with its three no-op render samples.

## Reproduction and executable evidence

Run `node tests/authoring_review07_test.js` with the existing local server. Result: **18 real compiler cases passed**. The test independently checks lossless roundtrips, same-value field edits, actual changed fields, ancestor read/write/copy acceptance, sibling rejection, unknown clock rejection, lexical register rebinding after moves/grouping, duplicate IDs, shadowed names, and warning results. Expected semantic failures are assertions, not excluded cases.

An interactive browser check loaded a Scene containing `Inner` and `Other`, selected `copy Scene.stash -> local`, and used the inspector's “Move block to scope” menu to move it from Inner to Other. The source retained the qualified source and bare destination exactly. The breadcrumb and nearest clock updated to Other. Real compiler validation reported the abandoned `Inner.local` read as never written. The browser also showed a warning for an independently unwritten `Other.stash` read, correctly confirming that bare names do not implicitly search ancestor register scopes.

## Concrete usability finding

**P2: compiler warning results receive the same success status as a clean result.** In the browser move reproduction, the log contains `register is read but never written (draws empty)`, while the status says “Compiler validated · current revision,” the summary says “Compiler accepted this source,” and the toast says “Compiler validation passed.” Warnings are present in the diagnostics log, so they are not dropped, but there is no warning count/severity near the validation status. This matters particularly after a scope move, because the source can compile while drawing an empty register.

Recommended narrow improvement: keep compilation success distinct from warning-free status, with a visible warning count and “accepted with warnings” message when diagnostic lines contain warnings. A register-scope hint beside the existing clock hint would also help explain why moving into a pattern differs from moving into a cadence. Neither suggestion requires playback or parser replacement.

## Scope consequences worth preserving in user guidance

- Renaming an ancestor pattern does not rewrite qualified register references or `over` operands. Real compiler rejection exposes the now-missing ancestor name on validation.
- Bare register names bind to the current pattern scope, not the nearest ancestor that has written that name. Qualified references explicitly select an enclosing named pattern.
- Reading a register never written **anywhere** in scope warns; this is not a proof of write-before-read at every activation. A copy can still read an initially empty snapshot. Ordered effects and runtime activation remain relevant.
- Ramp expansion gives each segment a register scope. An ordinary surrounding cadence does not. The test confirms that an outside read of a ramp-local register warns.
- A same-name nested pattern shadows outer named references; compiler acceptance does not mean the reference still targets the outer object the author intended.

No shared implementation files were edited, and no commits, pushes, or playback actions were performed.

## Coordinator follow-up

Parent resolution: compiler warning lines now produce an accepted-with-warnings count in the status, summary and toast. A live browser reproduction with an unwritten scoped register showed one warning and preserved its diagnostics. Semantic scope refactoring remains a separate feature.
