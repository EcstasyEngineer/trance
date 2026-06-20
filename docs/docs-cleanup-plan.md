# Docs cleanup plan (v0.1 → make room for v2)

Plan to deduplicate and prune `docs/`. The docs accreted by *time-layer* — current as-built,
historical, and forward-looking material are interleaved, so the grammar/visual subject alone
lives in 4–5 places. Companion to `q1-q2-next-steps.md` (the v2 direction this clears the way for).

## Principle

**One home per subject, sorted by lifecycle — and don't polish what v2 is about to rewrite.**

Three buckets:
- `docs/` → current as-built reference (one doc per subsystem)
- `docs/design/` → the *single* living v2 design doc + raw inputs
- `docs/archive/` → historical, frozen, unmaintained (git keeps the rest)

## The duplication today

The visual/grammar subject is described across: `visuals.md` + `visual-grammar.md` +
`authoring-visual-patterns.md` + `roadmap-grammar-v2.md` + `gpt-pro/{q1,q2}-*.md` + the two GPT
responses + `q1-q2-next-steps.md`. That is the redundancy to collapse.

## Per-doc disposition

| Doc | Lifecycle | Action |
|---|---|---|
| `visual-grammar.md` (47 KB) | historical | **Salvage then archive.** Lift the still-true non-obvious bits — cycler-semantics traps (§3.5) and the ACCELERATE falloff math (§3.6) — into `visuals.md`, then move the rest to `docs/archive/`. Two generations stale (Framing A/B → data-driven → intent); biggest YAGNI in the tree. |
| `roadmap-grammar-v2.md` (15 KB) | design | **Supersede.** `q1-q2-next-steps.md` is the current synthesis. Fold any still-unique bits in, then delete or reduce to a one-line "superseded by next-steps" pointer. Don't keep two v2 theses. |
| `gpt-pro/q1 response.md`, `q2 response.md` | design input | **Keep as raw inputs, label as such** (one-line header: "GPT Pro raw response — input to next-steps, not authoritative"). |
| `gpt-pro/q1-q2-next-steps.md` | design | **Promote.** Move to `docs/design/` as the single living v2 design doc. |
| `gpt-pro/q1-readability-simplification.md`, `q2-intent-grammar.md` | design input | **Keep as the prompts that produced the responses; label as inputs.** |
| `roadmap-f1-overlay.md` | design | **Trim hard.** The overlay redesign now hangs off the deferred Score-IR decision. Cut to "decided + open question: gated on Score IR." Don't carry detailed plans for a deferred dependency. |
| `authoring-visual-patterns.md` | current→legacy | **Reframe, don't rewrite.** v1 stays forever (lowering target + escape hatch + stored patterns), so not dead — but no longer the front door. Header demoting it to "v1 / low-level pattern reference"; note v2 is the intended surface when it lands. |
| `visuals.md` | current | **Make it THE as-built reference.** The audit's flagged overstatement ("no hand-coded visual classes in the playback path") is now actually true post-port — update to match reality; receive the salvaged cycler gotchas. Keep "how it works" vs authoring's "how to write" split clean. |
| `architecture.md`, `audio.md`, `sessions-and-playlists.md` | current/stable | **Light touch only.** Fix the stale `file:line` refs the audit found; leave the rest. Not changing. |

## What NOT to do now (YAGNI guard)

v2 will rewrite the authoring/visuals story anyway (next-steps Phase 4), so don't deep-rewrite
prose v2 will throw away. Lead by what's about to change:

- **Stable** (audio, sessions, architecture): fix stale refs, done.
- **About to change** (visuals, authoring, grammar): only *reframe + dedup* — demote headers, fix
  now-false claims, point to the v2 doc. No new polish.
- **Historical** (visual-grammar): archive now.
- **v2 design** (roadmaps + gpt-pro): collapse to one living doc now.

## Sequenced actions (cheap → judgment-needed)

1. **Archive `visual-grammar.md`** after salvaging §3.5 (cycler traps) + §3.6 (ACCELERATE math)
   into `visuals.md`. → `docs/archive/`. *(low risk, high dedup)*
2. **Collapse v2 design material**: move `q1-q2-next-steps.md` → `docs/design/`; reduce
   `roadmap-grammar-v2.md` to a superseded pointer (or merge unique bits first); add
   "raw input" headers to the GPT prompts + responses. *(low risk)*
3. **Fix now-true/now-false claims in `visuals.md`** (the data-driven render is real now). *(low risk)*
4. **Reframe `authoring-visual-patterns.md` header** to "v1 / low-level reference." *(low risk)*
5. **Trim `roadmap-f1-overlay.md`** to decided + Score-IR-gated open question. *(needs the
   Score-IR in/out call from next-steps §9)*
6. **Fix stale `file:line` refs** in architecture/audio/sessions (audit list). *(mechanical)*

Highest-value, lowest-risk first pair: **(1) + (2)**. Everything else can wait until you're next
in the files.

## Note on this file and `gpt-pro/`

The `gpt-pro/` folder is currently untracked working material (prompts, responses, this plan,
next-steps). Per the principle above, the keepers (`next-steps`, and these inputs) should land
under `docs/design/` if committed; the rest can stay scratch or be removed. Decide that when
executing step 2.
