#include <trance/platform/system_control.h>
#include <chrono>
#include <iostream>

#if defined(_WIN32)
#pragma warning(push, 0)
#include <windows.h>
#include <shellapi.h>
#pragma warning(pop)
#elif defined(__linux__)
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <sys/select.h>
#endif

SystemControl::SystemControl()
{
#if defined(_WIN32) || defined(__linux__)
  _thread = std::thread{[this] { _thread_done = false; thread_main(); _thread_done = true; }};
#else
  _thread_done = true;
#endif
}

SystemControl::~SystemControl()
{
  _stop = true;
#if defined(_WIN32)
  // Wake the message loop: WM_CLOSE tears down the window, whose WM_DESTROY posts
  // the quit message. The thread may still be STARTING UP here (window not created
  // yet, _hwnd unset) -- a single fire-and-forget post could then be skipped and the
  // join below would hang on GetMessage forever. Keep nudging until the thread
  // reports done: either the window appears (post lands; the queue holds it even if
  // GetMessage isn't running yet) or creation failed and the thread exits by itself.
  while (!_thread_done) {
    if (void* hwnd = _hwnd.load()) {
      PostMessage(static_cast<HWND>(hwnd), WM_CLOSE, 0, 0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
#endif
  // The Linux loop polls _stop every 200ms (select timeout); nothing to wake.
  if (_thread.joinable()) {
    _thread.join();
  }
}

std::vector<ControlRequest> SystemControl::drain()
{
  std::lock_guard<std::mutex> lock{_mutex};
  std::vector<ControlRequest> requests;
  requests.swap(_requests);
  return requests;
}

void SystemControl::set_status(bool overlay_on, bool paused)
{
  _overlay_on = overlay_on;
  _paused = paused;
}

void SystemControl::push(ControlRequest request)
{
  std::lock_guard<std::mutex> lock{_mutex};
  _requests.push_back(request);
}

#if defined(_WIN32)

// UNVALIDATED on real Windows (no Windows box in this environment) -- same status as
// render.cpp's Win32 overlay-hint path. Standard-issue Shell_NotifyIcon +
// RegisterHotKey plumbing; validate alongside #27's overlay hints.
namespace
{
  constexpr UINT kTrayCallbackMessage = WM_APP + 1;
  constexpr int kHotkeyId = 1;
  constexpr UINT kMenuSafety = 1;
  constexpr UINT kMenuOverlay = 2;
  constexpr UINT kMenuPause = 3;
  constexpr UINT kMenuShowUi = 4;
  constexpr UINT kMenuQuit = 5;

  // Re-broadcast by explorer.exe when the taskbar (re)starts; the icon must be
  // re-added then or it silently disappears after an explorer crash/restart. (This is
  // also why the tray window is an ordinary hidden top-level window and not a
  // message-only HWND_MESSAGE one -- message-only windows don't receive broadcasts.)
  UINT g_taskbar_created_message = 0;

  NOTIFYICONDATAW make_icon_data(HWND hwnd)
  {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, L"trance");
    return data;
  }

  void show_tray_menu(HWND hwnd, bool overlay_on, bool paused)
  {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
      return;
    }
    AppendMenuW(menu, MF_STRING, kMenuSafety, L"Safety stop\tShift+F11");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (overlay_on ? MF_CHECKED : 0u), kMenuOverlay, L"Overlay");
    AppendMenuW(menu, MF_STRING | (paused ? MF_CHECKED : 0u), kMenuPause, L"Paused");
    AppendMenuW(menu, MF_STRING, kMenuShowUi, L"Show control panel");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");
    // The documented TrackPopupMenu-from-a-tray-icon dance (KB135788): the hidden
    // window must be foreground or the menu won't dismiss on an outside click, and
    // the trailing WM_NULL works around the menu re-appearing.
    SetForegroundWindow(hwnd);
    POINT cursor;
    GetCursorPos(&cursor);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
  }

  LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
  {
    // The SystemControl* is stashed in GWLP_USERDATA right after CreateWindow;
    // messages arriving before that (WM_CREATE etc.) fall through to DefWindowProc.
    auto* control = reinterpret_cast<SystemControl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (control) {
      if (msg == kTrayCallbackMessage) {
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
          show_tray_menu(hwnd, control->overlay_on(), control->paused());
        } else if (lparam == WM_LBUTTONDBLCLK) {
          control->push(ControlRequest::kShowUi);
        }
        return 0;
      }
      if (msg == WM_HOTKEY && wparam == kHotkeyId) {
        control->push(ControlRequest::kSafety);
        return 0;
      }
      if (msg == WM_COMMAND) {
        switch (LOWORD(wparam)) {
        case kMenuSafety:
          control->push(ControlRequest::kSafety);
          break;
        case kMenuOverlay:
          control->push(ControlRequest::kOverlayToggle);
          break;
        case kMenuPause:
          control->push(ControlRequest::kPlayPauseToggle);
          break;
        case kMenuShowUi:
          control->push(ControlRequest::kShowUi);
          break;
        case kMenuQuit:
          control->push(ControlRequest::kQuit);
          break;
        }
        return 0;
      }
      if (g_taskbar_created_message && msg == g_taskbar_created_message) {
        auto icon_data = make_icon_data(hwnd);
        Shell_NotifyIconW(NIM_ADD, &icon_data);
        return 0;
      }
    }
    if (msg == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
}

void SystemControl::thread_main()
{
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = tray_wnd_proc;
  window_class.hInstance = GetModuleHandle(nullptr);
  window_class.lpszClassName = L"trance_system_control";
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::cerr << "system control: couldn't register tray window class" << std::endl;
    return;
  }
  HWND hwnd = CreateWindowW(window_class.lpszClassName, L"trance", WS_OVERLAPPED, 0, 0, 0, 0,
                            nullptr, nullptr, window_class.hInstance, nullptr);
  if (!hwnd) {
    std::cerr << "system control: couldn't create tray window" << std::endl;
    return;
  }
  SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  _hwnd = hwnd;

  g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");

  auto icon_data = make_icon_data(hwnd);
  if (Shell_NotifyIconW(NIM_ADD, &icon_data)) {
    std::cout << "system control: tray icon installed" << std::endl;
  } else {
    std::cerr << "system control: couldn't add tray icon" << std::endl;
  }

  // MOD_NOREPEAT: holding the key fires once, not an autorepeat stream of safety
  // stops (the second press means quit -- autorepeat would trigger it instantly).
  if (RegisterHotKey(hwnd, kHotkeyId, MOD_SHIFT | MOD_NOREPEAT, VK_F11)) {
    std::cout << "system control: global safety hotkey Shift+F11 registered" << std::endl;
  } else {
    std::cerr << "system control: couldn't register Shift+F11 (in use by another "
                 "application?); tray menu is the fallback"
              << std::endl;
  }

  MSG message;
  while (GetMessage(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  UnregisterHotKey(hwnd, kHotkeyId);
  Shell_NotifyIconW(NIM_DELETE, &icon_data);
  _hwnd = nullptr;
}

#elif defined(__linux__)

namespace
{
  // XGrabKey reports an already-grabbed key as an ASYNC BadAccess error, so the only
  // way to detect a conflict is a scoped error handler around the grabs + XSync.
  // X error handlers are process-global; the swap window is kept as small as possible
  // (grab + sync, then restore) to minimize the chance of eating an SFML error.
  std::atomic<bool> g_grab_failed = false;
  int grab_error_handler(Display*, XErrorEvent* error)
  {
    if (error->error_code == BadAccess) {
      g_grab_failed = true;
    }
    return 0;
  }
}

void SystemControl::thread_main()
{
  // A private X connection, used only by this thread -- no locking against SFML's
  // own connection needed. No X11 = no global hotkey (Wayland-only session); the
  // command channel / F2 UI remain the off-switches there.
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::cerr << "system control: no X11 display; global Shift+F11 safety hotkey "
                 "unavailable"
              << std::endl;
    return;
  }
  Window root = DefaultRootWindow(display);
  KeyCode keycode = XKeysymToKeycode(display, XK_F11);
  if (!keycode) {
    std::cerr << "system control: no keycode for F11; safety hotkey unavailable" << std::endl;
    XCloseDisplay(display);
    return;
  }

  // Grab Shift+F11 under every NumLock/CapsLock combination -- a bare ShiftMask
  // grab silently stops matching the moment NumLock is on.
  const unsigned int extra_masks[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};
  g_grab_failed = false;
  auto previous_handler = XSetErrorHandler(grab_error_handler);
  for (auto extra : extra_masks) {
    XGrabKey(display, keycode, ShiftMask | extra, root, False, GrabModeAsync, GrabModeAsync);
  }
  XSync(display, False);
  XSetErrorHandler(previous_handler);
  if (g_grab_failed) {
    std::cerr << "system control: Shift+F11 already grabbed by another application; "
                 "safety hotkey may not fire"
              << std::endl;
  } else {
    std::cout << "system control: global safety hotkey Shift+F11 registered" << std::endl;
  }

  // Autorepeat suppression: holding the key must fire ONE safety request, not a
  // repeat stream (the second request means quit -- see ControlRequest::kSafety).
  // With detectable autorepeat, repeats arrive as KeyPress with no intervening
  // KeyRelease, so gating on "released since the last press" filters them; the
  // Windows path gets the same behaviour from MOD_NOREPEAT.
  XkbSetDetectableAutoRepeat(display, True, nullptr);
  bool key_down = false;

  int fd = ConnectionNumber(display);
  while (!_stop) {
    // 200ms select timeout so teardown (_stop) is honoured promptly even when no
    // key events arrive.
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    timeval timeout = {0, 200 * 1000};
    select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
    while (XPending(display)) {
      XEvent event;
      XNextEvent(display, &event);
      if (event.type == KeyPress && event.xkey.keycode == keycode &&
          (event.xkey.state & ShiftMask)) {
        if (!key_down) {
          push(ControlRequest::kSafety);
        }
        key_down = true;
      } else if (event.type == KeyRelease && event.xkey.keycode == keycode) {
        key_down = false;
      }
    }
  }

  XUngrabKey(display, keycode, AnyModifier, root);
  XCloseDisplay(display);
}

#else

void SystemControl::thread_main() {}

#endif
