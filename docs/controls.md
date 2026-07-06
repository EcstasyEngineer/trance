# Runtime controls

All the ways to control a running `trance` player. Implementation: in-window keys in
`handle_events()` (`src/trance/main.cpp`); everything global lives in
`src/trance/platform/system_control.{h,cpp}`.

## In-window keys

Work while the trance window has focus (realtime mode):

| Key | Action |
|---|---|
| **Escape** (or closing the window) | Quit |
| **F1** | Toggle the debug overlay (visual/cycler/theme state) |
| **F2** | Toggle the in-app control panel (ImGui) |
| **M** | Toggle audio mute |

## Global safety hotkey — Shift+F11

Registered system-wide (Win32 `RegisterHotKey` on Windows, `XGrabKey` on X11), so it
works **no matter which application has focus** — including when the trance window is a
click-through overlay that can't receive input at all.

- **First press** — panic switch: overlay off, playback paused, control panel shown.
- **Second press** (from that safe state — overlay already off and paused) — quit
  outright.

Holding the key fires once, not an autorepeat stream. If registration fails (another
app owns the combination), a warning is printed at startup and the tray menu is the
fallback.

## System tray icon (Windows)

A tray icon appears for every realtime run. Its menu mirrors the runtime controls:

- **Safety stop** (Shift+F11) — same semantics as the hotkey above
- **Overlay** — toggle the click-through overlay (checkmark shows live state)
- **Paused** — toggle playback pause (checkmark shows live state)
- **Show control panel** — bring up the F2 panel (disengages the overlay first — a
  click-through window can't host an interactive panel)
- **Quit**

No Linux tray yet; X11 gets the global hotkey, which is the safety-critical half.

## Overlay mode — the escape routes

With the overlay engaged (`--overlay`, or toggled at runtime), the window is
click-through **by design**: no in-window key — not even Escape or F2 — can reach it.
Every way out lives outside the window:

- **Shift+F11** — the global safety hotkey (first press disengages, second quits)
- the **tray icon** menu (Windows)
- the **command channel** (`stop`, `overlay off`, … — see below)
- **Ctrl+C** in the launching terminal (SIGINT/SIGTERM are handled cleanly)

## Command channel

`--command_port <port>` opens a line-protocol control socket (start/stop, pause,
overlay on/off/opacity, status, screenshot, …). The verb reference is in
[spec-mcp-ambient-daemon.md](spec-mcp-ambient-daemon.md).
