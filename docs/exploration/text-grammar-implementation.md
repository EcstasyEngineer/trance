# Minimal V4 text frontend implementation

> Historical implementation record. See the [current text grammar](../v4-text-grammar.md)
> for `loop_subpatterns`, Inherit and fixed durations; sampled duration was removed.

The text frontend implements the eight literal production built-ins, retaining
every authored composition and ordered edge. Text syntax converts to the same
V4 JSON AST passed to the production native parser and scheduler. Existing V4
JSON remains accepted; V3 text remains rejected.

The supported surface is `v4;`, parallel/sequence/cycle blocks, ID, duration and
sampled duration, finite repeat, cadence and early cadence ramp, phase, fixed
round-robin selector cooldown, and edge chance exits. The five outputs and their
properties follow the three minimal-grammar design reviews. Scalar values use
linear arrows with optional scope, the fixed two-curve zoom sum, image-opacity
triangle, and image frame envelope. Probability fractions are literals. The
separately authorized `child_mode independent|loop|wait` clause maps directly to
the native enum; wait owner timing conflicts are syntax diagnostics, while
effective completion validation remains native.

Native API in `pattern_text_v4.h`:

```cpp
patternv4::parse_text_json(source);      // V4 text -> canonical JSON string
patternv4::format_text_json(json_source); // JSON -> canonical text or exception
```

Browser/CommonJS API in `v4-text.js`:

```js
TranceV4Text.parse(source); // authored document
TranceV4Text.format(doc);   // canonical text or exception
```

Both syntax parsers consume the full source, accept finite decimal/scientific
numbers, and reject unknown constructs, duplicates and trailing input with line
and column diagnostics. Parsing and formatting enforce a 4 MiB source budget,
64 composition levels and at most 4096 children per composition before recursive
processing. Native JSON formatting additionally bounds JSON parse depth.
Formatter field checks are strict at every supported
AST layer; unsupported advanced JSON forms fail without mutating the document.
The caller retains its existing document and can display JSON. There is no
generic JSON escape or expression/property bag in the text surface.

Canonical formatting uses a fixed clause/property order and two-space
indentation. Local scope spelling is omitted, independent child mode is omitted,
and cycle spells the fixed round-robin/no-self/first-zero policy implicitly.
Authored wrappers, IDs, property presence, numeric values and child order survive.

`tests/authoring_v4_text_test.cjs` reads the literal native built-ins rather than
the cached catalog. Browser and native tests also consume eight explicit text
examples in `tests/fixtures/v4-text.json`, preserving the structural review's
spellings independently of formatter output. Verified locally: all eight AST -> text -> AST comparisons,
stable formatting, input immutability, finite exponents, completion-mode syntax,
strict invalid-input diagnostics and unsupported-AST refusal. The packaging
script also completed locally and now regenerates its built-in catalog through
`authoring_server.builtins_v4()` on every build, including the new text module.

`v4_text_test` is wired into the supported native build. It checks all eight
exact AST round-trips, fixed-seed schedule/evaluated-value traces over 4096
frames, strict syntax failures and formatter refusal. The native execution test
adds recording-renderer equivalence through the same text conversion. Native
build/CTest evidence is recorded by the integrating task after the coordinated
Release build; syntax-only browser checks do not establish native timing validity.
