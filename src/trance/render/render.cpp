#include <trance/render/render.h>
#include <algorithm>
#include <iostream>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <common/trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#pragma warning(pop)

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
  // Overlay wiring (#27) on top of the X11 groundwork from #20, now runtime-toggleable
  // (apply_overlay_hints/clear_overlay_hints below). All of this is a no-op (returns
  // without touching anything) if we're not actually on an X11 session (e.g. Wayland-
  // only, or DISPLAY unset) -- overlay mode then degrades gracefully to an always-on-top
  // borderless window without click-through or translucency, rather than crashing.
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

  // Startup path (ScreenRenderer constructor, --overlay): the window is NOT yet mapped
  // (setVisible(false) until init()), so the states are written directly as a property
  // and the WM picks them up at map time. Opacity + input shape are shared with the
  // runtime path and work either way.
  void apply_x11_overlay_hints_at_startup(sf::WindowHandle handle, float opacity)
  {
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
  }
#endif
}

// Runtime overlay toggle (#27): both callable on a live, mapped window from the main
// loop (main.cpp's apply seam, fed by the #21 `overlay ...` verbs and the F2 UI's
// Overlay section). apply_overlay_hints() is idempotent -- calling it again while
// already on just rewrites the opacity, which is exactly the live-opacity-change path.
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
  // UNVALIDATED: no Windows box in this environment to test against. Mirrors the X11
  // path's intent (always-on-top, uniform translucency, click-through) via the
  // documented WS_EX_LAYERED | WS_EX_TRANSPARENT combination. SWP_FRAMECHANGED because
  // at runtime the ex-style just changed under a live window. Test on an actual
  // Windows machine before relying on this for #27 there.
  HWND hwnd = handle;
  LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  ex_style |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
  SetLayeredWindowAttributes(
      hwnd, 0, static_cast<BYTE>(std::clamp(opacity, 0.f, 1.f) * 255.f), LWA_ALPHA);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
  ex_style &= ~static_cast<LONG_PTR>(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
  SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
  SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
  (void)handle;
#endif
}

#if defined(__linux__)
// X11/Xlib.h #defines plain identifiers (None, Bool, Status, Success, ...) that
// collide with sf::Style::None and friends used below. The overlay-hint code above is
// the only place that needs the X11 macros, so undef them here rather than avoiding
// the names project-wide.
#undef None
#undef Bool
#undef Status
#undef Success
#endif

namespace
{
  void compile_shader(GLuint shader)
  {
    glCompileShader(shader);
    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
      GLint log_size = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);

      char* error_log = new char[log_size];
      glGetShaderInfoLog(shader, log_size, &log_size, error_log);
      std::cerr << error_log;
      delete[] error_log;
    }
  }

  void link_program(GLuint program)
  {
    glLinkProgram(program);
    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success) {
      GLint log_size = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_size);

      char* error_log = new char[log_size];
      glGetProgramInfoLog(program, log_size, &log_size, error_log);
      std::cerr << error_log;
      delete[] error_log;
    }
  }
}

GLuint compile(const std::string& vertex_text, const std::string& fragment_text)
{
  GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
  GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);

  const char* v = vertex_text.data();
  const char* f = fragment_text.data();

  glShaderSource(vertex, 1, &v, nullptr);
  glShaderSource(fragment, 1, &f, nullptr);

  compile_shader(vertex);
  compile_shader(fragment);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  link_program(program);
  return program;
}

void init_glew()
{
  GLenum ok = glewInit();
  if (ok != GLEW_OK) {
    std::cerr << "couldn't initialise GLEW: " << glewGetErrorString(ok) << std::endl;
  }

  if (!GLEW_VERSION_2_1) {
    std::cerr << "OpenGL 2.1 not available" << std::endl;
  }

  if (!GLEW_ARB_texture_non_power_of_two) {
    std::cerr << "OpenGL non-power-of-two textures not available" << std::endl;
  }

  if (!GLEW_ARB_shading_language_100 || !GLEW_ARB_shader_objects || !GLEW_ARB_vertex_shader ||
      !GLEW_ARB_fragment_shader) {
    std::cerr << "OpenGL shaders not available" << std::endl;
  }

  if (!GLEW_EXT_framebuffer_object) {
    std::cerr << "OpenGL framebuffer objects not available" << std::endl;
  }
}

sf::RenderWindow& Renderer::window()
{
  return *_window;
}

ScreenRenderer::ScreenRenderer(const trance_pb::System& system, const OverlayConfig& overlay)
{
  _window.reset(new sf::RenderWindow);
  glClearColor(0.f, 0.f, 0.f, 0.f);
  glClear(GL_COLOR_BUFFER_BIT);

  auto video_mode = sf::VideoMode::getDesktopMode();
  // Overlay mode forces borderless fullscreen (sf::Style::None) regardless of
  // system.windowed() -- an overlay that isn't fullscreen-borderless can't sit over
  // the whole desktop.
  auto style = (overlay.enabled || !system.windowed()) ? sf::Style::None : sf::Style::Default;
  _window->create(video_mode, "trance", style);
  // Overlay mode: input passes through to whatever's beneath (see apply_x11_overlay_hints),
  // so there's nothing for this window to usefully grab the cursor for.
  _window->setMouseCursorGrabbed(!overlay.enabled);
  _window->setVerticalSyncEnabled(system.enable_vsync());
  _window->setFramerateLimit(0);
  _window->setVisible(false);
  if (!_window->setActive(true)) {
    std::cerr << "couldn't activate window OpenGL context" << std::endl;
  }

  if (overlay.enabled) {
#if defined(__linux__)
    // Startup variant: the window is still unmapped here (setVisible(false) above), so
    // _NET_WM_STATE goes on as a direct property write; the WM reads it when init()
    // maps the window. The runtime toggle path uses ClientMessages instead.
    apply_x11_overlay_hints_at_startup(_window->getNativeHandle(), overlay.opacity);
#elif defined(_WIN32)
    // Win32 ex-styles work the same mapped or unmapped -- one path for both.
    apply_overlay_hints(_window->getNativeHandle(), overlay.opacity);
#endif
  }

  init_glew();
}

bool ScreenRenderer::vr_enabled() const
{
  return false;
}

bool ScreenRenderer::is_openvr() const
{
  return false;
}

uint32_t ScreenRenderer::view_width() const
{
  return width();
}

uint32_t ScreenRenderer::width() const
{
  return _window->getSize().x;
}

uint32_t ScreenRenderer::height() const
{
  return _window->getSize().y;
}

float ScreenRenderer::eye_spacing_multiplier() const
{
  return 1.f;
}

void ScreenRenderer::init()
{
  _window->setVisible(true);
  if (!_window->setActive()) {
    std::cerr << "couldn't reactivate window OpenGL context" << std::endl;
  }
  _window->display();
}

bool ScreenRenderer::update()
{
  return true;
}

void ScreenRenderer::render(const std::function<void(State)>& render_fn)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  render_fn(State::NONE);
  if (_ui_hook) {
    _ui_hook();
  }
  _window->display();
}
