# Native timing and numeric implementation

Implemented 2026-10-28. Verification results belong in the main implementation
handoff after the supported Release build; the implementation agent did not run
an independent or simultaneous CMake build.

`child_mode` is one enum on parallel/sequential compositions. Omission and explicit
`independent` preserve existing completion. `loop` restarts each completed parallel
child independently, or restarts the complete sequential list after its final
child. Restarts occur on the next content tick without consuming an idle boundary
tick; owner duration, cadence, and ancestor clipping retain authority.

`wait` has no authored duration, cadence, offset, or repeat. Its local and occurrence
clock span is the exact sum (sequential) or maximum (parallel) of timed composition
children. Direct output leaves follow their owner and do not gate completion.
At least one timed child is required. Static lifetime calculation never samples
or touches RNG. Explicit finite repetition multiplies actual iteration lifetime;
a sequential iteration without cadence or loop uses min(cap, effective child
sum), while parallel/cadence/loop owners retain their finite cap. Nested wait is
recursive. Sampled, random, forever, offset-bearing and unresolved gating spans
are rejected conservatively. Existing bounded forever children remain supported
in ordinary independent compositions.

Native warnings remain `ParseResult.warnings` strings. Each exact inactive or
clipped child span includes its AST path and half-open local frame interval;
sequential unused owner spans are also advisory. Unknown/sample-controlled spans
are not presented as exact. Warnings do not invalidate intentional clipping or
claim that an inactive child means the entire image is blank.

The internal numeric evaluator now consumes complete scientific-notation numbers
and rejects trailing input. Lowered scalar constants and curve values have exact
numeric regressions, including small negative values, large exponents and representable subnormals;
malformed exponent/prefix cases must throw. Numeric conversion uses `strtod`
so finite subnormal results remain usable even on standard libraries where
`stod` converts range status into an exception. This is portability hardening;
a Windows subnormal failure was not reproduced. This is the narrow numeric repair,
not a new authored expression language or typed-value IR.

Capture state is one map of records containing the drawable image, requested
slot, generation and explicit current-readiness flag. A failed replacement
retains the drawable capture and retries on later first passes. Animation follows
the requested slot as before. The compiler takes one AST copy at its public
boundary, moves child subtrees into owning recursive construction, clears effects
from scheduler-only nodes after action construction, and publishes clock pointers
only after successful construction.

Consequential regressions cover loop boundaries/cadence, wait sum/max/nesting,
finite repeats, ancestor clipping, wait policy rejections, advisory timing spans,
exact scientific constants and curve values, malformed numbers, and factory
failure without publishing dangling clock pointers. Existing capture readiness
and multipass tests exercise the record refactor. The recording renderer also
checks all eight built-ins through JSON -> text -> JSON for 4,225 content ticks
per recipe, including image/animation parameters, text/subtext/spiral draws,
selection effects and immutable second passes. Flash Text retains an opaque
synthetic lower capture for 1,025 ticks including terminal/restart boundaries.

These are scheduler and synthetic renderer checks. They do not establish decoded
media readiness, GPU pixels, wall-clock timing, or headset behavior. Actual player
completion/presentation integration is handled separately in Director.
