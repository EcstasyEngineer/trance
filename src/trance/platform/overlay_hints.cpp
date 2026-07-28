#include <trance/platform/overlay_hints.h>
#include <algorithm>
#include <iostream>

#if defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#elif defined(_WIN32)
#pragma warning(push, 0)
#include <windows.h>
#pragma warning(pop)
#endif

namespace
{
#if defined(__linux__)
  // All of this is a no-op (returns without touching anything) if we're not actually
  // on an X11 session (e.g. Wayland-only, or DISPLAY unset) -- overlay mode then
  // degrades gracefully to an always-on-top borderless window without click-through
  // or translucency, rather than crashing.
  Display* x11_open_overlay_display()
  {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
      std::cerr << "overlay mode: couldn't open X11 display (Wayland-only session?); "
                << "click-through and translucency hints skipped" << std::endl;
    }
    return display;
  }

  // Whole-window translucency. No per-pixel alpha in SFML's default visual -- see
  // OverlayConfig.
  void x11_write_opacity(Display* display, Window window, float opacity)
  {
    Atom opacity_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    if (opacity_atom != None) {
      auto value = static_cast<unsigned long>(
          std::clamp(opacity, 0.f, 1.f) * static_cast<float>(0xffffffffu));
      XChangeProperty(display, window, opacity_atom, XA_CARDINAL, 32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(&value), 1);
    }
  }

  // click_through = true installs an EMPTY input shape (the window receives no input
  // at all; clicks/keys fall through to whatever is beneath it on the desktop);
  // false restores the default full-window input region (None mask + ShapeSet).
  void x11_set_click_through(Display* display, Window window, bool click_through)
  {
    int shape_event_base = 0, shape_error_base = 0;
    if (!XShapeQueryExtension(display, &shape_event_base, &shape_error_base)) {
      std::cerr << "overlay mode: X11 Shape extension unavailable; window will not be "
                << "click-through" << std::endl;
      return;
    }
    if (click_through) {
      XShapeCombineRectangles(display, window, ShapeInput, 0, 0, nullptr, 0, ShapeSet, 0);
    } else {
      XShapeCombineMask(display, window, ShapeInput, 0, 0, None, ShapeSet);
    }
  }

  // The _NET_WM_STATE atoms overlay mode toggles: skip taskbar/pager (don't clutter
  // window switchers) + always-above (stay on top of normal windows). Fills `atoms`,
  // returns how many resolved.
  int x11_overlay_state_atoms(Display* display, Atom (&atoms)[3])
  {
    int count = 0;
    for (const char* name :
         {"_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER", "_NET_WM_STATE_ABOVE"}) {
      Atom atom = XInternAtom(display, name, False);
      if (atom != None) {
        atoms[count++] = atom;
      }
    }
    return count;
  }

  // Runtime path: the window is already MAPPED, so per EWMH the states must be changed
  // with a _NET_WM_STATE ClientMessage sent to the root window -- the WM only honours
  // direct property writes on not-yet-mapped windows. data.l[0]: 1 = _NET_WM_STATE_ADD,
  // 0 = _NET_WM_STATE_REMOVE.
  void x11_send_wm_state(Display* display, Window window, bool add)
  {
    Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    if (state_atom == None) {
      return;
    }
    Atom atoms[3];
    int count = x11_overlay_state_atoms(display, atoms);
    // Each ClientMessage carries up to two atoms (data.l[1] / data.l[2]).
    for (int i = 0; i < count; i += 2) {
      XEvent event = {};
      event.xclient.type = ClientMessage;
      event.xclient.window = window;
      event.xclient.message_type = state_atom;
      event.xclient.format = 32;
      event.xclient.data.l[0] = add ? 1 : 0;
      event.xclient.data.l[1] = static_cast<long>(atoms[i]);
      event.xclient.data.l[2] = i + 1 < count ? static_cast<long>(atoms[i + 1]) : 0;
      event.xclient.data.l[3] = 1;  // source indication: normal application
      XSendEvent(display, DefaultRootWindow(display), False,
                 SubstructureRedirectMask | SubstructureNotifyMask, &event);
    }
  }
