# XR unified runtime — plan and rationale

Status: phases 1-5 implemented; QA matrix pending hardware. OpenVR is deleted; OpenXR is
an automatic, hot-attachable output of the one desktop renderer; a later milestone (phases
6-8, still a plan) replaces SFML with SDL3 + direct libraries. Nothing in phases 1-5 has
been run against a physical headset — §5 is the release gate and every one of its rows is
still owed. Every decision below carries its rationale **and a test for that rationale**,
so we can check after the fact whether the reasoning held — not just whether the code
shipped.

Product priorities this plan is judged against (also stated in CLAUDE.md and README):

1. **Fast** — headset at native rate, desktop never in the way, no wasted passes/spins.
2. **Small** — one exe, no DLLs, fewer dependencies, net-negative diffs preferred.
3. **Easy to use** — zero configuration; plug in a headset and it works; loud, specific
   console lines when it can't.

---

## 1. End state

```
main loop ── content ticks at global_fps (once, regardless of outputs)
              │
        ScreenRenderer (the only renderer; owns the one visible window + GL context)
              ├── XrOutput (optional, hot-attachable)     ── eye passes VR_LEFT/VR_RIGHT
              │     xrWaitFrame paces the loop while attached      into per-eye swapchains,
              │     detach on any XR failure; re-probe every 5 s   head-locked quad layers
              └── desktop pass (always)                   ── State::NONE + ImGui F2 + present
```

- No VR "mode". No renderer selection. No hidden window. No restart, ever.
- The headset is plugged in → within ~10 s the same session is playing in it.
- The desktop window can be minimized/occluded; the headset does not care (VRChat model).
- Quitting Link/SteamVR, doffing, session loss: desktop keeps playing; XR re-attaches
  when the runtime returns.

## 2. Decisions and testable rationales

### D1 — OpenVR is deleted (files, vcpkg dep, `openvr_api.dll` install step)

**Why:** SteamVR is itself a conformant OpenXR runtime — the OpenXR path already runs
against it (`openxr.cpp` pins API 1.0 specifically for older SteamVR builds). Deleting
OpenVR loses zero hardware and removes the only dynamic library in the distributable.
*Small*: dist becomes exactly `trance.exe`. *Fast/easy*: one fewer code path to maintain
and QA.

**Rationale test:** `cmake --install … --prefix dist` yields exactly one file.
`rg -i openvr src/ CMakeLists.txt vcpkg.json` → zero functional hits (frozen
legacy-decode surface `legacy.proto`/`session_legacy.cpp` excepted). The
"zero hardware lost" claim is conformance reasoning, not fully testable with one
headset — the testable slice is: a SteamVR-runtime attach works via OpenXR on the
hardware we have.

### D2 — XR is automatic; there is no config item

**Why:** with a working probe + hot-attach loop, a "renderer" choice answers a question
the user should never be asked (does VRChat have an "enable VR?" button?). *Easy to use*
is the priority; the probe is a registry read when no runtime is registered
(effectively free), so *fast* is untouched.

Greenfield config rule applies: the `renderer` key, the `System.Renderer` proto enum,
and the `--renderer` flag are **deleted outright — no migration, no aliasing, no
back-compat parsing**. An old `system.json` containing the key regenerates on load like
any other invalid config; that is accepted. (`trance.proto` keeps a one-line
`reserved 2;` so the field number is never accidentally reused — hygiene, not
migration.) The F2 renderer radios are deleted; the eye-spacing slider stays,
always enabled. An off-switch is deliberately omitted; if a real need appears it is a
one-line flag added on demand.

**Rationale test:** fresh machine + Quest Link running → launch with zero config →
headset shows the session. Machine with no VR software → exactly one concise console
line, desktop plays. `rg -i "renderer" src/` finds no config surface for it.

### D3 — Console messaging: loud, specific, deduplicated

The existing three-way diagnosis in `openxr.cpp` (no runtime registered ≠ runtime
registered but unreachable ≠ no HMD) is load-bearing for self-diagnosis and survives
verbatim, with tails amended from "relaunch" to "trance retries every 5 seconds".
Failures log **on state change only** — first occurrence and transitions, never once per
5 s probe (a console left overnight stays readable).

**Rationale test:** each failure leaf produces its distinct message exactly once over a
5-minute soak; the transition to attach prints the runtime name / per-eye resolution.

### D4 — Mirror = a third render pass (`State::NONE`), never an eye blit

