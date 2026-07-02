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
  // v0 overlay wiring (#27) on top of the X11 groundwork from #20. All of this is a
  // no-op (returns without touching anything) if we're not actually on an X11 session
  // (e.g. Wayland-only, or DISPLAY unset) -- overlay mode then degrades gracefully to
  // an always-on-top borderless window without click-through or translucency, rather
  // than crashing.
  void apply_x11_overlay_hints(sf::WindowHandle handle, float opacity)
  {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
      std::cerr << "overlay mode: couldn't open X11 display (Wayland-only session?); "
                << "click-through and translucency hints skipped" << std::endl;
      return;
    }
    Window window = static_cast<Window>(handle);

    // Whole-window translucency. No per-pixel alpha in SFML 2.6 -- see OverlayConfig.
    Atom opacity_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    if (opacity_atom != None) {
      auto value = static_cast<unsigned long>(
          std::clamp(opacity, 0.f, 1.f) * static_cast<float>(0xffffffffu));
      XChangeProperty(display, window, opacity_atom, XA_CARDINAL, 32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(&value), 1);
    }

    // Skip taskbar/pager + always-above, so the overlay doesn't clutter window
    // switchers and stays on top of normal windows.
    Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom skip_taskbar_atom = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skip_pager_atom = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom above_atom = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    if (state_atom != None) {
      Atom states[3];
      int count = 0;
      if (skip_taskbar_atom != None) {
        states[count++] = skip_taskbar_atom;
      }
      if (skip_pager_atom != None) {
        states[count++] = skip_pager_atom;
      }
      if (above_atom != None) {
        states[count++] = above_atom;
      }
      if (count) {
        XChangeProperty(display, window, state_atom, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(states), count);
      }
    }

    // Empty input shape: the window still receives no input at all, so clicks/keys
    // fall through to whatever is beneath it on the desktop.
    int shape_event_base = 0, shape_error_base = 0;
    if (XShapeQueryExtension(display, &shape_event_base, &shape_error_base)) {
      XShapeCombineRectangles(display, window, ShapeInput, 0, 0, nullptr, 0, ShapeSet, 0);
    } else {
      std::cerr << "overlay mode: X11 Shape extension unavailable; window will not be "
                << "click-through" << std::endl;
    }

    XFlush(display);
    XCloseDisplay(display);
  }
#elif defined(_WIN32)
  // UNVALIDATED: no Windows box in this environment to test against. Mirrors the X11
  // path's intent (always-on-top, uniform translucency, click-through) via the
  // documented WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST combination. Wire up
  // and test on an actual Windows machine before relying on this for #27 there.
  void apply_win32_overlay_hints(sf::WindowHandle handle, float opacity)
  {
    HWND hwnd = handle;
    LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex_style |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
    SetLayeredWindowAttributes(
        hwnd, 0, static_cast<BYTE>(std::clamp(opacity, 0.f, 1.f) * 255.f), LWA_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
#endif

#if defined(__linux__)
  // X11/Xlib.h #defines plain identifiers (None, Bool, Status, Success, ...) that
  // collide with sf::Style::None and friends used below. apply_x11_overlay_hints()
  // above is the only place that needs the X11 macros, so undef them here rather than
  // avoiding the names project-wide.
#undef None
#undef Bool
#undef Status
#undef Success
#endif

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
  _window->setActive(true);

  if (overlay.enabled) {
#if defined(__linux__)
    apply_x11_overlay_hints(_window->getSystemHandle(), overlay.opacity);
#elif defined(_WIN32)
    apply_win32_overlay_hints(_window->getSystemHandle(), overlay.opacity);
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
  _window->setActive();
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
  _window->display();
}