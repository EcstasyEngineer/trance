#include <trance/render/render.h>
#include <algorithm>
#include <iostream>

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
  _window->setFramerateLimit(0);
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
