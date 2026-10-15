# Validation coverage and limits

The checked-in validation is a set of linters, regression tests, and manual QA
tools. It does not constitute a formal proof that every legal pattern, media
file, thread interleaving, or output device behaves correctly.

This page replaces the July 2026 maturity grades and bug ledger. Those findings
were tied to older code, and their partial status updates contradicted later
changes. Git history preserves that audit; it should not be used as a current
open-bug list.

## Automated checks

Enable `TRANCE_BUILD_TESTS`, build the full preset, then run CTest. Building only
`trance` can leave old test executables behind.

```powershell
cmake --preset windows-msvc -DTRANCE_BUILD_TESTS=ON
cmake --build --preset windows-release
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

| Check | What it exercises | What it does not establish |
|---|---|---|
| `grammar_lint` | `trance --lint`: all eight built-ins parse and compile; render expressions are evaluated at the first, middle, and last schedule frames with basic numeric bounds. | Effects use a no-op callback and empty registers. Three sampled states do not cover every branch, transition, random outcome, or frame. |
| `phase_execution_test` | Real parser/compiler/cyclers, exact pattern lifetimes, nested cadence ownership, entry order, branch exclusivity, child resets, clipping, sampled clocks, parent-clock reads, and SuperFast zoom expressions. | Its lightweight recorder is not production `run_effect`; it does not decode media, render pixels, or prove arbitrary nesting correct. |
| `compiled_visual_test` | Production image capture and refresh effects: initially empty or stale captures recover when media becomes ready, copies remain snapshots, and later render passes preserve selections. | Uses a recording media/render API; no actual decoding, GPU rendering or concurrency. |
| `playlist_runner_test` | Internal playlist transitions and stack behavior. | Complete live-loop, audio, UI, transport, or scheduling behavior. |
| `session_json_test` | Session/system JSON mapping, validation cases, and legacy import behavior. | Every filesystem failure or every possible malformed input. |
| `theme_bank_test` | Real synthetic media, theme selection, content isolation, fallback, failed-decode cache accounting and small-cache continuity. | Complete cross-thread race coverage or all real media codecs/files. |

`theme_bank_test` needs a working GL context, although it does not open a window.
It is labelled `gpu` and can exit successfully with a skip message when a context
is unavailable. Inspect that output before describing it as exercised. The
Windows CI workflow runs CTest with `-LE gpu`; it excludes this test.

`trance --lint path/to/session.session.json` also loads custom pattern sources and
uses each program's entrainment beat period. `--lint --pattern path/to/pattern.v3`
checks a standalone source. A successful lint run is a useful authoring check,
not a substitute for the behavioral tests above.

## Live validation

`tests/qa_command_channel.py` drives a running executable over its real socket.
It is deliberately outside CTest because it needs a desktop session. Use it
when changing command dispatch, pause/hide behavior, or presentation effects of
commands.

`tests/qa_playback_progress.py path/to/trance.exe` is a narrower live regression:
it uses temporary synthetic media, follows nested section transitions, replaces
the running custom source, and checks clean MCP replies and orderly shutdown.
It briefly shows a muted window and is also outside CTest.

`tests/fixtures/xr-baseline/` contains historical Windows desktop captures from
a machine without an OpenXR runtime. They document a baseline, not current
headset success. The [XR document](spec-xr-unified.md) describes the output
lifecycle and remaining hardware checks. Static review and a passing desktop
test do not establish headset attach/detach, pacing, eye rendering or runtime
failure recovery.

For visual timing changes, combine a behavioral trace with playback: inspect a
full occurrence and its entry/exit, include still-only and animation-only theme
sources, and confirm that repeated render passes do not change selections.
A sampled zoom curve can be correct while decoder delivery, fallback selection,
or the final projection is wrong.

## Boundaries to keep explicit

- Patterns have integer content clocks; playlists, presentation, animation
  delays, and audio have related but distinct time domains.
- Reading a parent clock is separate from owning or advancing a child schedule.
  Test both when changing nesting.
- Schedule reset and scalar/image-register lifetime are separate contracts.
  Do not infer one from the other.
- Theme loading crosses threads. Headless schedule tests cannot establish its
  race freedom.
- The language exposes two live theme sides, shared text state, and shared
  animation selection state. Multiple draw statements do not imply independent
  media players.
- F2 edits autosave. JSON serialization, sidecar information and live-state
  mutation must be considered together when changing the editor.

Current editor gaps are a playlist graph editor, session-variable definition
editing, and a session-duration estimate. Playlist/variable definitions remain
editable through the [JSON format](session-json-format.md).

When reporting a check, name its scope and whether it actually ran. Avoid
turning a passing linter, a test's name, or an earlier audit into a broader claim.
