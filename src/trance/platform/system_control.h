#ifndef TRANCE_SRC_TRANCE_PLATFORM_SYSTEM_CONTROL_H
#define TRANCE_SRC_TRANCE_PLATFORM_SYSTEM_CONTROL_H
// System tray icon + global hide-everything hotkey.
//
// Once the overlay engages, the window is click-through BY DESIGN, so no in-window
// control (F2 UI, Escape) can ever reach it again -- every off-switch must live
// OUTSIDE the window. This module is that outside: a background thread owning
//   - a global Shift+F11 hide-everything toggle (Win32 RegisterHotKey / X11 XGrabKey),
//     which works no matter which application has focus, and
//   - on Windows, a system tray icon (Shell_NotifyIcon) with a menu mirroring the
//     runtime controls (hide/show, overlay toggle + opacity nudge, pause, show F2
//     panel, quit).
//     No Linux tray in v0: a modern tray needs StatusNotifierItem over D-Bus (a new
//     dependency); X11 already gets the global hotkey, which is the safety-critical
//     half.
//
// Threading contract mirrors CommandChannel's: the platform thread only ever queues
// ControlRequests; the render thread drains them once per frame (main.cpp, right
// after handle_commands) and is the only thing that touches Director/Audio/window.
// set_status() mirrors live state back for the tray menu's checkmarks (atomics, any
// thread).
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

enum class ControlRequest {
  // Global hide-everything toggle (Shift+F11, and the tray's Hide/Show item): first
  // press hides the window / pauses / mutes instantly, next press restores whatever
  // pause/mute state existed before hiding. Never quits where a real quit surface
  // exists (tray Quit, window close, or the F2 panel's Quit button -- all kQuit's
  // job); the ONE exception is hotkey-only configurations (kHasTrayQuit false AND no
  // F2 panel, i.e. a failed ImGui init on Linux), where nothing else can
  // ever quit the process -- there the drain point in main.cpp lets a press while
  // already hidden quit, preserving Shift+F11's old second-press-quits escape hatch.
  kSafety,
  // Tray Hide/Show item: EXPLICIT target state, not a toggle. The item's label
  // promises an absolute action ("Show" / "Hide everything"), but TrackPopupMenu's
  // modal loop keeps dispatching WM_HOTKEY on the tray thread, so Shift+F11 (or a
  // channel hide/show) can flip the state while the menu sits open -- a toggle
  // request would then invert the user's intent. The drain point just writes
  // hidden = true/false; the apply seam no-ops when the state already matches
  // (same idempotent shape as the `hide`/`show` verbs).
  kHide,
  kShow,
  kOverlayToggle,
  // Tray "Overlay opacity +/-": nudge the live overlay opacity by 0.1, clamped to
  // [0, 1] at the drain point (command_protocol::clamp01, same clamp the verbs use).
  kOverlayOpacityUp,
  kOverlayOpacityDown,
  kPlayPauseToggle,
  // Bring the F2 control panel up (forces the overlay off first -- a click-through
  // window can't host an interactive panel -- and un-hides if hidden).
  kShowUi,
  kQuit,
};

class SystemControl
{
public:
  // True where this module provides a tray menu with a Quit item (Windows). Where it
  // doesn't (Linux: global hotkey only), the kSafety drain point in main.cpp falls
  // back to second-press-quits when no F2 panel exists either -- see kSafety above.
#if defined(_WIN32)
  static constexpr bool kHasTrayQuit = true;
#else
  static constexpr bool kHasTrayQuit = false;
#endif

  SystemControl();
  ~SystemControl();

  SystemControl(const SystemControl&) = delete;
  SystemControl& operator=(const SystemControl&) = delete;

  // Requests queued by the tray menu / global hotkey since the last call.
  // Render thread only.
  std::vector<ControlRequest> drain();

  // Mirror of the live runtime state, read by the tray menu for its checkmarks and
  // the Hide/Show item's dynamic label.
  void set_status(bool overlay_on, bool paused, bool hidden);
  bool overlay_on() const { return _overlay_on; }
  bool paused() const { return _paused; }
  bool hidden() const { return _hidden; }

  // Queue a request. Platform-thread side (the tray wndproc / hotkey loop call this);
  // public because the Win32 wndproc is a free function.
  void push(ControlRequest request);

private:
  void thread_main();

  std::mutex _mutex;
  std::vector<ControlRequest> _requests;
  std::atomic<bool> _overlay_on = false;
  std::atomic<bool> _paused = false;
  std::atomic<bool> _hidden = false;
  std::atomic<bool> _stop = false;
  // Set by thread_main on the way out; the destructor polls it so teardown can't
  // race a thread that is still starting up (see ~SystemControl).
  std::atomic<bool> _thread_done = false;
#if defined(_WIN32)
  // HWND of the hidden tray/hotkey window, stored as void* to keep <windows.h> out
  // of this header. Set by the thread once created; used by the destructor to post
  // the teardown message.
  std::atomic<void*> _hwnd = nullptr;
#endif
  std::thread _thread;
};

#endif
