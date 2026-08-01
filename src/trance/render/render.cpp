#include <trance/render/render.h>
#include <trance/platform/display_info.h>
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
  _window->create(video_mode, "trance", style);
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
