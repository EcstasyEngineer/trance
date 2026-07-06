#ifndef TRANCE_SRC_TRANCE_PLATFORM_OVERLAY_HINTS_H
#define TRANCE_SRC_TRANCE_PLATFORM_OVERLAY_HINTS_H
// Native window management for click-through overlay mode (X11 / Win32): borderless
// always-on-top, uniform window opacity, and an empty input region so all clicks/keys
// pass through to the desktop/apps beneath. Pure platform windowing -- no GL, no
// drawing; the renderer just hands over its native window handle.
//
// Per-pixel alpha (an ARGB visual) has not been explored since the SFML 3 migration;
// today the overlay is UNIFORM whole-window opacity only.
#pragma warning(push, 0)
#include <SFML/Window/WindowHandle.hpp>
#pragma warning(pop)

struct OverlayConfig {
  bool enabled = false;
  // 0 (fully transparent) .. 1 (fully opaque). Applies to the whole window uniformly.
  float opacity = 0.35f;
};

// Runtime overlay toggle: both callable on a live, MAPPED window from the main loop's
// apply seam (fed by the `overlay ...` verbs, the F2 UI's Overlay section, and the
// tray/hotkey requests). apply_overlay_hints() is idempotent -- calling it again while
// already on just rewrites the opacity, which is the live opacity-change path;
// clear_overlay_hints() restores a normal interactive window.
void apply_overlay_hints(sf::WindowHandle handle, float opacity);
void clear_overlay_hints(sf::WindowHandle handle);

// Startup variant (--overlay, called from the ScreenRenderer constructor): the window
// is NOT yet mapped there, and per EWMH an unmapped X11 window takes _NET_WM_STATE as
// a direct property write rather than a ClientMessage. On Win32 the ex-style path
// works the same mapped or unmapped, so this just forwards to apply_overlay_hints().
void apply_overlay_hints_at_startup(sf::WindowHandle handle, float opacity);

#endif
