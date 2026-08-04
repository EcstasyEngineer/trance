# Runtime controls

All the ways to control a running `trance` player. Implementation: in-window keys in
`handle_events()` (`src/trance/main.cpp`); everything global lives in
`src/trance/platform/system_control.{h,cpp}`.

## In-window keys

Work while the trance window has focus (realtime mode):

| Key | Action |
|---|---|
| **Escape** | Quit trance |
| **F2** | Toggle the in-app control panel (ImGui) |
| **F1** | Toggle the debug overlay (visual/cycler/theme state) |

| **M** | Toggle audio mute |

Escape quits outright; the other ways out are closing the window, the tray menu's
**Quit**, and the control panel's **Quit trance** button. While a control-panel text
field is being edited, Escape cancels that edit instead of quitting.

The F1 overlay's THEMES block reads `<loaded>/<total> img  <n> anim` per slot. Stills are
cached, so they have a loaded/total ratio; gifs are streamed on demand and have no such
ratio, which is why they are counted separately. **`0/0 img  17 anim` is a healthy folder
of gifs, not an empty theme** — a theme draws whichever kind it has, so an all-gif theme
serves `image` draws from its gifs and a stills-only theme serves `anim` draws from its
stills.

Every edit made in the F2 panel is saved as you make it — visual/theme weights, colours,
pattern text and the entrainment bed go back to the loaded session file, renderer and
window settings to `system.json`. There is no Save button; **System → Export** writes a
copy of the session somewhere else and leaves the live file alone.

The F2 panel is **not built in VR mode** (there is no single flat 2D pass to composite
it onto). Its **System → Renderer** radios persist to
`system.json` immediately but only take effect on the next launch;
`--renderer=monitor|openvr|openxr` overrides them for a single run. See the README's
[VR setup](../README.md#vr-setup) section — in particular, a `system.json` with no
`renderer` key is monitor mode, which SteamVR then shows as a flat virtual desktop.

## Global hide-everything hotkey — Shift+F11

Registered system-wide (Win32 `RegisterHotKey` on Windows, `XGrabKey` on X11), so it
works **no matter which application has focus** — including when the trance window is a
click-through overlay that can't receive input at all.

- **First press** — hide everything instantly: window invisible, playback paused,
  audio muted. The process stays alive (tray icon, hotkey, command channel).
- **Next press** — restore: window visible again, with the pause/mute state from
  before the hide brought back (a pause/resume explicitly commanded *while* hidden —
  tray or command channel — updates what gets restored; playback itself stays idle
  until the window is shown again). It never quits — with one exception: in hotkey-only
  configurations with no other quit surface (Linux VR, or Linux fullscreen when the
  ImGui panel failed to initialise — no tray, no panel), a press while already hidden
  quits instead of restoring, so an orderly exit always exists.

Holding the key fires once, not an autorepeat stream. If registration fails (another
app owns the combination), a warning is printed at startup and the tray menu is the
fallback.

## System tray icon (Windows)

A tray icon appears for every realtime run. Its menu mirrors the runtime controls:

- **Hide everything / Show** (Shift+F11) — drives the same hidden state as the hotkey
  above; the item performs exactly the action its label names (an explicit hide or
  show, not a blind toggle — so a menu left open across a state change can't invert
  your intent)
- **Overlay** — toggle the click-through overlay (checkmark shows live state)
- **Overlay opacity + / −** — nudge the live overlay opacity in 0.1 steps (clamped
  to 0..1)
- **Paused** — toggle playback pause (checkmark shows live state)
- **Show control panel** — bring up the F2 panel (disengages the overlay and un-hides
  first — a click-through or invisible window can't host an interactive panel)
- **Quit**

No Linux tray yet; X11 gets the global hotkey, which is the safety-critical half.

## Overlay mode — the escape routes

With the overlay engaged (`--overlay`, or toggled at runtime), the window is
click-through **by design**: no in-window key — not even Escape or F2 — can reach it.
Every way out lives outside the window:

- **Shift+F11** — the global hide-everything toggle (window hidden + paused + muted;
  hiding also clears the click-through state, so the restored window is interactive)
- the **tray icon** menu (Windows) — Quit lives here
- the **command channel** (`stop`, `overlay off`, `hide`/`show`, … — see below)
- **Ctrl+C** in the launching terminal (SIGINT/SIGTERM are handled cleanly)

## Command channel

`--command_port <port>` opens a line-protocol control socket (start/stop, pause,
overlay on/off/opacity, hide/show, status, screenshot, …). The verb reference is in
[spec-mcp-ambient-daemon.md](spec-mcp-ambient-daemon.md).
