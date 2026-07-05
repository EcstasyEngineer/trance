#ifndef TRANCE_SRC_TRANCE_PLATFORM_SYSTEM_CONTROL_H
#define TRANCE_SRC_TRANCE_PLATFORM_SYSTEM_CONTROL_H
// System tray icon + global safety hotkey (#27 follow-up).
//
// Once the overlay engages, the window is click-through BY DESIGN, so no in-window
// control (F2 UI, Escape) can ever reach it again -- every off-switch must live
// OUTSIDE the window. This module is that outside: a background thread owning
//   - a global Shift+F11 "safety" hotkey (Win32 RegisterHotKey / X11 XGrabKey),
//     which works no matter which application has focus, and
//   - on Windows, a system tray icon (Shell_NotifyIcon) with a menu mirroring the
//     runtime controls (safety stop, overlay toggle, pause, show F2 panel, quit).
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
  // Global panic switch (Shift+F11): overlay off, playback paused, control panel
  // shown. If already in that safe state, a second press quits outright.
  kSafety,
  kOverlayToggle,
  kPlayPauseToggle,
  // Bring the F2 control panel up (forces the overlay off first -- a click-through
  // window can't host an interactive panel).
  kShowUi,
  kQuit,
};

class SystemControl
{
public:
  SystemControl();
  ~SystemControl();

  SystemControl(const SystemControl&) = delete;
  SystemControl& operator=(const SystemControl&) = delete;

  // Requests queued by the tray menu / global hotkey since the last call.
  // Render thread only.
  std::vector<ControlRequest> drain();

  // Mirror of the live runtime state, read by the tray menu for its checkmarks.
  void set_status(bool overlay_on, bool paused);
  bool overlay_on() const { return _overlay_on; }
  bool paused() const { return _paused; }

  // Queue a request. Platform-thread side (the tray wndproc / hotkey loop call this);
  // public because the Win32 wndproc is a free function.
  void push(ControlRequest request);

private:
  void thread_main();

  std::mutex _mutex;
  std::vector<ControlRequest> _requests;
  std::atomic<bool> _overlay_on = false;
  std::atomic<bool> _paused = false;
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
