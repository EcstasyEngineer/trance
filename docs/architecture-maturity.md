# Architecture & Code Maturity Audit

Audit date: 2026-07-05. Method: five parallel component assessments (each grounded in
file:line reads), followed by an adversarial verification pass that re-derived every
load-bearing bug claim from source before it was recorded here. Grades use a five-step
scale: **Production / Solid / Functional / Prototype / Legacy**.

**How to read this file.** It is a snapshot, not a live tracker. The ledger below is the
audit's original findings with resolution status appended as work has landed; anything
marked open was open at the last revision of this file and is worth re-confirming against
source before acting on it. The file:line references are as of commit `ef02fa7` and have
drifted — trust the function and file names, not the numbers.

## Scoreboard

| Component | Grade | One-line verdict |
|---|---|---|
| Visual pipeline (grammar v3 → cyclers → render eval) | **Solid** | Recently rewritten, real behavioral tests; expr-eval crash hole and untested effect runtime keep it from Production. |
| App shell & control surfaces (main loop, #21 channel, F2 UI) | **Solid** | Disciplined threading + single reconcile seam; the playlist machine has since been extracted and tested, but `play_session` is still a large lambda-web. |
| ThemeBank + media loading | **Functional** | Architecture sound and repeatedly hardened, but carries verified cross-thread data races and zero tests. |
| Renderer layer (screen/overlay, VR) | **Functional** | Careful X11 overlay; the VR side has since been unified into one renderer (§4's note) and is still **never run on a headset**; unvalidated Win32 branch. |
| Data model (proto+JSON, sidecar, convert) | **Solid** | Spec-driven strict loader with real tests; exception-type gap and non-atomic saves. |
| Build system (CMake presets + vcpkg manifest) | **Solid** (near Production) | Hygienic, documented workarounds, warnings-as-errors on MSVC, CI green and running ctest. |
| Docs | **Functional** | Normative specs current and rigorous; the stale layer and the frozen archive have since been deleted. |

## Verified findings ledger

Every entry below was independently confirmed against source by the adversarial pass
(REAL = confirmed as claimed; REAL-BUT = confirmed with the stated correction). These are
the items worth burning-down first. Resolved entries are struck through and kept so a
reader can tell "fixed" from "never a problem".

| # | Finding | Verdict |
|---|---|---|
| 1 | Data race: `ThemeInfo::enabled` written by render thread (`set_program`, theme_bank.cpp:197) while async thread reads it (`cache_per_theme`, theme_bank.cpp:377). Plain bool, no atomics — UB. | REAL |
| 2 | Data race: `_animation_theme_changed`/`_alt_animation_theme_changed` plain bools written on render thread (theme_bank.cpp:455), cleared on async thread (:584), read on render thread (:233). | ~~REAL~~ — **fixed by deletion**: the flags are gone. Streamer staleness is now detected by comparing each streamer's load-time tag (the lane's `ThemeInfo*`, read from the atomic slot) against the lane's live slot pointer, so there is no cross-thread flag left to race and no lost-signal window (the async thread used to clear the flag as a side effect of loading). |
| 3 | Use-after-free window: `get_image` check-then-copy of `_all_images[index].image` (theme_bank.cpp:247-252) races `do_unload`'s `reset()` — which happens **outside** the unloading theme's mutex scope (:550-557), gated only by `use_count==0`. | REAL-BUT (worse than claimed: reset isn't under the mutex at all) |
| 4 | Runtime terminate from a pattern typo: a lone `.` in a raw `[expr]` passes the parser (pattern_parser_v3.cpp:399), reaches `std::stod` (render_eval.h:214), and nothing in the per-frame path catches. | REAL |
| 5 | Stateful render ops double-advance per eye: spiral phase + `_warp_time` mutate inside the per-eye render callback — affected **both** OpenVR and 3D video export. `_warp_time += 1/60` also hard-codes 60fps. | ~~REAL~~ — **fixed**: the export half went away with the export path, and render-pass mutations are now gated at the eval seam so a stereo pass ticks accumulating state once. The hard-coded 60fps in `_warp_time` is a separate issue, being addressed independently — re-check `render_eval` before citing it. |
| 6 | Loader terminate on malformed JSON: colour fields and theme/variable arrays use bare `get<std::string>()` (session_json.cpp:463, :520, :553); `nlohmann::json::type_error` escapes main.cpp:1037's `catch (std::runtime_error&)`. Subroutine entries ARE type-checked; the gap is colours + those arrays. | REAL-BUT |
| 7 | Export null-deref on an unknown `--export_path` extension. | ~~REAL~~ — **resolved by deleting the video-export path entirely.** No fix was written; the code is gone. (`--export_archive` is an unrelated, live feature.) |
| 8 | `CommandChannel::reply()` does one unchecked blocking `send()` on the render thread (command_channel.cpp:165); partial writes dropped, full client buffer stalls a frame. | REAL |
| 9 | Playlist spin: a weighted cycle of zero-`play_time` standard items never breaks the `while(true)` switch loop, firing audio events every pass; only subroutine depth is capped. | REAL — **still open.** The loop moved into `PlaylistRunner::advance()` (`playlist_runner.cpp`) when the machine was extracted; the missing iteration cap moved with it. |
| 10 | `AsyncStreamer::async_update` 1ms sleep-polls until the render thread advances `_index` (async_streamer.cpp:137-139); pause/stop catching the streamer in that window leaves it polling indefinitely. | REAL-BUT (1ms sleep-poll, not a hot spin) |
| 11 | Data-loss risk: F2 Save writes the live session back via `save_session_json` with no temp-file/rename and no stream-failure check (session_json.cpp:968, app_ui.cpp:451-479); a failed write can truncate the user's session in place and the UI's exists-check still passes. | ~~REAL~~ (found by the adversarial pass itself) — **fixed**: both savers go through `write_file_atomically` (temp sibling + `std::filesystem::rename`) and throw on a failed stream. The stakes went up first: the panel now autosaves the live session on every committed edit rather than on a Save click, so the truncation window was being opened constantly. |

## Component assessments

### 1. Visual pipeline — **Solid**

A small compiler stack: recursive-descent v3 parser (`pattern_parser_v3.cpp`, ~1.3k LOC)
lowers pattern text to a shared IR (`pattern_ast.h`), a deliberately tiny compiler
(~130 LOC) maps it 1:1 onto the `Cycler` frame-counter tree, and a per-frame evaluator
interprets string-typed `[expr]`s against live cycler state. The compile-down invariant
is real, not aspirational — the compiler is a flat switch with no grammar knowledge;
all cleverness is parse-time lowering.

Strengths: consistent error philosophy (built-in parse failure = startup throw; custom
pattern failure = skip + surfaced warning); behavioral tests that encode actual shipped
regressions (accelerate pacing, zoom-cap jigsaw, burst exclusivity, tick conversion);
an engineered headless test seam.

Risks beyond the ledger: silent-zero resolution for unknown identifiers in raw exprs
(spec §7.4 calls the resolution check "mandatory"; it covers only image registers);
IR wider than the language (`Node::divide`, `anim alt`, Set/Inc/Toggle effects
unreachable from the surface — drift risk); ramp lowering is O(steps) with per-frame
re-parse of expr strings (fine today, a cliff if patterns grow); `Visual::reset()` is a
no-op so registers/pulse counters persist across repeat picks; ParallelCycler LCM can
overflow uint32.

Coverage reality: parser/lowering/cycler timing genuinely covered; the **effect
runtime** (`run_effect`, copy/guard/pulse/chance) executes only in production — every
test compiles with `MakeAction{}`.

Actions: (1) harden expr layer — non-throwing `strtod` + full-identifier resolution
check (small, mechanical); (2) test the effect runtime via a fake `VisualControl`
(medium, mechanical); (3) decide multi-pass render semantics — see decision list below.

### 2. App shell & control surfaces — **Solid**

~2,300 LOC: `main.cpp` (CLI, bootstrap, the `play_session` monolith), `net/` (loopback
TCP channel + pure line parser), `ui/app_ui` (ImGui F2 panel), `media/audio`. Single-
threaded render loop owns all mutation; workers only queue. All control inputs (socket
verbs, F2 UI, tray/hotkey, CLI flags) converge on one `CommandRuntimeState` reconciled
at a single per-frame apply seam — UI and socket structurally cannot disagree.

Strengths: explicit threading contract honored everywhere; bounded shutdown (every
blocking call gated by a 200ms `_running` recheck); pause engineered as a system
property (audio suspends, playlist wall-clock freezes, frame drain gates); fail-loud
startup validation; honest degradation on every unavailable surface.

Risks beyond the ledger: `play_session` is a large lambda-web closing over loop
locals — every new control surface adds another closure; the playlist stack machine
dereferences `find()->second` unguarded (safe only via a distant load-time validation
invariant, while the F2 UI now mutates the session live); the
ImGui↔proto coupling rests on comment-only invariants ("never reorders/erases map
entries" — one future `erase` in a draw function is a use-after-free); `stop`/`start`
are pause aliases diverging from spec §4; `audio->Update()` runs while paused so
wall-clock fades complete silently; SIGINT in normal windowed mode still hard-kills;
the main loop busy-spins (acknowledged TODO).

Coverage reality: the pure protocol parser is well-tested (~40 checks), and the playlist
machine now has `tests/playlist_runner_test.cpp`. Nothing exercises `CommandChannel`,
`execute_command`, pause semantics, `Audio`, or `AppUi`; the spec §6 pytest socket client
was never written.

Actions: (1) ~~extract the playlist stack machine into a testable class~~ — **done**, it is
`PlaylistRunner` (`src/trance/playlist_runner.{h,cpp}`) with its own ctest; (2) write the
spec §6 socket integration test (small, mechanical); (3) resolve stop-vs-pause + stub-verb
story — decision list below.

### 3. ThemeBank + media loading — **Functional**

The async media pipeline (~800 LOC ThemeBank + AsyncStreamer + GIF/WebM streamers +
SFML's stb-backed still-image loader). Bi-thematic slot queue (`unloading|primary|alternate|loading`),
RAM cache reconciled by a 10ms async thread, GL uploads render-thread-only with a
deferred-free purge protocol in both directions. 2014-era hand-rolled lock/atomic
concurrency.

Strengths: failure paths recently and thoughtfully hardened (ef8c63d — dead-slot
marking, shuffler stripping, render-side `_last_good_image` backstop); correct GL
thread discipline; memory bounded by construction; corrupt-media input validation at
every decoder; the bi-thematic invariant cleanly enforced (every accessor is `bool
alternate`, no index leaks).

Risks: ledger items 1-3 and 10, plus: `advance_theme`'s slot shift is per-slot atomic
but not atomic as a whole (async thread can observe a half-shifted queue); OOM policy
is terminate-with-message (no degrade path); manual lock/unlock (not `lock_guard`) on
`_purge_mutex` isn't exception-safe.

Coverage reality: zero tests; the headless harness excludes this component by
construction. The ef8c63d fixes were verified by a hand-run bad-media session whose
fixtures live only in the commit message. No sanitizer configuration exists anywhere
in the build.

Actions: (1) fix the three races — atomics + moving the reset under the read-side
locking view (small, mechanical); (2) add a TSan/ASan-able smoke run + check in the
corrupt-media fixture session (medium, mechanical).

### 4. Renderer layer — **Functional**

**Superseded by the XR unification** (`spec-xr-unified.md`, phases 1–5): OpenVR and the
whole renderer-selection idea are deleted, and there is now ONE renderer —
`ScreenRenderer`, owning the window, with the OpenXR side as a hot-attachable output of
it. Everything below is the July 2026 shape and is kept as the audit record; the caveat
that survives all of it verbatim is that **no VR code here has ever run against a
physical headset**.

Three-implementation strategy: `ScreenRenderer` (windowed/fullscreen/overlay),
`OpenVrRenderer`, `OpenXrRenderer`. SFML creates contexts; drawing is raw GL 2.1-era
GLSL. Overlay click-through is free-function X11/Win32 hint code reconciled once per
frame from the apply seam.

**The load-bearing caveat for this whole component: neither VR backend has ever run
against a physical headset.** Both were reviewed against the OpenXR/OpenVR specs and had
real defects corrected — a sampling-incomplete OpenVR texture that submitted successfully
over a black view, an XR frame loop that starved runtime synchronisation whenever no
visual frame was due, an ignored swapchain-release failure, a missing hybrid-GPU export
that would have bound the session to the iGPU — but "corrected against the spec" is not
"observed working". Every VR claim in this file and in the README is intended behaviour.

Strengths: clean 8-method virtual interface with sensible fallback selection; the X11
overlay path is genuinely careful (correct EWMH mapped/unmapped split, XShape probe,
graceful Wayland degradation); the pre-swap UI hook solved a real double-swap strobe
bug and documents it; contained X11 macro pollution.

Risks: the Win32 overlay/tray paths remain UNVALIDATED in-source (no Windows box in the
dev environment — the intermittent opaque-overlay bug's leading hypothesis, DWM
fullscreen-optimization promotion of an exactly-fullscreen borderless topmost GL window
out of the composited path, ships with a blind mitigation and needs hands-on
confirmation); the `#ifdef _WIN32` bodies in `openxr.cpp` and the Win32 overlay paths have
never been *compiled* on the dev box, so `/W3 /WX` is itself an unpassed gate for them;
`compile()` never detaches/deletes shaders and returns programs even on link failure;
`init_glew()` warns but never fails; render.h's "SFML 2.6 cannot create an ARGB visual"
comment predates the shipped SFML 3 migration — the uniform-opacity design may rest on an
obsolete constraint (per-pixel alpha may now be achievable).

Resolved since the audit: ledger items 5 and 7; and `OpenVrRenderer::update()` no longer
drains and ignores every VREvent — it handles `VREvent_Quit` (with
`AcknowledgeQuit_Exiting`) and an own-pid `VREvent_ProcessQuit`, shutting the API down and
exiting through the normal teardown path.

Coverage reality: zero, and mostly honestly untestable headless. Realistic: GLSL lint
as a build step.

Actions: (1) get both VR backends in front of an actual headset — this is the single
highest-value unblocking action for this component and needs owner hardware;
(2) Windows hands-on validation of overlay+tray+opacity and the ARGB re-examination
(medium, needs owner/hardware).

### 5. Data model, build, editor, docs

**Data model — Solid.** JSON on disk, frozen proto in memory, strict hand-written
mapper (+ sidecar for pattern files / scan dirs). Spec discipline is real: unknown-key
errors with JSON paths, path-contract enforcement, version wrapper. The legacy
converter is careful (backslash-vs-octal state machine, MessageDifferencer round-trip
check). Debt: ledger items 6 and 11; serialization writes pattern files as a side
effect; deprecated proto fields and a dead `SessionArchive` message await the named
trance_pb-retirement wave. (The sidecar-less `save_session` / two-arg
`search_resources` overloads that silently froze scan-dirs and re-slugged pattern
files went out with the creator — it was their only caller, so they are deleted and
the footgun is gone rather than merely unused.)

**Build — Solid, near Production.** Presets + vcpkg manifest + pinned baseline,
per-target options, `/W3 /WX`, every workaround commented with its failure mode. Tests
still default OFF locally, but CI now configures with `-DTRANCE_BUILD_TESTS=ON` and runs
`ctest` on every pull request, so a red test blocks a merge. (CI had been failing on every
run since February — it requested VS 17 2022 on a VS 2026 runner — and now uses the
preset.) One live trap worth knowing: building only the `trance` target leaves stale test
binaries in place and `ctest` then reports false passes; build the test targets, or the
whole preset, before believing a green run.

#### Tracked gaps from the creator deletion

The wxWidgets `creator` editor was deleted: its file dialogs filtered `*.session` while
the loader fatally rejects non-JSON paths, so it could not open what it offered; it had no
support for custom patterns or entrainment; and its save path would structurally mangle a
modern session through sidecar-less overloads. Three things it did have no F2 equivalent.
They are gaps, not regressions to be papered over — recorded here so they are not
rediscovered as surprises. The workaround in every case is the CLI / hand-edited JSON
path, which is fully capable because the session format is plain JSON with a normative
spec.

| Gap | Was | Workaround today |
|---|---|---|
| Playlist GUI editing | `src/creator/playlist.cpp` — graph editor for playlist items, next-item weights, transitions | Hand-edit the `playlist` object in the `.session.json` ([session-json-format.md](session-json-format.md)) |
| Session-variable GUI editing | `src/creator/variables.cpp` — add/rename variables and their allowed values | Hand-edit `variable_map` in the JSON; set values at launch with `--variables` |
| Session-duration estimate | `src/creator/launch.cpp` — walked the play graph to estimate total runtime before launching | None. The number was advisory only; no runtime behaviour depended on it. |

**Docs — Functional.** The normative core (`session-json-format.md`,
`spec-grammar-v3.md`, `engine-today.md`) is current and rigorous. The stale layer this
audit flagged has since been fixed: `sessions-and-playlists.md` and `architecture.md` both
lead with the JSON format, and `session-json-format.md` §9 marks its shipped waves
shipped. `docs/archive/` — twelve frozen design-history files that referenced deleted code
and that nothing read — has been deleted outright; git history holds it.

The remaining doc debt is a *class*, not a file: prose throughout the tree cites
`file.cpp:NNN` line numbers that drift silently with every refactor. Prefer naming the
function.

Actions: (1) close the loader exception gap (small, mechanical); (2) ~~execute the docs
cleanup plan~~ — done.

## Decisions needing the owner

These surfaced independently across assessments and are cheap once decided, but they are
product calls, not engineering calls. Two have since been decided; they are kept here with
their outcome so the decision is not relitigated.

1. **`stop` vs `pause` semantics** — **still open.** Spec §4 says stop = "return to idle";
   both are implemented as pause aliases. Is the alias permanent (update spec) or is idle a
   real target state? Should stub verbs (`set`/`get`/`load session`) reply a
   distinguishable `not-implemented`?
2. ~~**VR path: fix or fence**~~ — **decided: fix.** Both backends were kept and repaired
   (ledger #5, the ignored VREvents, and the further defects listed in §4); OpenVR stays a
   required dependency. **What this decision did NOT settle:** whether either backend
   actually works. Nothing has run on a headset, so "fix" is so far a claim about the code,
   not about the product. The follow-up question — whether a backend that fails on real
   hardware gets debugged or fenced — is untouched and remains the owner's to make.
3. ~~**Creator: retire or keep**~~ — **decided: deleted.** `src/creator/`, the
   `TRANCE_BUILD_CREATOR` option and the wxWidgets dependency are gone; the three
   features with no F2 equivalent are tracked in §5 above.
4. ~~**jpgd exit strategy**~~ — **decided: deleted.** The vendoring was motivated by
   SFML 2's inability to decode progressive JPEGs; SFML 3 loads images through stb_image,
   which does both baseline and progressive. Measured side by side over a generated corpus
   (baseline 4:2:0/4:2:2/4:4:4, progressive 4:2:0/4:4:4, grayscale baseline + progressive,
   CMYK, Adobe YCCK, restart markers, EXIF-tagged, 2000x1400, truncated, corrupt scan),
   stb decoded everything jpgd did — plus the CMYK, YCCK and truncated files jpgd rejected
   outright. `src/jpgd/` (3.5k LOC) is gone and JPEGs go through the same
   `sf::Image::loadFromFile` path as every other still format.