#elif defined(_WIN32)
  constexpr int kOverlayOverscanPixels = 1;

  struct Win32OverlayRestore {
    HWND hwnd = nullptr;
    RECT rect = {};
    bool valid = false;
    // Latched on the first store attempt for this hwnd, success or not. Without it a
    // failed initial GetWindowRect would retry on a later opacity re-apply -- by which
    // point the window is already overscanned, and the overscanned rect would be
    // captured as the "restore" target.
    bool attempted = false;
  };

  Win32OverlayRestore g_win32_overlay_restore;

  RECT win32_monitor_rect(HWND hwnd)
  {
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
      return info.rcMonitor;
    }
    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }

  void win32_store_overlay_rect(HWND hwnd)
  {
    if (g_win32_overlay_restore.attempted && g_win32_overlay_restore.hwnd == hwnd) {
      return;
    }
    g_win32_overlay_restore = {};
    g_win32_overlay_restore.hwnd = hwnd;
    g_win32_overlay_restore.attempted = true;
    g_win32_overlay_restore.valid = GetWindowRect(hwnd, &g_win32_overlay_restore.rect) != FALSE;
    if (!g_win32_overlay_restore.valid) {
      std::cerr << "overlay mode: GetWindowRect failed (error " << GetLastError()
                << "); overlay-off may not restore the previous window bounds" << std::endl;
    }
  }

  void win32_apply_overlay_bounds(HWND hwnd)
  {
    win32_store_overlay_rect(hwnd);
    const RECT monitor = win32_monitor_rect(hwnd);
    const int overscan = kOverlayOverscanPixels;
    const int x = monitor.left - overscan;
    const int y = monitor.top - overscan;
    const int width = (monitor.right - monitor.left) + 2 * overscan;
    const int height = (monitor.bottom - monitor.top) + 2 * overscan;
    if (!SetWindowPos(hwnd, nullptr, x, y, width, height,
                      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
      std::cerr << "overlay mode: overscan SetWindowPos failed (error " << GetLastError()
                << "); DWM may still treat the window as exact fullscreen" << std::endl;
    }
  }

  void win32_restore_overlay_bounds(HWND hwnd)
  {
    if (g_win32_overlay_restore.valid && g_win32_overlay_restore.hwnd == hwnd) {
      const RECT rect = g_win32_overlay_restore.rect;
      if (!SetWindowPos(hwnd, HWND_NOTOPMOST, rect.left, rect.top, rect.right - rect.left,
                        rect.bottom - rect.top, SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        std::cerr << "overlay mode: restore SetWindowPos failed (error " << GetLastError()
                  << "); window keeps the overscanned overlay bounds" << std::endl;
      }
      // Cleared even on failure: the next engage must capture a fresh rect rather
      // than trust one that a failed restore may have left meaningless.
      g_win32_overlay_restore = {};
      return;
    }
    g_win32_overlay_restore = {};
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
#endif
}

void apply_overlay_hints(sf::WindowHandle handle, float opacity)
{
#if defined(__linux__)
  Display* display = x11_open_overlay_display();
  if (!display) {
    return;
  }
  Window window = static_cast<Window>(handle);
  x11_write_opacity(display, window, opacity);
  x11_send_wm_state(display, window, true);
  x11_set_click_through(display, window, true);
  XFlush(display);
  XCloseDisplay(display);
#elif defined(_WIN32)
  // UNVALIDATED on real Windows hardware. Mirrors the X11 path's intent -- always-on-
  // top, uniform translucency, click-through -- via the documented WS_EX_LAYERED |
  // WS_EX_TRANSPARENT combination.
  //
  // WS_EX_NOACTIVATE: an overlay must never take activation/focus -- without it the
  // window can still be activated (e.g. via alt-tab or the taskbar) even though
  // WS_EX_TRANSPARENT makes it mouse-invisible.
  //
  // DWM can promote an exactly-fullscreen borderless TOPMOST GL window out of the
  // composited/redirected path (fullscreen optimization / independent flip), where
  // LWA_ALPHA has no effect -- the window renders fully opaque while click-through
  // keeps working. Countermeasures: 1px overscan (win32_apply_overlay_bounds) so the
  // window is never exactly fullscreen, plus stripping WS_EX_LAYERED and forcing a
  // frame change so re-adding the styles + alpha is a fresh transition DWM must
  // re-evaluate; SetLayeredWindowAttributes failures are logged instead of assumed to
  // have taken. DirectComposition is the next rung if this still fails on hardware.
  HWND hwnd = handle;
  win32_apply_overlay_bounds(hwnd);
  LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (ex_style & WS_EX_LAYERED) {
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  ex_style |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  if (!SetLayeredWindowAttributes(
          hwnd, 0, static_cast<BYTE>(std::clamp(opacity, 0.f, 1.f) * 255.f), LWA_ALPHA)) {
    std::cerr << "overlay mode: SetLayeredWindowAttributes failed (error "
              << GetLastError() << "); window may render opaque" << std::endl;
  }
#else
  (void)handle;
  (void)opacity;
#endif
}

void clear_overlay_hints(sf::WindowHandle handle)
{
#if defined(__linux__)
  Display* display = x11_open_overlay_display();
  if (!display) {
    return;
  }
  Window window = static_cast<Window>(handle);
  Atom opacity_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
  if (opacity_atom != None) {
    XDeleteProperty(display, window, opacity_atom);
  }
  x11_send_wm_state(display, window, false);
  x11_set_click_through(display, window, false);
  XFlush(display);
  XCloseDisplay(display);
#elif defined(_WIN32)
  // UNVALIDATED (see apply_overlay_hints). Restore full alpha before stripping
  // WS_EX_LAYERED, then drop the overlay ex-styles and the topmost bit.
  HWND hwnd = handle;
  SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
  LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  ex_style &= ~static_cast<LONG_PTR>(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                     WS_EX_NOACTIVATE);
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
  win32_restore_overlay_bounds(hwnd);
  // The restore above deliberately uses SWP_NOACTIVATE (it must not steal activation
  // mid-resize), which leaves the window styled-interactive but NOT activated -- and an
  // unactivated window never delivers WM_SETFOCUS, so imgui-SFML's focus latch stays
  // false and swallows every click on the panel we just made clickable. Activate it
  // explicitly here, after the ex-styles are back (SetForegroundWindow is a no-op while
  // WS_EX_NOACTIVATE is still set).
  SetForegroundWindow(hwnd);
  SetActiveWindow(hwnd);
#else
  (void)handle;
#endif
}

void focus_window(sf::WindowHandle handle)
{
#if defined(_WIN32)
  // sf::Window::requestFocus() only FLASHES the taskbar button when another process
  // owns the foreground; the overlay's own tray helper window is exactly that case
  // (show_tray_menu SetForegroundWindow's the hidden helper), so take activation
  // directly as well.
  HWND hwnd = handle;
  SetForegroundWindow(hwnd);
  SetActiveWindow(hwnd);
#else
  // X11 (and every other platform SFML supports): requestFocus() already does the right
  // thing -- _NET_ACTIVE_WINDOW / XSetInputFocus -- and the caller has issued it, so
  // there is nothing platform-specific left to add here.
  (void)handle;
#endif
}

void apply_overlay_hints_at_startup(sf::WindowHandle handle, float opacity)
{
#if defined(__linux__)
  // Unmapped window: _NET_WM_STATE goes on as a direct property write and the WM picks
  // it up at map time. Opacity + input shape are shared with the runtime path and work
  // either way.
  Display* display = x11_open_overlay_display();
  if (!display) {
    return;
  }
  Window window = static_cast<Window>(handle);
  x11_write_opacity(display, window, opacity);
  Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
  if (state_atom != None) {
    Atom atoms[3];
    int count = x11_overlay_state_atoms(display, atoms);
    if (count) {
      XChangeProperty(display, window, state_atom, XA_ATOM, 32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(atoms), count);
    }
  }
  x11_set_click_through(display, window, true);
  XFlush(display);
  XCloseDisplay(display);
#elif defined(_WIN32)
  // Win32 ex-styles work the same mapped or unmapped -- one path for both.
  apply_overlay_hints(handle, opacity);
#else
  (void)handle;
  (void)opacity;
#endif
}
