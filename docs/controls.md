# Runtime controls

## Window keys and F2

| Key | Action |
|---|---|
| **Escape** | Quit; in an ImGui text edit, cancel the edit instead. |
| **F2** | Toggle the control panel. |
| **F1** | Toggle visual, cycler, and theme diagnostics. |
| **M** | Toggle global audio mute. |

These keys require focus. Closing the window, F2 **Quit trance**, and Windows
tray **Quit** also exit.

If F2 opens but neither hover nor clicks respond, Alt-Tab away and back is the
known workaround for [the startup focus issue](https://github.com/EcstasyEngineer/trance/issues/54).
The UI reconciles its input focus with the window to repair stale backend state.

F2 writes committed session edits to the loaded file. System/display settings go
to `system.json`; **System → Export** writes a session copy. **Windowed** takes
effect on the next launch; eye spacing applies live.

Theme media rows preview stills on hover; animation rows are labeled. Runtime
theme solos appear with a **runtime solo (not saved)** banner. F1 reports
loaded/total stills, failed image decodes, and animation counts separately:
`0/0 img 0 failed 17 anim` can be a valid animation-only theme.

F1/F2 render on the desktop with OpenXR attached, never in the headset.
See [OpenXR output](spec-xr-unified.md).

## Global hide/show: Shift+F11

On Windows and X11, Shift+F11 works while another application has focus.

- First press hides, pauses, and mutes.
- Next press restores visibility and the requested pause/mute state.
- Explicit pause/resume or mute commands while hidden change what show restores.
- Hiding clears overlay mode, making the restored window interactive.

The process stays alive. Registration failure is reported at startup.
In the hotkey-only case with no tray or usable panel, a press while already
hidden quits instead of restoring, preserving an exit route.

`--hidden` starts hidden. `--muted` separately requests mute that survives show.

## Windows tray

The menu provides Hide/Show, Overlay, opacity changes, Paused, Show control panel,
and Quit. **Show control panel** shows the window and clears click-through mode
so the panel can receive input. Linux has the global hotkey but no tray.

## Overlay

`--overlay` or runtime overlay controls make the window click-through,
translucent, and always on top. Its own keys cannot reach it. Use:

- **Shift+F11:** hide; showing again clears overlay.
- **Windows tray:** turn Overlay off, open the panel, or quit.
- **Command/MCP:** `overlay off`, `ui on`, or `hide`.
- **Ctrl+C:** orderly shutdown when launched with `--overlay` from a terminal.

`--overlay_opacity=0.35` sets initial whole-window opacity (0–1).

## External control

`--command_port=9191` opens TCP on `127.0.0.1`; `--mcp` exposes the same
actions as stdio tools. See the [command reference](spec-mcp-ambient-daemon.md)
and [MCP setup](mcp-install.md).

There is no `start`/`stop` or live session-loading command. Use
`pause`/`resume`, or `hide`/`show` for visibility and silence together.

## Console progress

Playback appends a line when a pattern traversal begins, a named section changes
or restarts, or the primary/secondary theme lanes change. Section paths retain
their nesting, with local position and length in content frames:

```text
themes: primary="Landscape" secondary="Abstract"
pattern scene #1: scene 1/120f | scene/slow 1/60f
  phase scene #1: scene 61/120f | scene/fast 1/60f
```

The traversal number counts starts during this run. Up to eight active named
sections appear per line. Rapid pattern/section events are coalesced to at most
four progress lines per second, with counts of the intervening traversals and
transitions; these lines are a playback summary, not a complete event trace.
Theme changes are reported separately. No per-frame progress redraw is performed.

These records go to stderr, keeping MCP's JSON on stdout. Lines append rather
than overwrite the console, preserving media errors, XR diagnostics, and useful
redirected logs. F1 supplies the continuously updated schedule view.

Implementation: [main.cpp](../src/trance/main.cpp),
[system_control.cpp](../src/trance/platform/system_control.cpp).
