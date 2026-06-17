#include <trance/director.h>
#include <common/session.h>
#include <common/util.h>
#include <trance/media/font.h>
#include <trance/theme_bank.h>
#include <trance/visual/api.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/visual.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>

#pragma warning(push, 0)
extern "C" {
#include <GL/glew.h>
}
#include <common/trance.pb.h>
#include <SFML/OpenGL.hpp>
#pragma warning(pop)

namespace
{
  const uint32_t spiral_type_max = 7;
}
#include "shaders.h"

Director::Director(const trance_pb::Session& session, const trance_pb::System& system,
                   ThemeBank& themes, const trance_pb::Program& program, Renderer& renderer)
: _session{session}
, _system{system}
, _themes{themes}
, _program{&program}
, _new_program{0}
, _spiral_program{0}
, _quad_buffer{0}
, _renderer{renderer}
, _last_visual_selection{0}
, _debug_overlay{false}
, _debug_font_loaded{false}
, _debug_font_ok{false}
{
  std::cout << "\npreloading GPU" << std::endl;
  static const std::size_t gl_preload = 1000;
  for (std::size_t i = 0; i < gl_preload; ++i) {
    themes.get_image(false);
    themes.get_image(true);
  }

  _new_program = compile(new_vertex, new_fragment);
  _spiral_program = compile(spiral_vertex, spiral_fragment);

  static const float quad_data[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f,
                                    1.f,  -1.f, 1.f, 1.f,  -1.f, 1.f};
  glGenBuffers(1, &_quad_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, _quad_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, quad_data, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  _visual_api.reset(new VisualApiImpl{*this, _themes, session, system, _renderer.height()});
  change_visual(0);
  _renderer.init();
}

Director::~Director()
{
  glDeleteBuffers(1, &_quad_buffer);
}

void Director::set_program(const trance_pb::Program& program)
{
  _program = &program;
}

bool Director::update()
{
  _visual_api->update();
  _visual->cycler()->advance();
  if (_visual->cycler()->complete()) {
    change_visual(_visual->cycler()->length());
  }
  return _renderer.update();
}

void Director::render() const
{
  Image::delete_textures();
  _visual_api->debug_begin_frame();
  _renderer.render([&](Renderer::State state) {
    _render_state = state;
    _visual->render(*_visual_api);
    // Only draw the HUD on the flat screen pass (not per-eye VR targets).
    if (_debug_overlay && state == Renderer::State::NONE) {
      draw_debug_overlay();
    }
  });
}

void Director::toggle_debug_overlay()
{
  _debug_overlay = !_debug_overlay;
}

const trance_pb::Program& Director::program() const
{
  return *_program;
}

bool Director::vr_enabled() const
{
  return _renderer.vr_enabled();
}

void Director::render_spiral(float spiral, uint32_t spiral_width, uint32_t spiral_type) const
{
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);

  auto aspect_ratio = float(_renderer.view_width()) / float(_renderer.height());

  glUseProgram(_spiral_program);
  glUniform1f(glGetUniformLocation(_spiral_program, "near_plane"), 1.f);
  glUniform1f(glGetUniformLocation(_spiral_program, "far_plane"), 1.f + far_plane_distance());
  glUniform1f(glGetUniformLocation(_spiral_program, "eye_offset"), eye_offset());
  glUniform1f(glGetUniformLocation(_spiral_program, "aspect_ratio"), aspect_ratio);
  glUniform1f(glGetUniformLocation(_spiral_program, "width"), float(spiral_width));
  glUniform1f(glGetUniformLocation(_spiral_program, "spiral_type"), float(spiral_type));
  glUniform1f(glGetUniformLocation(_spiral_program, "time"), spiral);
  glUniform4f(glGetUniformLocation(_spiral_program, "acolour"), _program->spiral_colour_a().r(),
              _program->spiral_colour_a().g(), _program->spiral_colour_a().b(),
              _program->spiral_colour_a().a());
  glUniform4f(glGetUniformLocation(_spiral_program, "bcolour"), _program->spiral_colour_b().r(),
              _program->spiral_colour_b().g(), _program->spiral_colour_b().b(),
              _program->spiral_colour_b().a());

  auto position_location = glGetAttribLocation(_spiral_program, "device_position");
  glEnableVertexAttribArray(position_location);
  glBindBuffer(GL_ARRAY_BUFFER, _quad_buffer);
  glVertexAttribPointer(position_location, 2, GL_FLOAT, false, 0, 0);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(position_location);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Director::render_image(const Image& image, float alpha, float zoom_origin, float zoom) const
{
  GLuint position_buffer;
  glGenBuffers(1, &position_buffer);
  std::vector<float> position_data;

  GLuint texture_buffer;
  glGenBuffers(1, &texture_buffer);
  std::vector<float> texture_data;

  auto x_scale = float(image.width()) / _renderer.width();
  auto y_scale = float(image.height()) / _renderer.height();
  auto x_size = std::min(1.f, x_scale / y_scale);
  auto y_size = std::min(1.f, y_scale / x_scale);
  if (vr_enabled()) {
    x_size /= 2.5;
    y_size /= 2.5;
  }

  GLsizei vertex_count = 0;
  auto add_quad = [&](int x, int y, bool x_flip, bool y_flip) {
    auto x_offset = 2 * x * x_size;
    auto y_offset = 2 * y * y_size;
    vertex_count += 6;

    position_data.insert(position_data.end(),
                         {x_offset - x_size, y_offset - y_size, zoom, zoom_origin,
                          x_offset + x_size, y_offset - y_size, zoom, zoom_origin,
                          x_offset - x_size, y_offset + y_size, zoom, zoom_origin,
                          x_offset + x_size, y_offset - y_size, zoom, zoom_origin,
                          x_offset + x_size, y_offset + y_size, zoom, zoom_origin,
                          x_offset - x_size, y_offset + y_size, zoom, zoom_origin});

    texture_data.insert(
        texture_data.end(),
        {x_flip ? 1.f : 0.f, y_flip ? 0.f : 1.f, x_flip ? 0.f : 1.f, y_flip ? 0.f : 1.f,
         x_flip ? 1.f : 0.f, y_flip ? 1.f : 0.f, x_flip ? 0.f : 1.f, y_flip ? 0.f : 1.f,
         x_flip ? 0.f : 1.f, y_flip ? 1.f : 0.f, x_flip ? 1.f : 0.f, y_flip ? 1.f : 0.f});
  };

  for (int x = 0; x_size * (2 * x - 1) < 1 + std::abs(_system.eye_spacing().eye_spacing()); ++x) {
    for (int y = 0; y_size * (2 * y - 1) < 1; ++y) {
      add_quad(x, y, x % 2, y % 2);
      if (x) {
        add_quad(-x, y, x % 2, y % 2);
      }
      if (y) {
        add_quad(x, -y, x % 2, y % 2);
      }
      if (x && y) {
        add_quad(-x, -y, x % 2, y % 2);
      }
    }
  }
  // Avoids the very edge of images.
  static const auto epsilon = 1.f / 256;
  for (auto& uv : texture_data) {
    uv = uv * (1 - epsilon) + epsilon / 2;
  }

  glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * position_data.size(), position_data.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindBuffer(GL_ARRAY_BUFFER, texture_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * texture_data.size(), texture_data.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glEnable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glUseProgram(_new_program);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, image.texture());
  glUniform1i(glGetUniformLocation(_new_program, "texture"), 0);
  glUniform1f(glGetUniformLocation(_new_program, "near_plane"), 1.f);
  glUniform1f(glGetUniformLocation(_new_program, "far_plane"), 1.f + far_plane_distance());
  glUniform1f(glGetUniformLocation(_new_program, "eye_offset"), eye_offset());
  glUniform4f(glGetUniformLocation(_new_program, "colour"), 1.f, 1.f, 1.f, alpha);

  GLuint position_location = glGetAttribLocation(_new_program, "virtual_position");
  glEnableVertexAttribArray(position_location);
  glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
  glVertexAttribPointer(position_location, 4, GL_FLOAT, false, 0, 0);

  GLuint texture_location = glGetAttribLocation(_new_program, "texture_coord");
  glEnableVertexAttribArray(texture_location);
  glBindBuffer(GL_ARRAY_BUFFER, texture_buffer);
  glVertexAttribPointer(texture_location, 2, GL_FLOAT, false, 0, 0);

  glDrawArrays(GL_TRIANGLES, 0, vertex_count);

  glDisableVertexAttribArray(position_location);
  glDisableVertexAttribArray(texture_location);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glDeleteBuffers(1, &position_buffer);
  glDeleteBuffers(1, &texture_buffer);
}

sf::Vector2f Director::text_size(const Font& font, const std::string& text, bool large) const
{
  auto size = font.get_size(text, large);
  return {size.x / _renderer.view_width(), size.y / _renderer.height()};
}

void Director::render_text(const Font& font, const std::string& text, bool large,
                           const sf::Color& colour, float scale, const sf::Vector2f& offset,
                           float zoom_origin, float zoom) const
{
  auto vertices = font.get_vertices(text, large);

  GLuint position_buffer;
  glGenBuffers(1, &position_buffer);
  std::vector<float> position_data;

  GLuint texture_buffer;
  glGenBuffers(1, &texture_buffer);
  std::vector<float> texture_data;

  for (const auto& vertex : vertices) {
    position_data.insert(position_data.end(),
                         {offset.x + 2 * scale * vertex.x / _renderer.view_width(),
                          offset.y - 2 * scale * vertex.y / _renderer.height(), zoom, zoom_origin});
    texture_data.insert(texture_data.end(), {vertex.u, vertex.v});
  }

  glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * position_data.size(), position_data.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindBuffer(GL_ARRAY_BUFFER, texture_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * texture_data.size(), texture_data.data(),
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glEnable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glUseProgram(_new_program);

  glActiveTexture(GL_TEXTURE0);
  font.bind_texture(large);
  glUniform1i(glGetUniformLocation(_new_program, "texture"), 0);
  glUniform1f(glGetUniformLocation(_new_program, "near_plane"), 1.f);
  glUniform1f(glGetUniformLocation(_new_program, "far_plane"), 1.f + far_plane_distance());
  glUniform1f(glGetUniformLocation(_new_program, "eye_offset"), eye_offset());
  glUniform4f(glGetUniformLocation(_new_program, "colour"), colour.r / 255.f, colour.g / 255.f,
              colour.b / 255.f, colour.a / 255.f);

  GLuint position_location = glGetAttribLocation(_new_program, "virtual_position");
  glEnableVertexAttribArray(position_location);
  glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
  glVertexAttribPointer(position_location, 4, GL_FLOAT, false, 0, 0);

  GLuint texture_location = glGetAttribLocation(_new_program, "texture_coord");
  glEnableVertexAttribArray(texture_location);
  glBindBuffer(GL_ARRAY_BUFFER, texture_buffer);
  glVertexAttribPointer(texture_location, 2, GL_FLOAT, false, 0, 0);

  glDrawArrays(GL_QUADS, 0, GLsizei(vertices.size()));

  // Font texture must be unbound.
  glActiveTexture(GL_TEXTURE0);
  sf::Texture::bind(nullptr);

  glDisableVertexAttribArray(position_location);
  glDisableVertexAttribArray(texture_location);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glDeleteBuffers(1, &position_buffer);
  glDeleteBuffers(1, &texture_buffer);
}

void Director::change_visual(uint32_t length)
{
  // Always change if current visual isn't in the program.
  bool included = false;
  for (const auto& type : _program->visual_type()) {
    if (_visual && type.random_weight() && type.type() == _last_visual_selection) {
      included = true;
    }
  }
  // Like !random_chance(chance), but scaled to current speed and cycle length.
  // Roughly 1/2 chance for a cycle of length 2048.
  auto fps = program().global_fps();
  if (included && length && random((2 * fps * length) / 2048) >= 120) {
    return;
  }

  uint32_t total = 0;
  for (const auto& type : _program->visual_type()) {
    total += type.random_weight();
  }
  auto r = random(total);
  total = 0;
  trance_pb::Program_VisualType t;
  for (const auto& type : _program->visual_type()) {
    total += type.random_weight();
    if (r < total) {
      t = type.type();
      break;
    }
  }

  if (_visual && t == _last_visual_selection) {
    _visual->reset();
    return;
  }
  if (t == trance_pb::Program_VisualType_ACCELERATE) {
    _visual.reset(new AccelerateVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_SLOW_FLASH) {
    _visual.reset(new SlowFlashVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_SUB_TEXT) {
    _visual.reset(new SubTextVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_FLASH_TEXT) {
    _visual.reset(new FlashTextVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_PARALLEL) {
    _visual.reset(new SimpleVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_SUPER_PARALLEL) {
    _visual.reset(new ParallelVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_ANIMATION) {
    _visual.reset(new AnimationVisual{*_visual_api});
  }
  if (t == trance_pb::Program_VisualType_SUPER_FAST) {
    _visual.reset(new SuperFastVisual{*_visual_api});
  }
  _last_visual_selection = t;
}

float Director::far_plane_distance() const
{
  return _system.draw_depth().draw_depth() * 256.f;
}

float Director::eye_offset() const
{
  auto offset = _renderer.eye_spacing_multiplier() * _system.eye_spacing().eye_spacing();
  return _render_state == Renderer::State::VR_LEFT
      ? -offset
      : _render_state == Renderer::State::VR_RIGHT ? offset : 0;
}

namespace
{
  // The proto VisualType enum value (see trance.proto) mapped to a human label.
  // Note the enum name and the concrete Visual class don't always line up:
  // PARALLEL is a single image, SUPER_PARALLEL is the 3-image overlay.
  std::string visual_type_name(uint32_t t)
  {
    switch (t) {
    case 1:
      return "ACCELERATE [accelerating image + spiral]";
    case 2:
      return "SLOW_FLASH [slow then fast flash phases]";
    case 3:
      return "SUB_TEXT [image + scrolling subtext]";
    case 4:
      return "FLASH_TEXT [2-image crossfade + text]";
    case 5:
      return "PARALLEL [single image]";
    case 6:
      return "SUPER_PARALLEL [3-image overlay / triple fade]";
    case 7:
      return "ANIMATION [animation + crossfade image]";
    case 8:
      return "SUPER_FAST [rapid current/next cuts]";
    default:
      return "(none)";
    }
  }

  std::string bar(float frac, int width)
  {
    frac = frac < 0.f ? 0.f : frac > 1.f ? 1.f : frac;
    int fill = int(frac * width + 0.5f);
    std::string s = "[";
    for (int i = 0; i < width; ++i) {
      s += (i < fill ? '#' : '-');
    }
    s += "]";
    return s;
  }

  // Recursively render the cycler tree. Depth and breadth are capped so a busy
  // visual (e.g. ACCELERATE, which has dozens of subcycles) stays legible.
  void append_cycler(std::ostringstream& out, const Cycler* c, int depth, int max_depth)
  {
    if (!c) {
      return;
    }
    for (int i = 0; i < depth; ++i) {
      out << "  ";
    }
    out << (depth ? "+-" : "") << c->type_name() << " " << c->position() << "/" << c->length()
        << " " << bar(c->progress(), 8);
    if (c->active()) {
      out << " *";
    }
    out << "\n";

    auto kids = c->children();
    if (depth >= max_depth) {
      if (!kids.empty()) {
        for (int i = 0; i <= depth; ++i) {
          out << "  ";
        }
        out << "+-(" << kids.size() << " subcycle(s)...)\n";
      }
      return;
    }
    static const std::size_t max_children = 20;
    std::size_t shown = std::min(kids.size(), max_children);
    for (std::size_t i = 0; i < shown; ++i) {
      append_cycler(out, kids[i], depth + 1, max_depth);
    }
    if (kids.size() > shown) {
      for (int i = 0; i <= depth; ++i) {
        out << "  ";
      }
      out << "+-(" << (kids.size() - shown) << " more...)\n";
    }
  }
}

void Director::draw_debug_overlay() const
{
  // Lazily load a monospace system font so the theme glyph rows line up. These
  // paths are Windows-specific, which matches the rest of the build.
  if (!_debug_font_loaded) {
    _debug_font_loaded = true;
    _debug_font_ok = _debug_font.loadFromFile("C:/Windows/Fonts/consola.ttf") ||
        _debug_font.loadFromFile("C:/Windows/Fonts/cour.ttf") ||
        _debug_font.loadFromFile("C:/Windows/Fonts/arial.ttf");
  }
  if (!_debug_font_ok) {
    return;
  }

  std::ostringstream out;
  char buf[32];

  out << "== TRANCE DEBUG (F1 to hide) ==\n";
  out << "visual : " << visual_type_name(_last_visual_selection) << "\n";

  // Image layers composited this frame -- this is the on-screen overlay depth.
  const auto& layers = _visual_api->debug_layers();
  out << "layers : " << layers.size() << " image(s) drawn  alpha=";
  for (float a : layers) {
    std::snprintf(buf, sizeof(buf), "%.2f ", a);
    out << buf;
  }
  out << "\n";

  std::snprintf(buf, sizeof(buf), "%.3f", _visual_api->debug_spiral());
  out << "spiral : type " << _visual_api->debug_spiral_type() << "  width "
      << _visual_api->debug_spiral_width() << "  phase " << buf << "\n";
  out << "font   : " << (_visual_api->debug_font().empty() ? "(none)" : _visual_api->debug_font())
      << "   sub: "
      << (_visual_api->debug_subfont().empty() ? "(none)" : _visual_api->debug_subfont()) << "\n";

  // Theme bank: the two active slots [1]/[2] are what visuals interleave.
  auto snap = _themes.debug_snapshot();
  out << "-- THEMES (slot[1] x slot[2] = merged on screen) --\n";
  static const char* labels[4] = {" unld[0]", "*pri [1]", " alt [2]", " load[3]"};
  for (int i = 0; i < 4; ++i) {
    const auto& s = snap.slots[i];
    out << labels[i] << " ";
    if (!s.valid) {
      out << "(empty)\n";
      continue;
    }
    std::string name = s.name.substr(0, 12);
    out << name;
    for (std::size_t k = name.size(); k < 13u; ++k) {
      out << ' ';
    }
    static const int row = 24;
    int fill = s.total ? int(float(s.loaded) / float(s.total) * row + 0.5f) : 0;
    out << ' ';
    for (int k = 0; k < row; ++k) {
      out << (k < fill ? '#' : '.');
    }
    out << "  " << s.loaded << "/" << s.total << "\n";
  }
  out << "enabled:";
  for (const auto& w : snap.enabled_weights) {
    out << " " << w.first << "=" << w.second;
  }
  out << (snap.enabled_weights.empty() ? " (none)\n" : "\n");
  out << "pinned : " << (snap.pinned.empty() ? "(none)" : snap.pinned) << "\n";
  out << "cache  : " << snap.image_cache_size << " imgs   swaps_to_match: " << snap.swaps_to_match
      << "   global_fps: " << program().global_fps() << "\n";
  out << "legend : #=loaded(RAM)  .=not loaded\n";

  // Cycler tree last: its depth varies frame-to-frame, so keeping it at the
  // bottom stops it from shoving the fixed rows above it around.
  out << "-- CYCLER (pattern of the current visual; pos/len are frames) --\n";
  const Visual* visual = _visual.get();
  append_cycler(out, visual ? visual->cycler() : nullptr, 0, 20);
  out << "types  : Action=beat  OneShot=once(len=longest)  Parallel=together(len=LCM)\n";
  out << "         Sequence=in order(len=sum)  Repeat=loop  Offset=delay   *=active now";

  // Draw the HUD as flat 2D over the rendered frame. pushGLStates/popGLStates
  // isolate SFML's 2D drawing from the visual's raw OpenGL state.
  auto& window = _renderer.window();
  window.pushGLStates();

  sf::Text text;
  text.setFont(_debug_font);
  text.setCharacterSize(14);
  text.setFillColor(sf::Color(sf::Uint8(120), sf::Uint8(255), sf::Uint8(160)));
  text.setString(out.str());
  text.setPosition(14.f, 12.f);

  auto bounds = text.getLocalBounds();
  sf::RectangleShape backing(
      sf::Vector2f(bounds.left + bounds.width + 28.f, bounds.top + bounds.height + 24.f));
  backing.setPosition(6.f, 6.f);
  backing.setFillColor(sf::Color(sf::Uint8(0), sf::Uint8(0), sf::Uint8(0), sf::Uint8(175)));
  window.draw(backing);
  window.draw(text);

  window.popGLStates();
}
