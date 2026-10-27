> **Historical development record.** This report captures an intermediate implementation. For the released language, see the [V4 grammar](../../../spec-grammar-v4.md).

# Proposal 16: author the image changes, not the register plumbing

Status: design proposal for discussion, not implemented behavior. Grounded in [the eight actual recipes](../BUILTIN-GRAMMAR-MAP.md). No playback claim and no promise that all desirable semantics already lower correctly to v3.

## The alternative

The editor's first screen should answer “what changes on screen, when, and how?” Present a composition as **sections containing image streams, text streams and a spiral**. A section can play its children together or one after another and repeat. Most built-ins need a single section; Slow Flash needs two successive sections.

An image stream is one persistent visible role. Its card reads as a sentence:

> **Primary images**, change **every 64 frames**, **zoom 0 → 0.5 per image**. Show motion **every third change**. Transition: **cut**.

The user edits the emphasized choices. Optional behaviors stay attached to that stream: motion accents, dissolve, visibility visits and interruption by a hold. They are not additional peers that the author has to wire together. “Every third change” has an explicit event being counted; it does not mean every third frame, segment definition, animation selection, or scene restart.

Four conceptual choices handle the visible image behavior:

1. **Content:** primary, secondary, runtime side, or probabilistic side switching. A motion accent uses that stream's selected side.
2. **Change rhythm:** fixed interval, accelerating intervals, or rapid cuts interrupted by a sampled hold. The last choice reveals hold probability, eligibility period, cooldown and duration together.
3. **Movement:** start/end zoom and optional origin movement, with an explicit owner such as “per image,” “whole section,” or “during hold.” A small per-image punch can be added to whole-section approach without exposing an expression by default.
4. **Appearance:** cut/dissolve; opacity; an optional visibility window or visit envelope. These are separate controls because a transition between images and an overlay's arrival/departure are different operations.

Layer order remains a visible list. A dissolve owns its outgoing image; a stream owns its live selection. Register names, copy order and paired draw commands are generated implementation details until Advanced source is opened.

This is intentionally fewer concepts than the grammar palette. A feature is exposed where it changes an authored behavior, rather than receiving a palette item because the parser has a production for it.

## All eight as concrete behavior cards

| Built-in | Proposed initial view |
|---|---|
| Accelerate | One 2048f section. Image stream: switch theme side with 50% probability per change; 140 progressively tightening changes using the existing normalized early-ease shape; approach 0 → 0.4 over section plus 0 → 0.1 punch per image; motion every fourth change. Word stream: primary, on image changes, opening quarter only, 50% occurrence probability. Spiral: speed 1 → 4 over section. |
| Slow Flash | Section sequence repeated twice. Slow 1024f: primary images every 64f, zoom 0 → 0.5 per image, motion every second change; primary caption every 64f; spiral speed 2. Fast 512f: secondary images every 8f, zoom 0 → 0.8 over section; secondary words every 16f; spiral speed 4. |
| Sub Text | 1024f section. Runtime-side images every 64f, zoom 0 → 0.375 per image, motion every third change. Secondary subtext every 32f. Spiral speed 4. |
| Flash Text | 1024f section. Secondary images every 64f; dissolve across the incoming interval; each image zooms 0 → 0.4 while incoming, then 0.4 → 0.8 while outgoing. Secondary words every 64f, primary captions every 32f. Spiral speed 2. Show the authored 128f × 8 lifetime only in Advanced if its reset boundaries affect actual behavior; don't discard them merely because the sentence is simpler. |
| Simple | 2048f section. Runtime-side images every 64f, zoom 0 → 0.5 per image, motion every third change. Primary captions every 32f. Spiral speed 3. |
| Super Parallel | 1152f section with three image streams: primary/opaque/phase 0, primary/opacity 0.5/phase 32f, secondary/opacity 0.33/phase 64f; all change every 96f and zoom 0 → 0.875 per image. First stream uses motion every second change. Runtime words every 32f. Spiral speed 3. |
| Animation | 1024f section. Runtime-side image stream: motion always, change every 64f, zoom 0 → 0.625. Secondary still overlay: change every 64f at phase 48f, zoom 0.5 → 0.625; visit 8f rise / 16f hold / 8f fall / remaining cycle absent. Runtime captions every 32f. Spiral speed 3. |
| Super Fast | 2048f section. Runtime-side image stream: cuts every 8f, zoom 0.0625 → 0.1875 per cut. Interrupt with one selected animated image, zoom 0.1 → 0.7 over its 64–128f sampled hold; one-in-twelve roll at eligible 8f boundaries, 64f cooldown after exit. Primary words every 8f with 25% occurrence probability. Spiral speed 3. |

