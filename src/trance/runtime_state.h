#ifndef TRANCE_SRC_TRANCE_RUNTIME_STATE_H
#define TRANCE_SRC_TRANCE_RUNTIME_STATE_H
// The shared live-control state (docs/spec-mcp-ambient-daemon.md). Every control
// surface -- command-channel verbs, the F2 UI's Overlay section, the tray/hotkey
// requests -- only WRITES these fields; play_session()'s per-frame apply seam is the
// single place that reconciles the real window/audio against them. That's what keeps
// the surfaces structurally unable to disagree. Render-thread only.
#include <string>

struct CommandRuntimeState {
  // Gates director.update()/theme_bank->advance_frames() in the main loop; the
  // playlist clock is frozen there too, and Audio pauses via set_paused(). While
  // hidden this is only the user's pause INTENT (playback idles on `hidden` itself,
  // and set_paused leaves the audio to the hide seam), so a pause/resume commanded
  // while hidden survives the `show` restore.
  bool paused = false;
  // Hide-everything (silent running): window invisible, playback idle, audio muted;
  // the process (command channel + tray + hotkey) stays alive. Written by the `hide`/
  // `show` verbs, the Shift+F11 toggle, and the tray's explicit Hide/Show item; the
  // apply seam hides/shows the window, stashes/restores mute, and on show resumes
  // audio iff `paused` says play.
  bool hidden = false;
  // Protocol-complete stub: the spec scopes actual intensity wiring as TBD, so the
  // verb just stores the value here for a future consumer.
  float intensity = 1.f;
  // Live overlay intent; the apply seam diffs these against what the window currently
  // has and pushes changes via apply_overlay_hints/clear_overlay_hints.
  bool overlay_on = false;
  float overlay_opacity = 0.35f;
  // One-shot request to pull keyboard/mouse focus back to the trance window, set by
  // every path that makes the window interactive again (show_control_panel, F2 while
  // the overlay is engaged). Consumed and cleared by the apply seam once the window is
  // out of click-through/hidden state: without it, an overlay-off leaves the window
  // unfocused (Win32 restores its styles with SWP_NOACTIVATE, and the tray's
  // SetForegroundWindow left the hidden helper window in front), and imgui-SFML drops
  // every mouse event while its focus latch is false -- panel drawn, clicks ignored.
  bool focus_requested = false;
  // `screenshot PATH`: consumed by the renderer's pre-display hook (which sees the
  // fully composited back buffer) on the next rendered frame; empty = nothing pending.
  std::string screenshot_path;
};

#endif
