#include <trance/render/render.h>
#include <trance/platform/display_info.h>
#include <trance/render/openxr.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <common/trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#pragma warning(pop)

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

bool save_window_screenshot(sf::RenderWindow& window, const std::string& path)
{
  const auto size = window.getSize();
  std::vector<std::uint8_t> pixels(std::size_t{size.x} * size.y * 4);
  glReadPixels(0, 0, GLsizei(size.x), GLsizei(size.y), GL_RGBA, GL_UNSIGNED_BYTE,
               pixels.data());
  // GL reads bottom-up; sf::Image wants top-down.
  std::vector<std::uint8_t> flipped(pixels.size());
  const std::size_t stride = std::size_t{size.x} * 4;
  for (std::size_t y = 0; y < size.y; ++y) {
    std::copy_n(pixels.data() + (size.y - 1 - y) * stride, stride, flipped.data() + y * stride);
  }
  sf::Image image{sf::Vector2u{size.x, size.y}, flipped.data()};
  if (!image.saveToFile(path)) {
    std::cerr << "screenshot: couldn't write " << path << std::endl;
    return false;
  }
  return true;
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
  // The context version is REQUESTED explicitly rather than left to SFML's default,
  // which is 1.1: WglContext only appends WGL_CONTEXT_MAJOR/MINOR_VERSION_ARB when the
  // request exceeds 1.1, so with the default we never ask for a version at all and
  // simply accept whatever compatibility context the driver hands out. That is a
  // correctness problem for the XR output this window's context also feeds (trap 4):
  // XR_KHR_opengl_enable runtimes publish a [minApiVersionSupported,
  // maxApiVersionSupported] range (XrOutput checks it) and a context outside it is
  // grounds to reject the session. 4.5 sits inside every desktop runtime's range and is
  // universally available on hardware that can drive a headset, and the request is safe
  // on anything older -- SFML retries with successively lower versions, then plain
  // wglCreateContext, if the driver refuses. Attribute flags stay Default
  // (compatibility) deliberately: SFML's graphics module, which this pipeline draws
  // through, does not work on a Core profile context.
  sf::ContextSettings context_settings;
  context_settings.majorVersion = 4;
  context_settings.minorVersion = 5;
  _window->create(video_mode, "trance", style, sf::State::Windowed, context_settings);
  // Overlay mode: input passes through to whatever's beneath (platform/overlay_hints),
  // so there's nothing for this window to usefully grab the cursor for.
  _window->setMouseCursorGrabbed(!overlay.enabled);
  _window->setVerticalSyncEnabled(system.enable_vsync());
  // Presentation cap -- the ONLY thing in the engine that bounds how many frames get
  // drawn and swapped. global_fps does not: the main loop drains however many content
  // ticks elapsed wall-clock bought and then presents at most once per iteration, and on
  // a desktop run the F2 panel exists unconditionally, so its repaint term holds
  // do_render true every single iteration (main.cpp). The loop therefore presents as
  // fast as the swap lets it, whatever global_fps says.
  //
  // With vsync on the swap itself blocks at the panel and the limiter must stay OFF:
  // SFML implements setFramerateLimit with a sleep, and sleeping on top of a blocking
  // swap fights the driver's pacing instead of adding to it. With vsync off there was
  // previously no cap at all -- the loop free-spun the GPU rendering frames the panel
  // can never show -- so fall back to the display's own rate there. Unknown rate (off
  // Windows, display_info.h) keeps the old uncapped behaviour rather than inventing one.
  const uint32_t refresh_hz = display_refresh_hz();
  _window->setFramerateLimit(system.enable_vsync() ? 0 : refresh_hz);
  _window->setVisible(false);
  if (!_window->setActive(true)) {
    std::cerr << "couldn't activate window OpenGL context" << std::endl;
  }

  if (overlay.enabled) {
    // The window is still unmapped here (setVisible(false) above); the startup variant
    // handles the unmapped-window EWMH difference on X11.
    apply_overlay_hints_at_startup(_window->getNativeHandle(), overlay.opacity);
  }

  init_glew();
}

ScreenRenderer::~ScreenRenderer()
{
  // Explicit, and BEFORE ~Renderer takes the window (and its GL context) away: the XR
  // teardown is GL-bound, so it has to happen while this object's own window is still
  // alive and current (trap 5). Member destruction alone would get the order right --
  // _xr is a derived member, _window a base one -- but not the "context current" half.
  detach_xr();
}

