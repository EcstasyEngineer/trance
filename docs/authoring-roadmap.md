# Pattern authoring roadmap

These are proposed improvements, recorded after the September 2026 grammar and
playback review. They are not a description of shipped features. Current syntax
and behavior live in [the grammar reference](spec-grammar-v3.md) and
[authoring guide](authoring-v3-patterns.md).

Follow-up is tracked in [issue #66](https://github.com/EcstasyEngineer/trance/issues/66).

The aim is a reliable loop: describe an idea, produce a pattern, validate it,
preview it, and revise it. Both people and automation should use the same
compiler and receive the same useful diagnostics.

## First milestone: trustworthy authoring feedback

1. **Strict expression validation.** Reject unknown names/functions, invalid
   arity, unsupported modifiers, non-finite results and invalid duration
   arithmetic before playback. Today some unknown expressions quietly become
   zero or pass through an argument. Preserve source spans through lowering;
   return structured diagnostics with locations, explanations and examples.
   Share these between `--lint`, F2 and MCP. Track existing expression and
   arithmetic work in issues #64 and #63.
2. **Bounded, repeatable preview.** Run a specified number of frames with a seed,
   pinned media and isolated random streams. A seed alone is insufficient while
   media selection and scheduling share randomness. Record active pattern paths,
   clock values, effect firings and selections; support frame stepping and a
   small timeline. Display the failing frame and seed with every counterexample.
3. **Complete the automation loop.** Add non-mutating MCP operations such as
   `validate_pattern`, `describe_capabilities` and `preview_pattern` around that
   shared implementation. Existing source loading and screenshots are useful
   foundations. A bad candidate must leave the last working playback intact.

Acceptance: a new author can generate a nested pattern, understand and repair an
invalid expression, preview two identical runs, and apply the result without
restarting the player. No separate approximation of the grammar in the tools.

## Next: reusable patterns

- **Parameters with meaning.** Compile-time defaults, units, ranges and short
  descriptions can become F2 controls and automation inputs. Start with knobs
  such as pulse duration, zoom strength and palette/theme selection. Keep runtime
  mutation separate until its timing semantics are specified.
- **Shareable packages.** Bundle source, language version, metadata and a small
  demonstration with a clear media contract. Include a few carefully explained
  examples that compose existing primitives. Avoid introducing a package manager,
  general scripting language or macro system before a concrete need appears.
- **Independently owned layers.** Images already have named registers; text and
  animation still have shared runtime state. Two independently fading text lines
  are a concrete reason to add text handles. Establish ownership, copy behavior
  and lifetime for text first, then animation if needed.

## Timing: extend only for a demonstrated use case

A child should retain its own schedule even when a curve reads an ancestor's
clock. A useful example is a slow crescendo over an entire section while a
nested pulse restarts every few frames. Existing `over NAME` clock selection
already expresses this kind of relationship. Arbitrary writes into a parent's
timer would make reuse and re-entry harder to reason about; do not add them
without a case that cannot be expressed with explicit clocks and parameters.

## Validation investment

Generate small bounded pattern trees and test observable invariants: parent
durations win, inactive branches do not advance, re-entry restarts local clocks,
and multiple render passes do not duplicate effects. Shrink failures to a short
pattern and a reproducible trace. Keep a few real-media integration tests beside
these. This is a better next investment than attempting a proof of the entire
renderer before its authoring contracts are stable.

Playback observability should complement this: concise program, theme, pattern
and subpattern transitions, actionable media errors and quiet expected desktop
operation without a headset. F1 remains the detailed live inspector.

## Reference points

- [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
  provides a useful eventual editor integration boundary; diagnostics come first.
- [Remotion frame seeking](https://www.remotion.dev/docs/studio/seek) illustrates
  the value of precise visual preview navigation.
- [Hypothesis stateful testing](https://hypothesis.readthedocs.io/en/latest/stateful.html)
  explains generated action sequences and reduced reproductions; the test
  strategy matters more than choosing that particular library.
