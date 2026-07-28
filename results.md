# Notes-sweep results — 2026-07-28

Findings from the airplane-notes sweep, what was verified, what shipped on
`feat/notes-sweep` (15 commits), and what still needs the Windows QA pass.
Tracking issues: #34–#42.

## Findings replicated (all verified in source before filing)

| Issue | Finding | Status |
|---|---|---|
| #34 | Themes: checkbox + absolute 0–10 int slider; `pinned` existed in proto but was never exposed in the F2 UI; visuals had no pin at all | **Implemented** |
| #35 | No grammar text view or editor existed anywhere; persistence + `line:col` parser errors already existed (feature was nearly free) | **Implemented** |
| #36 | Themes are frozen filename lists; `scan` was second-class and destroyed on save; extension allowlists filtered at scan time although the decode layer already tolerates junk | **Core implemented** (proto-parity cull + real proto scan field still open) |
| #37 | Live bug found during recon: scan-derived paths stored scan-relative, not root-relative — scanned themes were broken in the **player** as well as `--export_archive` | **Fixed + regression tests** |
| #38 | Esc and F2 were literally the same branch; "Escape never quits" was an old deliberate policy | **Implemented** (Esc quits; 8 stale doc sites updated) |
| #39 | Overlay: imgui-SFML focus latch never restored after overlay-off (clicks dropped *inside* imgui); `clear_overlay_hints` forces alpha 255 while the seam trusts a never-verified shadow var | **Implemented** (hypotheses A/B/D/E; C deliberately deferred) |
| #40 | Keybind normalization — design decision, not code | **Open** (decide the scheme) |
| #41 | VR "virtual desktop" mystery: `system.json` had **no `renderer` key** → monitor mode → SteamVR mirrored the desktop window. The head-locked OpenXR backend existed but never ran, and was mono | **Implemented** (`--renderer` flag, loud failure banner + F2 warning, docs, stereo per-eye quads) |
| #42 | `animation`: original still-layer is a trapezoid with ~32f true absence; v3 flattened it to a triangle. `accelerate`: identical total length (2770f vs 2772f) but slower-shaped ramp + lost lean-in/bursts/text stabs | **Implemented** (see grammar work below) |

## Grammar intent analysis (`docs/intent-screenplays.md`)

Intention screenplays for all 8 pre-fork visuals, diffed against the v3 ports.
Systemic conclusion: the ports flattened behavior exactly where the runtime had
the capability but the grammar had no author surface. **Verdict: small
extensions, not a v4** — the two-nouns/one-rule model is sound; v3 lacked
lightshow vocabulary.

Landed (all parser-only, zero runtime change):
- `show A..B` — visibility window on any draw (fractions, frames, or `[expr]`)
- `env in X [hold Y] out Z` — trapezoid-with-absence alpha envelope
- `line` — SPLIT_LINE text surface
- `alternate [chance P]` — deterministic/probabilistic A/B theme ping-pong

Re-authored built-ins:
- `animation` — still layer restored to 8f in / 17f hold / 8f out / 33f absent
  (ground-truthed by dumping compiled per-frame alpha; 16/16/16 legs cannot fit
  a 64f clock, so ramps traded to 8f to preserve the full absence hole)
- `accelerate` — per owner spec: 2048f total, 140-step up-ramp (sustained
  ≤16f strobe from ~54% in vs ~73% before; ~46% of runtime at strobe tempo),
  50% primary/alt theme swap per new image (`alternate chance 0.5` — flip is
  gated, draw always fires), restored whole-run lean-in + `anim every 4th`

Still open on #42: re-authoring the other six built-ins' text lanes with the
new surfaces (needs visual QA), `spell`, `alternate as NAME`, shadow/font
surfaces (E5), burst-progress export (E6 — the one runtime extension).

## Adversarial review (Codex, 3 passes)

Six real defects found in the branch's own work, all fixed and re-verified:
1. openxr: `xrReleaseSwapchainImage` failures were ignored → now terminal + layerless frame
2. Stereo double-ticked spiral phase / warp time (also a **latent OpenVR bug**) → render-pass mutations gated at the eval seam, cycler path untouched
3. `clear_overlay_hints` activated unconditionally (focus steal on hide; double activation) → parameterized, single activation site
4. `focus_requested` leaked forever in VR / null-app_ui → cleared when moot
5. Repeated raw `show [expr]` composition precedence bug → RHS parenthesized
6. Scan junk became phantom media; root junk poisoned `/wildcards/` and killed scan persistence → junk denylist (denylist, not a media allowlist)

Every fix's test was verified red-before-fix. Also fixed in passing (S3):
stale lint-cache on rename, cross-program lint bleed; (e896905): latent
chance-guard overwrite of `when` on text draws.

## Windows QA checklist (the load-bearing caveat)

Nothing here has rendered on a screen — the dev box is headless; everything is
compile + ctest verified only, and the `#ifdef _WIN32` bodies (openxr.cpp,
overlay Win32 paths) were **never compiled**. On the laptop:

1. Build (`cmake --preset windows-msvc`, `--build --preset windows-release`) — /W3 /WX is the first real gate for ~3.5k new lines
2. F2 panel: percent sliders + pin + tween, pattern viewer/editor + live lint, VR-failure warning line
3. Esc quits (and only cancels edits inside a text field); F2 still toggles
4. Overlay: engage/disengage rapidly — panel must stay clickable, opacity must return to 35%; watch for the flagged regression risk (focus steal at engage if WS_EX_NOACTIVATE isn't in effect yet)
5. VR (Quest 3): set `"renderer": "openxr"` (or `--renderer=openxr`); verify head-locked stereo, parallax on zoom, no ghosting/doubled image, comfort at default eye spacing; if init fails, the banner + F2 warning must say why
6. Scan a messy media folder: junk excluded, weird-but-real extensions still load, `default.json` keeps `{"scan": ...}` across restarts

**Accepted tradeoff** (final review pass): the junk denylist drops extensionless
files, which SFML could in principle decode as media. Deliberate: extensionless
files recur as junk far more often than as intentional media. Rename the file if
it's real content.