bool ScreenRenderer::attach_xr()
{
  if (_xr) {
    return true;
  }
  // XrOutput binds to whatever GL context is current on this thread, so make sure it is
  // ours before it looks (wglGetCurrentDC/Context). Nothing else in the process owns a
  // context any more -- the hidden helper window is gone.
  if (!_window->setActive(true)) {
    std::cerr << "couldn't activate window OpenGL context for OpenXR" << std::endl;
    return false;
  }
  std::unique_ptr<XrOutput> xr{new XrOutput};
  if (!xr->success()) {
    // Every failure leaf has already printed its specific diagnosis (no runtime
    // registered / registered but unreachable / no HMD). Destroying it here, with the
    // context still current, is also what releases the loader's instance.
    return false;
  }
  std::cerr << "OpenXR attached: " << xr->width() << "x" << xr->height() << " per eye"
            << std::endl;
  _xr = std::move(xr);
  return true;
}

void ScreenRenderer::detach_xr()
{
  if (!_xr) {
    return;
  }
  if (!_window->setActive(true)) {
    std::cerr << "couldn't activate window OpenGL context for OpenXR teardown" << std::endl;
  }
  _xr.reset();
}

bool ScreenRenderer::vr_enabled() const
{
  return _xr != nullptr;
}

uint32_t ScreenRenderer::view_width() const
{
  return width();
}

uint32_t ScreenRenderer::width() const
{
  return _pass == State::NONE ? _window->getSize().x : _xr->width();
}

uint32_t ScreenRenderer::height() const
{
  return _pass == State::NONE ? _window->getSize().y : _xr->height();
}

float ScreenRenderer::eye_spacing_multiplier() const
{
  // Only ever consumed on an eye pass (Director::eye_offset zeroes the shear on NONE),
  // but answer for the pass anyway rather than for the attachment.
  return _pass == State::NONE ? 1.f : _xr->eye_spacing_multiplier();
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
  if (_xr && _xr->update() == XrOutput::Update::DetachRequested) {
    // The seam phase 4 completes: the XR side comes down here, with the context current,
    // and the desktop is meant to keep playing while a background probe re-attaches.
    // Until that lands, a detach request still ends the run -- exactly what the VR-mode
    // renderer's `update() == false` did, minus taking the window with it.
    detach_xr();
    return false;
  }
  return true;
}

void ScreenRenderer::render(const std::function<void(State)>& render_fn, bool blank)
{
  // 1. The eye passes, when a headset is attached and its session is actually running.
  //    `blank` (paused/hidden) submits a layerless frame instead of drawing: the
  //    handshake stays alive so the runtime never flags us unresponsive, and the content
  //    genuinely vanishes from the headset rather than freezing head-locked in front of
  //    the user's eyes (D8, trap 9). Note this keys off paused/hidden, NOT off whether a
  //    render happened -- the F2 panel keeps the desktop repainting while paused, and
  //    that must not put content back in the headset.
  if (_xr && _xr->session_running()) {
    if (blank) {
      _xr->render_idle(true);
    } else {
      _xr->render([&](State state) {
        _pass = state;
        render_fn(state);
      });
    }
  }
  // 2. The desktop pass, ALWAYS -- no XR early-out above may skip it (trap 10), which is
  //    why it sits outside every branch rather than in an else. It re-establishes its own
  //    GL state instead of inheriting it: the eye passes leave an eye-sized viewport and
  //    a swapchain FBO bound, and FRAMEBUFFER_SRGB stays disabled here for the same
  //    reason it is disabled there -- the pipeline writes gamma-encoded bytes and the
  //    default framebuffer is not sRGB-typed, so this is a straight passthrough (trap 3).
  //    The desktop is a third render pass, never a blit from an eye texture (D4).
  _pass = State::NONE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_FRAMEBUFFER_SRGB);
  glViewport(0, 0, GLsizei(_window->getSize().x), GLsizei(_window->getSize().y));
  glClear(GL_COLOR_BUFFER_BIT);
  render_fn(State::NONE);
  if (_ui_hook) {
    _ui_hook();
  }
  _window->display();
}

bool ScreenRenderer::render_idle(bool blank)
{
  // Nothing was drawn this iteration (hidden, or between visual frames with no UI to
  // repaint). The window has nothing to present, but a running XR session still owes the
  // runtime its frame.
  if (_xr && _xr->session_running()) {
    return _xr->render_idle(blank);
  }
  return false;
}