This exposes the recipe without pretending that superficially similar intervals are identical. A section-wide zoom, incoming/outgoing image lifetime, overlay phase and sampled interruption lifetime have distinct owners.

## Competing approaches and my position

| Option | Benefit | Cost / position |
|---|---|---|
| Keep 22 grammar blocks, improve labels | Low implementation risk; full syntax remains visible. | Still makes a dissolve a copy/draw/image wiring exercise. Useful Advanced view, insufficient main view. |
| Make all effects independent lanes | Uniform visual layout. | Incorrect for an every-Nth motion accent: it belongs to image-change events, not an independent clock. Separate lanes are appropriate for independently refreshed captions or a visiting overlay. |
| Eight preset-specific forms | Fast route to editing these recipes. | Repeated special cases and weak custom composition. Presets should instantiate reusable stream behaviors rather than become eight separate editors. |
| Persistent streams with attached behaviors | Directly matches the recurring patterns across all eight. | Needs explicit event ownership and a defined lowering layer; my recommendation. |
| Replace v3 grammar immediately | Could make saved source equally clear. | Premature while actual semantics have counter/state mismatches. Prototype the view and lowering contract first; use evidence to decide which runtime abstractions are worth making first-class. |

## A concrete, bounded implementation path

First add an **intent summary** beside the existing source/block editor for recognized compositions. Each summary line names content, change event, motion owner and appearance. It must display “Advanced source behavior” for anything it cannot faithfully represent. This is reversible and immediately useful without silently rewriting custom source.

Then permit editing one reusable image-stream card for Simple and Sub Text. Compare the generated source, compiled effect timing and representative output with the original. Add dissolve as a behavior only after the outgoing/incoming ownership contract is explicit. Add interruption only after phase-owned event counters and theme selection semantics are verified. This tests the general mechanism before making a second editor that merely hides the same mistakes.

Potential immediately safe UI improvement: group palette primitives under **Advanced grammar**, and provide recipe-level shortcuts for “Image stream,” “Text stream,” “Dissolve images,” “Staggered image stack,” and “Animated hold interruption.” A shortcut should create and select the complete relevant subtree, with a behavior summary. It must not claim a persistent image-stream abstraction while still exposing unexplained generated registers as normal fields. This change requires agreement with the other reviewers before any shared UI edit.

## Equivalence boundaries that block a misleading redesign

- Accelerate's source comments intend every-fourth-cut motion and cross-cut alternating-side persistence. Independently compiled ramp segments have distinct counters/toggles; execution review is checking the mismatch. An intent editor should show the desired event ownership, but cannot claim the existing source already realizes it.
- A stream's still and animation should resolve one chosen side if that is what the card promises. Current `runtime` image and animation selection can resolve independently. Multiple independent simultaneous animation streamers are not available merely by adding image registers.
- No silent change from source-over layering to a normalized mixture. Opacity 1/0.5/0.33 and order have a concrete compositing result.
- No silent change from phase offsets to delayed starts, or from half-open sample timing to endpoint-inclusive curves. Startup selections and integer envelope samples matter.
- Image zoom is the renderer's projection parameter; it is not a conventional percent enlargement. A more familiar scale control needs a tested mapping before relabeling values.
- Main text alpha/fade/env and independent text snapshots are not implemented uniformly. A generic “dissolve text” card would promise unavailable behavior.

## Forum coordination

Reviewer 18 and I agree on persistent image roles, attached motion accents, owned outgoing snapshots, explicit motion timing owner, and retaining advanced primitives. I object to presenting the motion accent as an independently scheduled lane: it must count image changes. Reviewer 17 is checking event-counter and side-selection correctness; its evidence should govern the lowering contract rather than source comments alone. No shared production/UI patch is approved by this document.
