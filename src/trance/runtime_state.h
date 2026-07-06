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
  // playlist clock is frozen there too, and Audio pauses via set_paused().
  bool paused = false;
  // Protocol-complete stub: the spec scopes actual intensity wiring as TBD, so the
  // verb just stores the value here for a future consumer.
  float intensity = 1.f;
  // Live overlay intent; the apply seam diffs these against what the window currently
  // has and pushes changes via apply_overlay_hints/clear_overlay_hints.
  bool overlay_on = false;
  float overlay_opacity = 0.35f;
  // `screenshot PATH`: consumed by the renderer's pre-display hook (which sees the
  // fully composited back buffer) on the next rendered frame; empty = nothing pending.
  std::string screenshot_path;
};

#endif
