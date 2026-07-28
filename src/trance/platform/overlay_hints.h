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
// `activate`: on Win32 the clear restores the window's ex-styles with SWP_NOACTIVATE, so
// the window comes back styled-interactive but NOT activated -- and an unactivated window
// never delivers WM_SETFOCUS, leaving imgui-SFML's focus latch false and every click on
// the panel swallowed. Pass true only when this clear is the LAST thing that touches the
// window, i.e. nobody else is going to activate it:
//   - overlay-off: pass FALSE. The main loop's deferred focus seam activates instead (it
//     also issues requestFocus(), the cross-platform half this can't do). Activating here
//     too would activate the window twice for one toggle.
//   - hide: pass FALSE. The window is about to be hidden; taking the foreground first
//     would briefly steal it from whatever the user is actually doing.
// No effect off Win32 -- X11 needs no activation here.
void clear_overlay_hints(sf::WindowHandle handle, bool activate = false);

// Startup variant (--overlay, called from the ScreenRenderer constructor): the window
// is NOT yet mapped there, and per EWMH an unmapped X11 window takes _NET_WM_STATE as
// a direct property write rather than a ClientMessage. On Win32 the ex-style path
// works the same mapped or unmapped, so this just forwards to apply_overlay_hints().
void apply_overlay_hints_at_startup(sf::WindowHandle handle, float opacity);

// Pull activation/keyboard focus back to the window, after sf::Window::requestFocus().
// No-op on X11 (requestFocus() covers it); on Win32 it adds the SetForegroundWindow +
// SetActiveWindow pair that requestFocus() alone won't do when a foreign window (the
// tray helper) holds the foreground. Call only on an interactive, non-click-through
// window -- activating a WS_EX_NOACTIVATE overlay is a no-op by design.
void focus_window(sf::WindowHandle handle);

#endif
