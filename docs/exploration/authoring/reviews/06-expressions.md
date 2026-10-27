> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Independent review 06: expressions and composed values

Reviewed 2026-10-26 against checked-out `grammar.js`, `app.js`, production `pattern_parser_v3.cpp`, `render_eval.h`, and `main.cpp` lint implementation. Read `CLAUDE.md`; repository identity/authentication check passed. No playback, commits, or production/editor implementation edits.

## Verdict and reproducible checks

**Structural preservation and product acceptance: yes for the exercised supported forms. Strict expression validation: no. Playback semantics: not established.** Run `node tests/authoring_review06_test.js` for 52 real Release product lint cases and lossless structural roundtrips. Add `--browser` with Playwright available through `NODE_PATH` and the local authoring server on port 8766 for seven actual inspector/composer interactions followed by server-backed compiler validation.

The product cases cover unary minus/not, nested unary minus, all six comparisons, arithmetic including modulo, power, nested ternary, `and`/`or`, min/max/abs nesting, parenthesized expressions, decimals, all six clock attribute spellings, this/self/named ancestor/root references, raw unknown scalar, constant and curve syntax, all easing forms, named clocks, fractional/frame/raw-condition windows, repeated windows, and frame/fraction/mixed-unit envelopes. Inspector field replacements preserve bracket contents, curve syntax, windows and envelopes. Negative tests exercise exponent literals, unary plus outside brackets, leading-dot bare constants, unknown easing/clock names, mixed/reversed/oversized windows, zero rise and oversized envelopes.

## Expression language verified from evaluator source

`render_eval.h` defines precedence low to high as ternary, `or`, `and`, one comparison, addition/subtraction, multiplication/division/modulo, power, unary, primary. Power is right associative; unary binds more tightly than power, so `-2^2` means `(-2)^2`. Ternary branches recurse. Boolean results are numeric 0/1. There is no short circuit: both boolean operands and both ternary branches are evaluated. Divide/modulo by zero return zero. `%` uses floating-point `fmod`, not integer remainder. min/max accept up to two parsed operands; abs uses the first. Parentheses group full ternary expressions.

Clock attributes are `progress`, `frame`, `length`, `position`, `index`, `active`. A bare identifier reads a scalar register, default zero. A dotted identifier resolves the clock map (or literal `root`); missing clock/attribute returns zero. The parser substitutes this/self and visible named ancestors. A curve's explicit `over` name is checked against scope; a raw `[missing.progress]` reference passes through.

These evaluator facts are source inspection, not numerical playback observations. The test's expression lint checks are acceptance/smoke evidence, not exact-output assertions. Large legal expression results can fail lint: `2^3^2` evaluates outside lint's absolute-value threshold of 16, so the power smoke case divides its result by 1000.

## Reproduced limitations and findings sent to parent

1. **Raw bracket expression validation is permissive.** Real lint accepts `[sin(1)]`, `[1e-3]`, `[1 +]`, `[(1]`, `[1<2<3]`, `[min(1,2,3)]`, `[missing.progress]`, `[this.missing]`, `[+1]`. This is existing engine behavior, not proof these are supported forms. Unknown function calls return their first argument, unknown identifiers/attributes return zero, unsupported trailing text may remain unread, and missing delimiters are tolerated. In particular `[1e-3]` reads the numeric prefix 1, not 0.001. The editor should explicitly state that compiler acceptance does not establish raw expression correctness. Production strictness changes are outside this authoring review.

2. **Leading-dot decimal acceptance differs by context.** Bare `zoom .25` and `env in .25 out .25` are rejected because these entry points check `next_is_digit`; `zoom [.25]` and curve endpoint `curve -1 -> .5` are accepted. The browser structural model accepts and preserves all these. The builder should normalize leading-dot decimals or instruct authors to include the leading zero. Exponent notation is unsupported outside brackets and misread inside them. Unary plus is unsupported.

3. **Original composer changed show expressions to zoom.** Reproduction before parent's shared fix: select image; target show; change mode to Expression; enter a condition; Apply. The mode handler resets target show to the first numeric field and writes `zoom [condition]`. Mode and target compatibility must preserve show for expressions. The original unrestricted modes also allowed windows/envelopes on numeric targets and curves on show/env. Parent is addressing these shared controls.

4. **Lint remains a bounded smoke check.** `main.cpp` evaluates lowered numeric fields at root positions 0, midpoint, and end-1, requiring finite values with magnitude <=16. Guards are evaluated without the numeric bound; registers start empty and effects are no-ops. This cannot prove expression syntax, all intermediate values, selected media, exhaustive clocks, or actual visual behavior.

Parser literal/curve/window/envelope lowering uses float values and `std::to_string`, which emits six decimal places; very small literals can lose precision when lowered. Source/serialization retains authored text, so this is a production semantic limit, not an export rewrite. No production numeric representation changes were made.

Parent shared UI corrections were independently rechecked: seven browser compositions passed real compiler validation; switching show to Expression preserves show; only compatible modes are enabled for numeric/show/env targets; leading-dot constant and signed curve endpoints normalize to leading-zero syntax. Parent owns final combined verification. This review intentionally avoids asserting playback or exact numerical runtime output.