**Why (correctness):** the eye textures carry the per-eye `eye_offset` shear (text
"jumping at you" would smear sideways on the desktop) and sRGB-typed storage (a blit to
the non-sRGB default framebuffer invokes driver-dependent sRGB conversion rules — the
exact mess the current swapchain format logic was written to avoid), and the aspect
ratios differ. The window needs a NONE pass for ImGui anyway.
**Why it's cheap:** the scene is a handful of 2D textured quads + text + one spiral
shader; a third pass on hardware that drives a headset is noise.

**Rationale test:** measure GPU frame time attached with and without the desktop pass
(minimize toggles it) — expected delta well under 1 ms. Desktop image shows no shear;
F1/F2/screenshots match the desktop-path fixtures captured before Phase 2 (frozen
fixture, not memory: capture screenshots + a console transcript on current master
first — that capture is Phase 1's first commit).

### D5 — `xrWaitFrame` is the sole pacer while the session is running

While attached **and the session is running**: vsync forced off, SFML's (sleep-based)
framerate limit set to 0. Detached — or attached-idle (session not yet READY, or
STOPPING acknowledged), where no `xrWaitFrame` exists to block on — both are restored
from `system.json` so the loop never hot-spins. The desktop present must never add a
second blocking pacer while XR paces. Content still ticks at `global_fps` via the
existing wall-clock accumulator — attach/detach never changes content speed.

**Rationale test:** 60 Hz monitor with `enable_vsync=true` + 90 Hz headset → runtime
performance overlay reads ~90 fps (60 would prove the desktop swap governs). Detached →
window paces at refresh exactly as today. A `global_fps: 10` session on a 90 Hz headset
shows zero black flashes between visual changes (the `render_idle` re-present contract —
quad layers name swapchains, not images — is preserved verbatim).

### D6 — Minimize/occlusion immunity (the VRChat property)

Nothing in the loop may block on window visibility. With vsync/limit off (D5), the swap
on a minimized window is effectively free; if a driver misbehaves, the fallback is
skipping the NONE pass + present while `IsIconic()` — the XR side is untouched either
way. Event pumping never stops.

**What actually shipped (phase 3, recorded because it deviates from the paragraph
above):** the `IsIconic()` skip is **unconditional and on by default**, not held back
pending a driver misbehaving. Two reasons, neither of them a measurement:
D4's own rationale test is written as "measure GPU frame time attached with and without
the desktop pass (**minimize toggles it**)" — that toggle only exists if the skip is
always on; and a pass whose entire output is a swap to a window with no pixel on screen
is the one case where skipping provably costs nothing visible (trap 15's pending-
screenshot force is the sole exception, and it is implemented). What this costs is
honest to state: **T5's minimized leg can no longer measure whether the unskipped swap
was in fact free**, which was D6's original rationale test for that state. The occluded
and other-virtual-desktop legs still present unskipped and still measure exactly that —
which is also the case trap 13 says decides — so the measurement is narrowed, not lost.
The soaks themselves were **not run** in phase 3 (no XR runtime on the development
machine); they are owed to the §5 QA matrix pass.

**Rationale test:** minimize / fully occlude / move to another virtual desktop for 60 s
each during headset playback: runtime overlay holds native rate, no reprojection spike;
restore shows current content within 1 s. (Minimized: also confirm the headset holds
native rate *because* nothing blocks, not merely because the pass is skipped — the
occluded leg is the control for that.)

### D7 — Detach, never exit; hot-attach via a background probe

Every XR failure that today quits the app (instance loss, session loss, wedged
swapchain `_lost`, event-pump death, EXITING) becomes: tear down the XR side with the
GL context current, keep playing on desktop, resume probing. The existing
STOPPING→READY doff/resume machine is kept untouched.

**Loader behavior (verified against openxr-loader 1.1.60 source, 2026-08-11):** the
loader re-reads `ActiveRuntime` whenever it holds no runtime, and caches a successfully
loaded runtime until a failed create or `xrDestroyInstance`. Consequences, both
designed in:
- Hot-attach and runtime switching need no helper process — but **every probe must
  fully destroy its instance on failure**, including the `FORM_FACTOR_UNAVAILABLE`
  (no-HMD) case. There is deliberately **no instance retention**: a retained no-HMD
  instance pins the loader to the old runtime and defeats Oculus↔SteamVR switching.
  Probes are 5 s apart; a create-per-probe is affordable.
- **Probing must not run on the render thread.** A *registered but dead* runtime
  (service stopped, stale registry) can block extension enumeration / instance
  creation for seconds on DLL load and IPC timeouts — a periodic desktop hitch at
  every probe. Design: the registry pre-check stays inline (one registry read, free);
  when a runtime is registered, a single in-flight **detached** probe thread does
  enumerate→create→`xrGetSystem` and posts the verdict; the main loop polls it and,
  on success, performs the GL-bound steps on the render thread with the context
  current — starting with the mandatory `xrGetOpenGLGraphicsRequirementsKHR` call on
  the handed-over instance (conformant runtimes reject session creation without it),
  then session, swapchains, FBOs. Calling instance-level XR from a worker while no
  other XR object exists is legal per OpenXR threading rules; the instance handle
  transfers cleanly.
  **Hung-probe policy (explicit, since a hung thread cannot be killed):** one probe
  in flight ever; if it posts no verdict within 30 s, print
  `XR probe is not responding; XR disabled until restart (broken runtime install?)`
  and stop probing for the run — the leaked thread is accepted, the desktop is never
  affected. Escalation if this bites in practice: a timeout-killed helper process.
- **Attach-time hitch budget.** `xrCreateSession` runs on the render thread and has
  no latency guarantee (tens–hundreds of ms typical, seconds possible during
  GPU/runtime transitions). Accepted deliberately: it fires exactly once, at the
  moment the user just started Link or donned the headset — a sub-second desktop
  stall then is invisible. If real-world attaches exceed ~2 s, escalate session
  creation to the XR-thread extension (trap 8's path), don't paper over it.

**Rationale test:** start trance with Link closed → start Link → headset lights up
≤ 10 s, no restart. Kill Link mid-session at three different moments → one detach line,
desktop/audio seamless, re-attach on Link restart. 10 kill/restart cycles → flat
memory. Runtime switch while unattached lands on the new runtime (proves
no-retention). With a deliberately dead registered runtime, the desktop frame cadence
shows no periodic hitch (proves off-thread probing).

### D8 — Pause/hide semantics (existing contracts preserved)

Pause: desktop freezes last frame with UI live; headset gets `layerCount=0` (content
genuinely vanishes rather than freezing head-locked in front of the eyes). Hide
(Shift+F11 / tray / `hide`): both blank, audio stash-muted, window hidden. The
xrWaitFrame/xrBeginFrame/xrEndFrame handshake never stops while the session is
running — the runtime must never flag the app unresponsive, however long it sits
paused.

**Rationale test:** hide for 10 minutes attached → runtime never shows "not
responding"; un-hide restores both outputs and un-mutes.

### D9 — The XR milestone lands on SFML; SDL3 is a separate later milestone

**Why:** the ChatGPT brainstorm's premise — "SDL3 ships OpenXR integration" — is void
for this codebase (verified 2026-08-11): SDL_GPU's XR support merged 2026-01-30
targeting **SDL 3.6.0** (our pinned vcpkg baseline ships 3.4.10, which has zero XR
symbols) and supports **Vulkan/D3D12/Metal only — no OpenGL**, while our entire draw
path is compat-profile GL. So no library choice buys us any XR functionality: the hard
part of `openxr.cpp` — session/swapchain/quad-layer logic, sRGB negotiation,
doff/resume, the fatal-vs-retryable swapchain taxonomy, anti-strobe idle re-present —
is raw OpenXR + GL and carries across libraries; only its context-owning shell (the
hidden SFML window) is library-bound, and Phase 2 deletes exactly that shell. The XR-untested state of that code is precisely why the
platform underneath it must not change in the same milestone: phases 1–5 are a net
**negative** ~150-line diff on a known base, hardware-validated by the QA matrix (§5),
and every line of them survives the SDL3 milestone unchanged.

**Rationale test:** phases 1–5 ship with no platform regressions and the QA matrix
green — proving the unification never needed a new windowing library. If Phase 2–4 had
instead stalled on SFML limitations, this rationale was wrong; record which.

### D10 — SDL3 + direct libraries afterward (what each swap actually buys)

The follow-on milestone replaces SFML with SDL3 for platform, and SFML's wrapper
subsystems with the libraries they wrap. Itemized, in priority terms:

| Swap | What it buys | Test of the rationale |
|---|---|---|
| SFML window/events → SDL3 | *Fast/correctness:* pacing becomes fully explicit — `SDL_GL_SetSwapInterval(0)` is a direct, unlayered call (driver control panels can still override swap behavior, as for any GL app — the gain is no sleep-based limiter of our own to fight), and minimize/occlusion arrive as events (`SDL_EVENT_WINDOW_MINIMIZED`/`_OCCLUDED`) instead of native `IsIconic` polling. The D6 skip (which phase 3 shipped unconditionally, so this is a certainty, not an "if we needed it") loses its native-API half: same decision, driven by an event. | The minimize test passes with no native-API workaround in the tree; the pacing code paths shrink (measure: lines deleted in render.cpp pacing logic). |
| imgui-sfml → stock `imgui[sdl3-binding,opengl3-binding]` | *Small/maintenance:* imgui-sfml is a third-party bridge that pins our windowing to SFML and lags imgui releases. The SDL3+GL3 backends ship **in the Dear ImGui repo itself**, maintained by the imgui project — they update the day imgui does and drop a whole dependency. Concretely for us: it also removes the texture tight-packing workaround and the imgui-sfml focus-latch quirk `main.cpp` carries. | `vcpkg.json` loses a dep; the two named workarounds are deleted; F2 works identically (full #39 hand-test matrix). |
| sf::Font → FreeType direct | *Small:* sf::Font **is** a FreeType wrapper, so metrics/kerning parity is near-exact (same engine underneath; our rasterization/atlas choices can still differ slightly); we keep `Font`'s public interface frozen so `director.cpp` doesn't change. One wrapper removed. | Before/after screenshots of text-heavy visuals differ only by ≤1px antialiasing. |
| sf::Image → stb_image direct | *Small:* SFML 3's loader **is** stb_image; same decode, one wrapper removed. | Decode is byte-identical on a fixture set (hash comparison). |
| SFML Audio → miniaudio direct | *Small:* SFML 3's audio backend **is** miniaudio (changelog 3.0.0), so this stays in the same backend family — low device-layer risk, though version/configuration differ, so nothing is assumed: every gate below still applies. **Caveat, named:** this is still the scariest swap in the whole plan, because the entrainment bed (binaural/isochronic synthesis) is the therapeutic core and its harness moves from `sf::SoundStream` to a `ma_data_source`. Mitigations are mandatory: the pure-synthesis core is fixture-guarded (offline WAV render before/after must be byte-identical), the swap goes **last** in its own revertible phase, ogg/vorbis support must be wired explicitly (stb_vorbis as a miniaudio decode backend — sessions use ogg, this is not optional), and long-duration under-load listening tests gate the ship. | WAV fixture byte-identical; 10+ min bed under CPU load with zero clicks; loop-seam and mid-play-morph listening tests; wav/ogg/flac/mp3 all load. |
| CMake side | *Small/maintenance:* SFML's mis-resolving config scripts forced a 24-line per-config ogg/vorbis/FLAC lib-pinning workaround in CMakeLists.txt — it dies with SFML. | The workaround block is deleted and clean configure still links. |
| Binary size | *Small*, claimed but unproven: fewer wrappers should shrink the exe. | Record `trance.exe` size before/after the milestone. If it grew, the "small" rationale for the swap was wrong — say so in the closing notes. |

**What the SDL3 milestone must NOT touch:** the v3 grammar → cycler → render pipeline,
all GL draw code and shaders, ThemeBank, playlists, command channel/MCP, the OpenXR
logic (moves, not rewritten), entrainment synthesis math.

**Sequencing landmine that dictates its phase boundaries:** SFML GL resources (font
atlases, `sf::Texture`) live in SFML's context share group — window/context, ImGui,
fonts, and UI preview textures must migrate **in one phase**; CPU-side images and all
audio are context-free and can lag safely (which is what lets audio go last).

---

## 3. Known traps (found in code review, then adversarially re-verified by a second
model against the code and the OpenXR loader source; each must land with its phase)

1. **Render-mutation timing** — `Director::render_mutations_enabled()` returns
   `state != VR_RIGHT` (a two-pass assumption). With three passes, warp-style
   accumulating visuals would tick twice per presented frame. A per-frame pass index
   (mutate on pass 0 only) fixes the double-tick but is **not sufficient**: mutation
   advance would still ride the presentation rate, which *changes on attach*
   (monitor Hz → headset Hz), so accelerating visuals would speed up 1.5× the moment a
   90 Hz headset attaches to a 60 Hz desktop. Fix: advance render-time mutations by
   playback elapsed time (excluding pause/hide), applied once **before** all passes of
   a frame — never during a pass. Cyclers already advance only in `Director::update()`
   and nothing requires presentation-rate advancement, so this is safe. Phase 2; the
   "same speed attached vs detached" verify is the gate.
   The same before-all-passes rule covers two sub-cases found on re-verification:
   the stale-image refresh in `compiled_visual.cpp` (~:194) currently mutates once per
   *pass* — left unfixed, the two eyes can draw **different images** in the same
   frame — and the debug/F1 counters (director.cpp:192, :1053) would triple-count
   with three passes. Both key off the same pre-pass epoch.
   A third sub-case, found reviewing phase 2: the draw-side `ThemeBank::get_animation`
   in `visual/api.cpp` runs per PASS and is not a pure read — on a theme with no gifs
   (most themes) it falls through to the random still shuffle, so the two eyes and the
   desktop mirror would each draw a different still and the recency bookkeeping would
   advance three times per frame. Same rule: the frame's first pass resolves, the later
   passes replay what it resolved, in call order.
2. **Per-pass dimensions** — Director reads one global `view_width()/width()/height()`
   and scales images `/2.5` when `vr_enabled()`. With both outputs live these become
   per-pass (eye w×h vs window w×h; `/2.5` keys on the pass, not the renderer). Full
   site list (from re-verification — more than the obvious aspect/text spots):
   startup font-atlas sizing (director.cpp:82), aspect (director.cpp:293), the `/2.5`
   image scale (director.cpp:339), text vertex normalization (director.cpp:449, :468),
   and the text sizing in visual/api.cpp (:270, :288, :341). Phase 2.
   **Font-atlas site, decided (phase 2):** the atlas cannot be per-pass — it is
   rasterized once, at fixed pixel sizes, before any pass exists. It is sized from
   `Renderer::max_height()` (window height, or max(window, eye) when a headset is already
   attached) instead of the per-pass `height()`, which at construction time can only
   report the window. Phase 4 owes it the other half: with a hot background probe the
   attach can land after startup, and a headset plugged in later still gets the
   window-sized atlas unless the FontCache is rebuilt on attach.
3. **GL state between passes** — eye swapchains stay `GL_SRGB8_ALPHA8` written with
   `GL_FRAMEBUFFER_SRGB` disabled (gamma passthrough; RGBA8 washes out under Quest
   Link); the window's NONE pass writes the same gamma bytes to the non-sRGB default
   framebuffer. No blits between differently-typed framebuffers, ever (D4). And the
   desktop pass must **re-establish its state, not inherit it**: the eye passes leave
   the eye-sized viewport (and FBO binding) behind — before every NONE pass, bind
   FBO 0, set the window viewport, keep `FRAMEBUFFER_SRGB` disabled. Phase 2.
4. **Context version request** — SFML's default context request is GL 1.1 (WglContext
   only sends version attribs above that), and XR runtimes publish a [min,max] GL
   version window they may enforce. The visible window must request 4.5-compat
   explicitly (same reasoning, and code, as the current hidden-window path). Related:
   any future `RenderWindow::create()` (fullscreen/windowed toggle) invalidates the
   hDC/hGLRC the XR binding holds — such a change must detach first, recreate, then
   re-probe. Today windowed-mode changes are next-launch anyway; keep it that way.
5. **Teardown ordering** — XR teardown needs the GL context current. Unified, XrOutput
   is a member of ScreenRenderer while `_window` lives in the base, so derived-before-
   base destruction makes the ordering automatic; the same ordering must hold at
   *detach time*, not just process exit. **Landed:** phase 5's collapse removed the
   base, so the ordering is now member order within one class (`_window` declared
   first, destroyed last) — same guarantee, one less inheritance fact to know. The
   explicit `detach_xr()` in the destructor remains, because "context current" is the
   half no destruction order supplies.
6. **Probe thread + no instance retention** — D7's loader-verified design.
7. **Modal move/size loops** — dragging the window blocks the Win32 message pump and
   thus XR submission for the drag duration (not SFML-specific; SDL3 has the same
   pump). Measure in Phase 3; if reprojection doesn't cover it acceptably, the fix is
   the standard timer-in-modal-loop, or the render-thread extension below.
8. **xrWaitFrame hang risk** — a hung runtime freezes the (single) loop, desktop UI
   included. Same exposure as today's VR mode, newly visible. Accepted for v1;
   escalation path is an XR render thread — a named REQUIRED EXTENSION, not faked now.
9. **Paused frames must still blank the headset** — with the F2 panel existing
   unconditionally, paused iterations still take the full render path
   (main.cpp:999's `do_render` stays true), which would submit *content* quads while
   paused. The XR layer decision keys off `paused || hidden` — blank layers even when
   the desktop repaints for the UI — never off whether a render happened. Phase 2.
10. **The NONE pass is unconditional** — no XR early-out (session not running,
    xrWaitFrame failure, mid-frame error) may skip the desktop pass. Structure the
    unified `render()` so the desktop leg runs regardless of what the XR leg did.
    Phase 2.
11. **Attached-idle pacing** — while attached but the session is not running
    (pre-READY, post-STOPPING), there is no `xrWaitFrame` to block on; with vsync and
    the limiter forced off, the loop would hot-spin. Desktop pacing (D5's restore) must
    apply in this sub-state, not just when fully detached. Phase 3.
12. **End-call failures must feed detach** — `xrEndFrame`/`xrEndSession` failures are
    currently logged and ignored, and a failed submit still sets `_has_content`.
    Persistent end-call failure is a dead session that would otherwise never trigger
    detach; route those results into the detach-pending flag. Phase 4.
13. **Occlusion is not minimization** — `IsIconic` catches minimize; a fully occluded
    or DWM-cloaked (other virtual desktop) window still swaps, and driver behavior
    there is not guaranteed non-blocking even with swap-interval 0. T5's occluded case
    decides; if it fails, extend the skip-present check to DWM cloaking/occlusion
    queries. Phase 3. **Landed:** the `IsIconic` skip is in the tree unconditionally
    (see D6's "what actually shipped"), so T5 now decides only whether the check must be
    *extended* to cloaking/occlusion, not whether the skip exists at all.
14. **Phase 1 config sweep** — deleting `System.renderer` touches more than the
    parser: the defaults table (session.cpp), the legacy protobuf conversion
    (session_legacy.cpp), and session_json tests all reference it and must be swept in
    the same commit. Note: an old system.json containing the removed key fails strict
    key checking and **regenerates with defaults, losing its other settings** — the
    greenfield config rule accepts this explicitly (no allow-list entry, no
    migration).
15. **Screenshot vs skipped NONE pass** — the `screenshot` verb latches a pending
    flag consumed by the UI hook during the NONE pass; if minimize skips that pass
    (trap 13's fallback), an acknowledged screenshot hangs forever. Fix: a pending
    screenshot forces one NONE pass even while minimized. Phase 3.
16. **Audio fades ride the wall clock through pause** — fades in audio.cpp (~:149)
    keep advancing while playback is paused, so resume lands at a jumped volume.
    Pre-existing bug, adjacent to D8's pause semantics; fix while in the seam
    (freeze fade clocks with the pause stash). Phase 2, opportunistic.

## 4. Phases

Phases 1–5 are the XR milestone (SFML, ship-now). 6–8 are the SDL3 milestone (after
the QA matrix is green on real hardware). Every phase: builds clean `/W3 /WX` both
configs, full ctest green **with test targets rebuilt**, independently shippable.

- **Phase 1 — Fixtures, then delete OpenVR + the renderer config surface.**
  First commit: capture baseline fixtures on current master (desktop screenshots via
  the `screenshot` verb, a `--renderer=openxr` console transcript, full ctest output) —
  these are the frozen comparison points D4/Phase 2 diff against. Then remove
  `openvr.{h,cpp}`, `cmake/FindOpenVR.cmake`, vcpkg dep, DLL install step, the
  renderer selection in `main.cpp`, `parse_renderer`/`save_renderer`, `--renderer`,
  the F2 renderer radios, and the `System.renderer` proto field (`reserved 2;`), plus
  the trap-14 sweep. No migrations (D2). **Interim startup behavior** (until Phase 2):
  unconditionally attempt the existing startup-only `OpenXrRenderer`; on success,
  today's hidden-window VR mode; on failure, `ScreenRenderer` with the existing
  diagnostic banner. No hot probing yet. *Verify:* dist = one exe; grep clean; old
  configs regenerate; desktop-only machine plays with the banner; ctest green.
- **Phase 2 — Unification.** `OpenXrRenderer` → `XrOutput` (rewritten in place in
  `openxr.{h,cpp}`; hidden window deleted; binding from the visible window's context;
  tri-state update: running/detach). ScreenRenderer absorbs it; render order = eyes →
  NONE → UI → present; traps 1–5 and 9–10 land here; `app_ui` created unconditionally;
  VR-mode carve-outs (`screenshot`/`ui` "unavailable in VR", hide-seam special case)
  deleted. *Verify:* headset + desktop simultaneously; F2 on desktop while headset
  plays (the headline capability); F1/HUD never in headset; accumulating visuals same
  speed attached vs detached (trap 1's gate); pause blanks the headset while the F2
  panel keeps repainting (trap 9); `qa_command_channel.py` passes; doff/resume
  unchanged.
- **Phase 3 — Pacing decouple.** D5's force-off/restore, including the attached-idle
  sub-state (trap 11); D6 verification (minimize / occlude / virtual desktop soaks —
  trap 13); drag-the-window measurement (trap 7).
  *Landed:* D5 and the D6 `IsIconic` skip (shipped as the default — see D6) plus trap
  15's screenshot force are code; every hardware measurement phase 3 was asked for —
  the D6 soaks and trap 7's drag reading — is **unrun**, because the development machine
  has no XR runtime. They carry forward to the §5 QA matrix (T5, and a drag leg), and
  trap 7's observation point is commented in `main.cpp` rather than guessed at in code.
  One anti-spin consequence of D5 that phase 3 owed and now has: with the desktop no
  longer pacing anything while the session runs, `XrOutput::render()` sleeps 10 ms on an
  `xrWaitFrame` failure the way `render_idle()` always has — a wedged runtime that fails
  every frame call without posting an event must not spin the loop. Trap 12 (phase 4)
  replaces that floor with an actual detach.
- **Phase 4 — Hot-attach.** D7's background probe (registry gate inline, single
  in-flight probe thread, no instance retention, GL-bound attach steps on the render
  thread), detach-never-exit rewiring including end-call failures (trap 12), state-
  change-only logging, `status` verb gains `xr=off|unattached|attached|attached-idle`
  (the QA observability hook). *Verify:* T9/T10/T11/T12 below, plus the dead-runtime
  no-hitch and runtime-switch tests from D7.
  *Landed*, with four notes:
  (a) **trap 12 was widened.** The spec named `xrEndFrame`/`xrEndSession`; the phase also
  routes `xrWaitFrame`, `xrBeginFrame` and `xrAcquireSwapchainImage` failures into the
  same detach. Same argument in each case — every failure code those calls can return
  describes an already-broken session, and the alternative is a permanently black headset
  that still reports itself attached while printing one error line per frame. It also
  retires phase 3's 10 ms anti-spin floor in `render()`, which existed only because
  nothing else could react to a wedged runtime.
  (b) `xr=off` is real, not hypothetical: it is what a non-Win32 build reports and what
  the 30 s watchdog leaves behind after it disables probing for the run.
  (c) **trap 2's phase-4 debt (font atlas on late attach) is NOT paid** — see the comment
  on `Renderer::max_height`. A headset that attaches after startup draws text from a
  window-sized atlas. Paying it needs `VisualApiImpl` to retain the session + cache size
  so the `FontCache` can be rebuilt, and adds a synchronous font preload to the attach
  hitch budget; it wants a phase that is allowed into the visual pipeline (phase 5, or a
  follow-up of its own). A run that starts with the headset already attached is
  unaffected.
  (d) **Every hardware verification is owed**, as in phase 3: the development machine has
  no OpenXR runtime registered at all. What WAS observed there: a 95 s soak (≈19 probe
  intervals) printed the no-runtime line exactly once and `status` answered
  `xr=unattached` throughout, with the desktop playing normally — T1, and the D3 message
  discipline, on the one leaf reachable without hardware. T9–T13, the dead-registered-
  runtime no-hitch reading and the runtime-switch test are code-inspection only so far.
  The startup "VR UNAVAILABLE" banner is gone with them: it asserted a whole-run fact that
  hot-attach makes unknowable at startup, so the F2 panel's #41 banner now reads a live
  callback instead.
- **Phase 5 — Collapse + docs.** Renderer base merges into ScreenRenderer; docs
  (README, CLAUDE.md architecture notes, this spec's status line) updated; final
  strings/OPSEC scan; release zip.
  *Landed*, with three notes:
  (a) the collapse took `update()`'s bool with it -- the base declared it and the comment
  there promised phase 5 would remove it, since phase 4 made it unconditionally true. That
  ripples out one level: `Director::update()` returns void too and main.cpp's
  `continue_playing` accumulator is gone, because nothing can end a run from the render
  side any more.
  (b) trap 5's ordering is now purely a member-order fact (`_window` is declared before
  `_xr`, so it outlives it); the explicit `detach_xr()` in the destructor stays for the
  half the compiler cannot supply -- making the context CURRENT.
  (c) `Renderer::State` is `ScreenRenderer::State`; no other rename. The docs sweep also
  corrected two stale VR-mode claims found on the way past: "the F2 panel is not built in
  VR mode" (controls.md -- untrue since phase 2) and "Linux VR" as a hotkey-only
  configuration (system_control.h -- there has been no VR-only configuration since
  phase 2, and the XR output is Windows-only regardless).
- **Phase 6 — SDL3 platform swap** (window/context/events + ImGui backends + FreeType
  fonts + stb images together — the share-group landmine dictates this bundle; F1 HUD
  becomes an ImGui overlay; `sf::Vector2f/Color/Clock` → tiny local structs +
  `std::chrono`; overlay-hints/display-info keep their native internals behind
  unchanged interfaces; theme_bank_test's GL context via hidden SDL window).
- **Phase 7 — Audio → miniaudio** (D10's caveat row: fixture first, ogg backend wired,
  listening gates). SFML fully removed from the build here, including the CMake
  workaround block.
- **Phase 8 — Sweep.** Dead includes, docs, exe-size record (D10 last row), strings
  scan, dist zip.

## 5. Acceptance QA matrix (manual — this is the release gate; no headset CI exists)

Run top-to-bottom on Windows + Quest 3 (Link/Air Link), SteamVR where noted. Full
step-by-step expectations live with each phase's verify notes; headline checks:

| # | Scenario | Must observe |
|---|---|---|
| T1 | No VR software on machine | desktop plays ≤ 5 s; one concise no-runtime line, printed once ever |
| T2 | Runtime registered, Link closed | one "could not connect… retries every 5 seconds" line; flat memory over 10 min of probes |
| T3 | Link open, headset asleep → don it | "no HMD" line once → attach ≤ 10 s after don |
| T4 | Happy path startup | attach line names runtime + per-eye res; headset head-locked, colors not washed out; desktop mirrors, no shear; 90 fps on a 60 Hz monitor; `global_fps: 10` → zero strobe |
| T5 | Minimize / occlude / other desktop, 60 s each | headset native rate throughout; restore shows current frame ≤ 1 s |
| T6 | F2 while headset plays | panel on desktop only; edits live-apply to both outputs |
| T7 | Pause / hide / 10-min hide | headset blanks (not freezes); desktop per D8; never flagged unresponsive |
| T8 | Doff 30 s / re-don | STOPPING and resume lines once each; desktop smooth throughout |
| T9 | Kill Link mid-run ×3 timings | one detach line; desktop+audio seamless; re-attach ≤ 10 s after Link returns; never a crash |
| T10 | Start with Link closed → start Link | hot-attach without restart (the D7 experiment) |
| T11 | Kill/restart Link ×10 soak | every cycle re-attaches; memory/GPU flat |
| T12 | Runtime switch (SteamVR ↔ Oculus) while unattached | next attach lands on the new runtime, no restart |
| T13 | SteamVR dashboard "quit app" | detach line; trance never exits |

---

*Sources for the verified claims in D7/D9/D10: SDL wiki README-xr; libsdl-org/SDL PR
#14837 (merged 2026-01-30, milestone 3.6.0); vcpkg registry at trance's pinned baseline
`a7acc4f9…` (sdl3 3.4.10, imgui 1.92.8 with sdl3/opengl3/freetype features, miniaudio
0.11.25, stb, openxr-loader 1.1.60); openxr-loader 1.1.60 source
(runtime_interface.cpp / loader_core.cpp — ActiveRuntime re-read + caching semantics);
SFML 3.0.0 changelog (miniaudio backend, stb_image loader). The plan was adversarially
reviewed in two rounds by a second model against the codebase and the loader source
before being committed. Full working documents from the 2026-08-11 planning session are
not in-repo; this file is the surviving plan of record.*
