> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Independent reviewer 03: render modifiers

Acceptance status: PASS. Render model edits and native syntax pass. The discovered curve builder scope-selector defect was fixed by the coordinator and independently retested in the live browser.

## Checks performed

Run `node tests/authoring_review03_test.js` against the server at port 8766. Each case POSTs the edited source to `/api/validate`; the server writes an isolated temporary pattern and runs the actual Release product `--lint`. This is independent of the browser CST accepting its own serialized output.

85 cases cover:

- Zoom, origin, alpha, brightness: signed constants, parenthesized constants, bare curves, parenthesized curves, early/late/linear easing, named enclosing clocks, raw bracket expressions.
- Image and all four text statements: fade in/out/inout, fractional and frame windows, raw show expressions, frame and fractional envelopes with and without hold.
- Repeated show and alpha fields: edit the second occurrence independently, then clear the first show clause while preserving the second.
- Spiral speed, warp amplitude/wavelength/speed, drunk intensity: constant, named-clock curves and expressions.
- Look spiral shape type and width edits; explicit register draw with all parameter families; a nested draw using an offset ancestor clock.

Full 85-case run: PASS, native exit success in each case. The added offset-ancestor case was also accepted using the real browser validation button (`LINT OK`, current revision).

## Resolved finding R03-1: valid offset ancestor missing from curve clock menu

Priority P2. `app.js` `modulatorBuilder` identifies named cadence clocks using `n.header.match(/->\s*(\w+)\s*$/)`. An offset follows the target name in valid v3 syntax, so that regex fails.

Reproducer:

```text
pattern render_review for 128f {
  every 32f -> pulse offset 4f {
    every 8f { image primary zoom (curve 0 -> 1 over pulse) }
  }
}
```

In the actual workbench, select the image, expand “Compose a curve, expression, or window”, choose Curve. The Curve clock (over) menu lists only “Nearest clock (this)” and `render_review`; `pulse` is absent. The nearest clock is explicitly shown as `every 8f`, so choosing nearest changes the modeled curve from its intended 32-frame ancestor. The same source passes the actual product compiler through the browser validation button.

Recommended fix: extract named targets through the already-tokenized model field for each ancestor, rather than an end-anchored header regex. Include a browser regression with a named offset cadence enclosing another cadence. Reported to the implementation owner and coordinator; no shared implementation file was edited by this reviewer.

Independent retest after the coordinator fix: reloaded the actual workbench; the built-in super_fast base image's curve selector now lists `super_fast` and `rapid` even though the burst controller has trailing period/chance/cooldown/duration options. Reloaded the nested offset-cadence reproducer; the curve clock selector now lists `render_review` and `pulse`. Selected `pulse`, applied the composed curve, and validated through the browser: current revision accepted, custom pattern 128f, `LINT OK`. No outstanding finding in this review's scope.

## Limits

Native lint checks parsing, compilation and three no-op render samples. These results do not prove decoded-media output, exhaustive expression behavior, playback or perceptual correctness. Text alpha limitations are already disclosed in the inspector; no production runtime change is claimed here.
