> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Review 17: source preservation and import/export

Reviewed 2026-10-26 against `grammar.js`, `app.js`, the repository Release compiler through the local authoring server, and native Edge file input/downloads through Playwright. Read repository `CLAUDE.md`. No playback or shared implementation edits.

## Verdict

The CST preserves source exactly in the exercised cases, including malformed and unknown syntax. Native untouched import/export also preserves CRLF and unknown source. Two actual editor defects were reproduced and reported to the parent for repair; the browser regression fails until both are resolved.

1. **Apply source changes untouched CRLF files.** Import `# preserved\r\npattern p for 8f {image primary}\r\n#final`; export is identical. Click Apply source without editing; export loses CR bytes. HTML textarea values normalize line breaks, and `applySource` compares that normalized value with the original serialized document.
2. **Duplicate can concatenate tokens.** Import `pattern p for 8f {image primary}`, select the image and click Duplicate. Download contains `pattern p for 8f {image primaryimage primary}`, which is structurally invalid. The copied first child's empty leading trivia supplies no token separator. Undo restores the original bytes exactly.

## Reproducible checks

`node tests/authoring_review17_test.js --compiler` passes **382 lossless roundtrips** and **7 real compiler cases**. The roundtrip matrix includes all eight built-ins, LF/CRLF, final/no final newline, inline and standalone comments, BOM and Unicode, compound/nested same-line statements, keyword-like resource names, multiple roots, unknown future statements/scopes, malformed brackets/braces, every prefix of a representative composition, and 200 deterministic bounded token mutations. A changed numeric field preserves every unrelated byte, including unknown syntax and comments.

Add `--browser` with Playwright available through `NODE_PATH` and the local authoring server running. The browser test uses actual file upload and downloaded file streams, Apply source, Duplicate, and Undo. `AUTHORING_URL` and `PLAYWRIGHT_CHANNEL` override the default local URL and installed Edge channel.

The real compiler accepts tested CRLF/comments, keyword-like names, and compound nesting; it rejects unknown syntax, incomplete syntax, and multiple top-level pattern roots. Multiple roots are preserved as editable draft source, not claimed as valid product syntax. Structural errors and compiler rejection retain original source; they do not establish that every unknown fragment has a meaningful editable tree. Compiler acceptance does not establish playback behavior.

## Coordinator resolution

Both editor defects were repaired. The native browser regression now passes: untouched CRLF survives Apply/export, compact first-child duplication produces valid separated statements, and Undo restores exact source. No additional exploration was commissioned after the intent-audit redirection.
