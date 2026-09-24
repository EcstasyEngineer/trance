# OpenXR output

The Windows renderer implements automatic OpenXR attachment alongside its desktop
window. **Physical-headset acceptance is pending.** The former migration plan has
been condensed here into current behavior, design constraints, and the remaining
manual checks. Git history contains the original staged plan.

## 1. Architecture

```text
main loop → playback-time update
          → ScreenRenderer (window and GL context)
              ├─ optional XrOutput: left/right eye passes
              └─ desktop pass: scene, F1/F2, screenshots, present
```

`ScreenRenderer` is the only renderer. OpenXR is an optional output with per-eye
swapchains and head-locked quad layers. Each eye renders its own scene.
The desktop uses a separate unsheared pass at its own aspect ratio, not a blit of
an eye texture. UI stays on the desktop.

The OpenGL binding is Windows-specific. The machine's active OpenXR runtime
chooses the backend; trance does not select Oculus versus SteamVR.
There is no renderer config key or command-line selector.
Older system files may still contain `renderer`; loading ignores that obsolete
selection and preserves the other settings, and the next save removes the key.

## 2. Attachment and failure handling

When unattached, `XrProbe` retries at five-second intervals. On Windows a registry
precheck detects an absent runtime without loading it. Potentially blocking
instance discovery runs in one background probe; successful results transfer to
the render thread for GL-dependent session/swapchain construction.

Unsuccessful probes destroy their instances. Keeping an instance alive after a
no-headset result could retain the old runtime and interfere with a later runtime
switch. At most one probe is in flight. A 30-second watchdog disables probing for
the rest of the run if that worker does not return; the desktop stays available.
The worker cannot be safely killed in-process.

XR failures tear down that output with the GL context current, then resume
probing. A session stopping temporarily can instead become attached-idle and
resume. Doff behavior depends on runtime events and must be tested; it is not
correct to promise every doff detaches.

Attach/session creation still includes synchronous work on the render thread.
A runtime can stall there; hardware timing has not been measured.

State transitions produce diagnostics rather than repeating the same failure
every probe. The process defaults `XR_LOADER_DEBUG` to `none` to silence the
loader's duplicate console errors. An explicit environment setting is preserved;
set it to `error` or `all` before launch when investigating the loader itself.
Application diagnostics and background attachment retries remain enabled.
F2 shows current XR status. The command channel reports:

| Value | Meaning |
|---|---|
| `off` | Unsupported platform or probe disabled for this run. |
| `unattached` | Probing without an output. |
| `attached-idle` | Output exists; XR session is not running. |
| `attached` | Running XR session. |

## 3. Frame and resource contracts

- Running XR uses `xrWaitFrame` for pacing; desktop vsync/frame limiting are
  disabled. Detached or attached-idle restores desktop pacing.
- Content advances on playback time, independently of monitor/headset refresh.
  Render-time mutations apply before the frame's passes, not once per eye.
- While minimized on Windows, the desktop pass/present can be skipped; a pending
  screenshot still requires a desktop pass. Occlusion and other virtual desktops
  retain their presentation path and need hardware testing.
- Idle frames retain XR content without advancing its schedule.
- Pause freezes desktop content and submits no headset layers. Hide additionally
  hides the window and pauses/mutes audio. The XR handshake continues while its
  session runs.
- XR cleanup must happen with the GL context current and before the window/context
  is destroyed.
- Desktop dimensions, eye dimensions, and font-atlas sizing are distinct.

**Known limitation:** a headset attaching after font-cache construction uses the
existing desktop-sized atlas. `max_height()` accounts for an already attached
headset but does not rebuild the cache on late attachment, so text may look softer.

## 4. Status and deferred work

The unification is implemented on SFML 3 and OpenGL. The former OpenVR backend
is removed. The earlier SDL3/direct-library migration remains an unimplemented
proposal, not a dependency of current XR support or a committed next step.
Revisit it only against a concrete platform or maintenance need.

The prior implementation notes record a desktop-only no-runtime soak and code
review. They do not establish headset performance, image quality, or recovery.
No claim below becomes verified merely because the relevant code compiles.

## 5. Hardware acceptance matrix

Run on Windows with Quest Link/Air Link and SteamVR where indicated. The timing
targets below are acceptance criteria, not observed performance. Record runtime,
headset, GPU/driver, observations, and any failures for each run.

| ID | Scenario | Required observation |
|---|---|---|
| T1 | No VR software | Desktop starts; one concise no-runtime diagnostic rather than repeated spam. |
| T2 | Runtime registered, service closed | Specific failure; normal desktop cadence; flat memory during 10 minutes of probes. |
| T3 | Runtime open, headset asleep, then donned | No-HMD diagnosis transitions to attachment; target ≤10 seconds. |
| T4 | Normal startup | Runtime/resolution log; centered head-locked stereo content; correct colors; unsheared desktop; native headset rate even with slower monitor; no black flashes at low content FPS. |
| T5 | Minimize, fully occlude, move to another desktop for 60 seconds each | Native headset rate; restored desktop shows current content within one second. |
| T6 | Edit F2 while headset plays | UI remains desktop-only; changes reach both outputs. |
| T7 | Pause, hide, then ten-minute hide | Headset content disappears; runtime remains responsive; show restores requested audio/playback state. |
| T8 | Doff for 30 seconds, re-don | Appropriate stop/resume or detach/attach behavior; desktop remains usable. |
| T9 | Kill runtime at three playback moments | One detach diagnostic, no application crash, desktop/audio recover; reattachment after runtime restart. |
| T10 | Launch desktop first, then start runtime | Hot attachment without restarting trance. |
| T11 | Ten runtime kill/restart cycles | Reliable reattachment; no growing CPU/GPU memory use. |
| T12 | Switch active runtime while unattached | Next successful attach uses the new runtime. |
| T13 | Runtime dashboard requests app quit | XR detaches without exiting the desktop player. |

Also measure late-attach text quality and synchronous attach latency. Compare
desktop pass cost with and without minimization instead of assuming it is free.

Implementation:
[render.cpp](../src/trance/render/render.cpp),
[render.h](../src/trance/render/render.h),
[openxr.cpp](../src/trance/render/openxr.cpp).
