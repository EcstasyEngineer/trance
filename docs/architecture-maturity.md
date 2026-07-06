# Architecture & Code Maturity Audit

Date: 2026-07-05. Method: five parallel component assessments (Claude subagents, each
grounded in file:line reads), followed by an adversarial verification pass (Codex) that
re-derived every load-bearing bug claim from source before it was recorded here. Grades
use a five-step scale: **Production / Solid / Functional / Prototype / Legacy**.

## Scoreboard

| Component | Grade | One-line verdict |
|---|---|---|
| Visual pipeline (grammar v3 → cyclers → render eval) | **Solid** | Recently rewritten, real behavioral tests; expr-eval crash hole and untested effect runtime keep it from Production. |
| App shell & control surfaces (main loop, #21 channel, F2 UI) | **Solid** | Disciplined threading + single reconcile seam; `play_session` is an untestable 380-line lambda-web. |
| ThemeBank + media loading | **Functional** | Architecture sound and recently hardened, but carries verified cross-thread data races and zero tests. |
| Renderer layer (screen/overlay, VR, export) | **Functional** | Clean strategy split and careful X11 overlay; crashing export bug, stale VR path, unvalidated Win32 branch. |
| Data model (proto+JSON, sidecar, convert) | **Solid** | Spec-driven strict loader with real tests; exception-type gap and non-atomic saves. |
| Build system (CMake presets + vcpkg manifest) | **Solid** (near Production) | Hygienic, documented workarounds, warnings-as-errors on MSVC. |
| Creator (wxWidgets editor) | **Legacy** | Cannot open the files its own dialogs list; superseded by the F2 UI; still builds by default on Windows. |
| Docs | **Functional** | Normative specs current and rigorous; a stale layer actively misleads and the written cleanup plan was never executed. |

## Verified findings ledger

Every entry below was independently confirmed against source by the adversarial pass
(REAL = confirmed as claimed; REAL-BUT = confirmed with the stated correction). These are
the items worth burning-down first; file:line references are as of commit `ef02fa7`.

| # | Finding | Verdict |
|---|---|---|
| 1 | Data race: `ThemeInfo::enabled` written by render thread (`set_program`, theme_bank.cpp:197) while async thread reads it (`cache_per_theme`, theme_bank.cpp:377). Plain bool, no atomics — UB. | REAL |
| 2 | Data race: `_animation_theme_changed`/`_alt_animation_theme_changed` plain bools written on render thread (theme_bank.cpp:455), cleared on async thread (:584), read on render thread (:233). | REAL |
| 3 | Use-after-free window: `get_image` check-then-copy of `_all_images[index].image` (theme_bank.cpp:247-252) races `do_unload`'s `reset()` — which happens **outside** the unloading theme's mutex scope (:550-557), gated only by `use_count==0`. | REAL-BUT (worse than claimed: reset isn't under the mutex at all) |
| 4 | Runtime terminate from a pattern typo: a lone `.` in a raw `[expr]` passes the parser (pattern_parser_v3.cpp:399), reaches `std::stod` (render_eval.h:214), and nothing in the per-frame path catches. | REAL |
| 5 | Stateful render ops double-advance per eye: spiral phase + `_warp_time` mutate inside the per-eye render callback (render_eval.cpp:78, director.cpp:263) — affects **both** OpenVR (openvr.cpp:161) and 3D video export (video_export.cpp:107). `_warp_time += 1/60` also hard-codes 60fps. | REAL-BUT (3D export affected too) |
| 6 | Loader terminate on malformed JSON: colour fields and theme/variable arrays use bare `get<std::string>()` (session_json.cpp:463, :520, :553); `nlohmann::json::type_error` escapes main.cpp:1037's `catch (std::runtime_error&)`. Subroutine entries ARE type-checked; the gap is colours + those arrays. | REAL-BUT |
| 7 | Export null-deref: unknown `--export_path` extension resets `_exporter` with only a stderr line (video_export.cpp:39), then `render()` dereferences it (:132, :146); main.cpp:358 never checks. | REAL |
| 8 | `CommandChannel::reply()` does one unchecked blocking `send()` on the render thread (command_channel.cpp:165); partial writes dropped, full client buffer stalls a frame. | REAL |
| 9 | Playlist spin: a weighted cycle of zero-`play_time` standard items never breaks the `while(true)` switch loop (main.cpp:636-677), firing audio events every pass; only subroutine depth is capped. | REAL |
| 10 | `AsyncStreamer::async_update` 1ms sleep-polls until the render thread advances `_index` (async_streamer.cpp:137-139); pause/stop catching the streamer in that window leaves it polling indefinitely. | REAL-BUT (1ms sleep-poll, not a hot spin) |
| 11 | Data-loss risk: F2 Save writes the live session back via `save_session_json` with no temp-file/rename and no stream-failure check (session_json.cpp:968, app_ui.cpp:451-479); a failed write can truncate the user's session in place and the UI's exists-check still passes. | REAL (found by the adversarial pass itself) |

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

Risks beyond the ledger: `play_session` is a ~380-line lambda-web closing over loop
locals — zero unit-testability, and every new control surface adds another closure;
playlist stack machine dereferences `find()->second` unguarded (safe only via a distant
load-time validation invariant, while the F2 UI now mutates the session live); the
ImGui↔proto coupling rests on comment-only invariants ("never reorders/erases map
entries" — one future `erase` in a draw function is a use-after-free); `stop`/`start`
are pause aliases diverging from spec §4; `audio->Update()` runs while paused so
wall-clock fades complete silently; SIGINT in normal windowed mode still hard-kills;
the main loop busy-spins (acknowledged TODO).

Coverage reality: the pure parser is well-tested (~40 checks). Nothing exercises
`CommandChannel`, `execute_command`, the playlist machine, pause semantics, `Audio`, or
`AppUi`; the spec §6 pytest socket client was never written.

Actions: (1) extract the playlist stack machine into a testable class (medium,
mechanical); (2) write the spec §6 socket integration test (small, mechanical);
(3) resolve stop-vs-pause + stub-verb story — decision list below.

### 3. ThemeBank + media loading — **Functional**

The async media pipeline (~800 LOC ThemeBank + AsyncStreamer + GIF/WebM streamers +
vendored `jpgd`). Bi-thematic slot queue (`unloading|primary|alternate|loading`),
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
is terminate-with-message (no degrade path); vendored `jpgd` is a 2011 decoder with
3.2k LOC and known CVE history in forks, decoding arbitrary user files; manual
lock/unlock (not `lock_guard`) on `_purge_mutex` isn't exception-safe.

Coverage reality: zero tests; the headless harness excludes this component by
construction. The ef8c63d fixes were verified by a hand-run bad-media session whose
fixtures live only in the commit message. No sanitizer configuration exists anywhere
in the build.

Actions: (1) fix the three races — atomics + moving the reset under the read-side
locking view (small, mechanical); (2) add a TSan/ASan-able smoke run + check in the
corrupt-media fixture session (medium, mechanical); (3) jpgd exit strategy — decision
list below.

### 4. Renderer layer — **Functional**

~1,200 LOC, three-implementation strategy: `ScreenRenderer`
(windowed/fullscreen/overlay), `OpenVrRenderer`, `VideoExportRenderer`. SFML creates
contexts; drawing is raw GL 2.1-era GLSL. Overlay click-through is free-function
X11/Win32 hint code reconciled once per frame from the apply seam.

Strengths: clean 8-method virtual interface with sensible fallback selection; the X11
overlay path is genuinely careful (correct EWMH mapped/unmapped split, XShape probe,
graceful Wayland degradation); the pre-swap UI hook solved a real double-swap strobe
bug and documents it; contained X11 macro pollution.

Risks: ledger items 5 and 7, plus: the Win32 overlay/tray paths remain UNVALIDATED
in-source (no Windows box in the dev environment — the intermittent opaque-overlay
bug's leading hypothesis, DWM fullscreen-optimization promotion of an exactly-
fullscreen borderless topmost GL window out of the composited path, ships with a blind
mitigation and needs hands-on confirmation); `compile()` never detaches/deletes
shaders and returns programs even on link failure; `VideoExportRenderer` leaks both
FBOs/textures and its YUV program; VR `update()` drains and ignores every VREvent
(SteamVR quit can never propagate); `init_glew()` warns but never fails; render.h's
"SFML 2.6 cannot create an ARGB visual" comment predates the shipped SFML 3 migration
— the uniform-opacity design may rest on an obsolete constraint (per-pixel alpha may
now be achievable).

Coverage reality: zero, and mostly honestly untestable headless. Realistic: extract
exporter selection (would have caught the null-deref); GLSL lint as a build step.

Actions: (1) fix the export null-deref + make GL init failures fatal in export mode
(small, mechanical); (2) Windows hands-on validation of overlay+tray+opacity and the
ARGB re-examination (medium, needs owner/hardware); (3) VR fix-or-fence — decision
list below.

### 5. Data model, build, creator, docs

**Data model — Solid.** JSON on disk, frozen proto in memory, strict hand-written
mapper (+ sidecar for pattern files / scan dirs). Spec discipline is real: unknown-key
errors with JSON paths, path-contract enforcement, version wrapper. The legacy
converter is careful (backslash-vs-octal state machine, MessageDifferencer round-trip
check). Debt: ledger items 6 and 11; sidecar-less convenience save overloads silently
freeze scan-dirs and re-slug pattern files (the creator uses exactly those overloads);
serialization writes pattern files as a side effect; deprecated proto fields and a
dead `SessionArchive` message await the named trance_pb-retirement wave.

**Build — Solid, near Production.** Presets + vcpkg manifest + pinned baseline,
per-target options, `/W3 /WX`, every workaround commented with its failure mode.
Tests default OFF with no CI, so regressions surface only when someone runs ctest.

**Creator — Legacy.** File dialogs filter `*.session` while the loader fatally rejects
non-JSON paths — it cannot open what it offers. No support for custom patterns or
entrainment. Its save path would structurally mangle a modern session (sidecar-less
overloads). Still builds by default on Windows, paying the wxWidgets port cost for a
dead binary.

**Docs — Functional.** The normative core (`session-json-format.md`,
`spec-grammar-v3.md`, `engine-today.md`) is current and rigorous. But
`sessions-and-playlists.md` and `architecture.md` still open with ".session is a
protobuf" (false since the JSON cut), spec §9 labels shipped waves "future", and
`archive/docs-cleanup-plan.md` prescribes an archive structure that was never executed
(since done — the plan itself now lives in `docs/archive/`).

Actions: (1) close the loader exception gap (small, mechanical); (2) creator
retirement — decision list below; (3) execute the docs cleanup plan (medium, mostly
mechanical).

## Decisions needing the owner

These four surfaced independently across assessments and are cheap once decided, but
they are product calls, not engineering calls:

1. **`stop` vs `pause` semantics** — spec §4 says stop = "return to idle"; both are
   implemented as pause aliases. Is the alias permanent (update spec) or is idle a
   real target state? Should stub verbs (`set`/`get`/`load session`) reply a
   distinguishable `not-implemented`?
2. **VR path: fix or fence** — if OpenVR is alive, per-eye double-advance (ledger #5)
   and ignored VREvents need fixing; if dead, delete the path and make OpenVR an
   optional dependency instead of `REQUIRED`.
3. **Creator: retire or keep** — F2 parity v0 has landed and the creator can't open
   modern sessions. Flip `TRANCE_BUILD_CREATOR` default OFF, or delete `src/creator/`
   outright.
4. **jpgd exit strategy** — sync the vendored 2011 decoder against maintained
   upstream, or test whether SFML 3's stb_image handles the progressive JPEGs that
   motivated vendoring and delete 3.2k LOC.
