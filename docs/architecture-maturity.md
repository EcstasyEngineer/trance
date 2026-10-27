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

| Check | What it exercises | Limits |
|---|---|---|
| `grammar_lint` | Production V4 builtin validation and sampled render-expression checks. | Does not explore every transition or random outcome. |
| `v4_parser_test` | Native AST acceptance, diagnostics and execution bounds. | Is not a media decoder or pixel comparison. |
| `v4_runtime_test` | Parallel/sequential/random nesting, durations, repetitions, cadence, offsets, sampled holds, exits and clipping. | Finite test cases do not prove every possible AST. |
| `v4_native_execution_test` | Production compiler/render behavior assertions for Parallel/Flash Text/Super Parallel, private images and theme/animation coherence. | Recording calls do not establish decoded media or final GPU pixels. |
| `compiled_visual_test` | Production capture and refresh effects, stale selections and repeat-render invariance. | Uses recording media/render APIs. |
| `playlist_runner_test`, `session_json_test` | Playlist transitions, session serialization and validation. | Not complete live-loop or filesystem coverage. |
| `bed_preset_test` | Synthesized preset lookup, application and matching. | Not a listening or sound-device test. |
| `theme_bank_test` | Synthetic media, selection, fallback and cache behavior with a GL context. | Not complete codec or threading coverage. |

`theme_bank_test` needs a working GL context, although it does not open a window.
It is labelled `gpu` and can exit successfully with a skip message when a context
is unavailable. Inspect that output before describing it as exercised. The
Windows CI workflow runs CTest with `-LE gpu`; it excludes this test.

`trance --lint path/to/session.session.json` also loads custom pattern sources and
uses each program's entrainment beat period. `--lint --pattern path/to/pattern.pattern`
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
- Schedule reset and selection-counter/captured-image lifetime are separate contracts.
  Do not infer one from the other.
- Theme loading crosses threads. Headless schedule tests cannot establish its
  race freedom.
- The language exposes two live theme sides and shared text channels. Each image
  has a private capture, while same-side animations share a decoder/player.
- F2 edits autosave. JSON serialization, sidecar information and live-state
  mutation must be considered together when changing the editor.

Current editor gaps are a playlist graph editor, session-variable definition
editing, and a session-duration estimate. Playlist/variable definitions remain
editable through the [JSON format](session-json-format.md).

When reporting a check, name its scope and whether it actually ran. Avoid
turning a passing linter, a test's name, or an earlier audit into a broader claim.
