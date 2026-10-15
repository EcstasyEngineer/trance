# XR-unification baseline fixtures

Historical "before" capture for XR unification. These files record the desktop
before it became another pass of the unified renderer. They are comparison
evidence, not results from the current code. The current implementation and
hardware checks are in [OpenXR output](../../../docs/spec-xr-unified.md).

Captured at commit **26cd84b** ("docs(xr): plan of record for the OpenXR unified runtime
and SDL3 follow-on") — i.e. the last commit before any OpenVR/renderer-config deletion —
from `build/windows-msvc/Release/trance.exe`, on Windows 11 with **no OpenXR runtime
registered** (no `SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` in either hive) and no headset
attached.

| File | What it is |
|---|---|
| `session-used-default.json` | The session all runs played: the generated no-media default session (`./default.json`, bootstrapped by a no-arg cold start in an empty scratch directory). Themes are empty, so the visuals are text/spiral/flash on a blank background. |
| `desktop-screenshot.png` | Fullscreen desktop (`ScreenRenderer`) frame with the F2 panel open, taken through the command channel's `screenshot` verb — i.e. the composited scene + ImGui frame, glReadPixels'd from the back buffer inside the pre-display UI hook. |
| `desktop-screenshot-scene.png` | Same, after `ui off`: scene only, no panel. Baseline for desktop framing, aspect ratio, and gamma. |
| `desktop-command-channel.txt` | The verbs sent and the replies received during both screenshot runs (`status`, `screenshot <path>`, `ui off`). |
| `desktop-stdout.txt`, `desktop-stderr.txt` | Console of the plain desktop run. |
| `openxr-console.txt` | Console transcript (stdout + stderr) of a `--renderer=openxr` run on this no-runtime machine: the loader's failure chain, trance's `XR_ERROR_RUNTIME_UNAVAILABLE (no active OpenXR runtime)` diagnosis, and the `*** VR UNAVAILABLE ***` fallback banner. This is the "machine with no VR software" leaf of D3's three-way diagnosis, recorded before `--renderer` was deleted. |
| `ctest-release.txt` | Full `ctest --test-dir build/windows-msvc -C Release --output-on-failure` output at that commit (4/4 passing, `theme_bank_test` really running — this box has a GPU). |

Caveats worth knowing before comparing:

- The screenshots are content-random: the visual playing (`slow_flash`, `super_fast`, …)
  and its phase are whatever the scheduler picked at that instant, so a pixel diff
  against a later capture is meaningless. What they freeze is *framing*: window
  resolution/aspect, UI placement and scale, text/spiral geometry and gamma.
- `desktop-screenshot.png` shows a mostly white frame because the default session has no
  media and the flash visuals spend much of their cycle on the flash colour.
- `openxr-console.txt` is the no-runtime leaf only. The other two leaves of the
  diagnosis (runtime registered but unreachable; runtime up but no HMD) need a machine
  with VR software installed and belong to the current XR hardware checks, not
  this capture.
