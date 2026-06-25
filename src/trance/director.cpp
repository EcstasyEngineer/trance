#include <trance/director.h>
#include <common/session.h>
#include <common/util.h>
#include <trance/media/audio.h>
#include <trance/media/font.h>
#include <trance/theme_bank.h>
#include <trance/visual/api.h>
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/pattern_parser_v2.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/visual.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
, _last_custom_index{-1}
, _debug_overlay{false}
, _debug_font_loaded{false}
, _debug_font_ok{false}
, _audio{nullptr}
{
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
  build_builtin_patterns();
  rebuild_custom_patterns();
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
  rebuild_custom_patterns();
}

void Director::build_builtin_patterns()
{
  _builtin_compiled.clear();
  for (uint32_t t = 1; t <= 8; ++t) {
    // Prefer the v2 intent-grammar source where one exists; it lowers to the same
    // pattern::Node the v1 path produces, so everything downstream is identical.
    std::string v2_source = builtin::pattern_source_v2(t);
    if (!v2_source.empty()) {
      auto v2 = patternv2::parse(v2_source);
      if (!v2.ok) {
        throw std::runtime_error("built-in v2 pattern " + std::to_string(t)
                                 + " failed to parse: " + v2.error);
      }
      for (const auto& w : v2.warnings) {
        std::cerr << "v2 built-in " << t << " warning: " << w << std::endl;
      }
      pattern::Parsed parsed;
      parsed.name = std::move(v2.name);
      parsed.weight = 1;
      parsed.root = std::move(v2.root);
      parsed.render_block = std::move(v2.render_block);  // per-flash zoom etc.
      _builtin_compiled.emplace(t, std::move(parsed));
      continue;
    }

    std::string source = builtin::pattern_source(t);
    if (source.empty()) {
      continue;
    }
    auto result = pattern::parse(source);
    if (!result.ok) {
      // Built-in sources are compile-time constants with no hardcoded fallback left,
      // so a parse failure is a build bug -- fail fast and loud rather than risk a
      // null visual at selection time.
      throw std::runtime_error("built-in pattern " + std::to_string(t) + " failed to parse: "
                               + result.error);
    }
    _builtin_compiled.emplace(t, std::move(result.pattern));
  }
}

void Director::rebuild_custom_patterns()
{
  _custom_patterns.clear();
  _last_custom_index = -1;
  for (const auto& src : _program->custom_visual_pattern()) {
    if (!src.enabled()) {
      continue;
    }
    auto result = pattern::parse(src.source_text());
    if (!result.ok) {
      std::cerr << "skipping custom pattern '" << src.name() << "': " << result.error << std::endl;
      continue;
    }
    // Carry the proto's name/weight (authoritative) over the in-source ones.
    result.pattern.name = src.name();
    result.pattern.weight = src.random_weight();
    _custom_patterns.push_back(std::move(result.pattern));
  }
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

void Director::set_audio(const Audio* audio)
{
  _audio = audio;
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
  // 64-bit totals so a program with many large weights can't overflow the sum.
  uint64_t builtin_total = 0;
  for (const auto& type : _program->visual_type()) {
    builtin_total += type.random_weight();
  }
  uint64_t custom_total = 0;
  for (const auto& p : _custom_patterns) {
    custom_total += p.weight;
  }
  uint64_t total = builtin_total + custom_total;
  if (!total) {
    return;  // nothing selectable
  }

  // Always change if current visual isn't in the program (built-in or custom).
  bool included = false;
  if (_last_custom_index >= 0) {
    included = _visual && _last_custom_index < int(_custom_patterns.size());
  } else {
    for (const auto& type : _program->visual_type()) {
      if (_visual && type.random_weight() && type.type() == _last_visual_selection) {
        included = true;
      }
    }
  }
  // Like !random_chance(chance), but scaled to current speed and cycle length.
  // Roughly 1/2 chance for a cycle of length 2048.
  auto fps = program().global_fps();
  uint32_t stick = (2 * fps * length) / 2048;  // guard random(0) for short cycles
  if (included && length && stick && random(stick) >= 120) {
    return;
  }

  // Weighted pick over the built-in visual types first, then the custom patterns.
  uint64_t r = random(total);
  uint64_t acc = 0;
  trance_pb::Program_VisualType t = trance_pb::Program_VisualType_NONE;
  int custom_index = -1;
  bool picked_builtin = false;
  for (const auto& type : _program->visual_type()) {
    acc += type.random_weight();
    if (r < acc) {
      t = type.type();
      picked_builtin = true;
      break;
    }
  }
  if (!picked_builtin) {
    for (std::size_t i = 0; i < _custom_patterns.size(); ++i) {
      acc += _custom_patterns[i].weight;
      if (r < acc) {
        custom_index = int(i);
        break;
      }
    }
  }

  // A custom pattern was selected: compile it (or just reset if unchanged).
  if (custom_index >= 0) {
    if (_visual && custom_index == _last_custom_index) {
      _visual->reset();
      return;
    }
    const auto& p = _custom_patterns[custom_index];
    _visual.reset(new CompiledVisual{*_visual_api, p.root, p.render_block});
    _last_custom_index = custom_index;
    _custom_visual_name = p.name;
    _last_visual_selection = trance_pb::Program_VisualType_NONE;
    return;
  }

  if (_visual && _last_custom_index < 0 && t == _last_visual_selection) {
    _visual->reset();
    return;
  }
  _last_custom_index = -1;
  _custom_visual_name.clear();
  // Every built-in VisualType is now a compiled pattern (builtin_patterns.cpp); the enum
  // value still identifies it for the overlay. A type with no compiled source (e.g. NONE)
  // leaves the current visual in place.
  auto compiled = _builtin_compiled.find(t);
  if (compiled != _builtin_compiled.end()) {
    _visual.reset(
        new CompiledVisual{*_visual_api, compiled->second.root, compiled->second.render_block});
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

  // The innermost active cycler carrying a phase label on the active path, i.e.
  // the "section" the viewer would name (e.g. SLOW / FAST / INTERLEAVE). We tag at
  // most one label per active path, so this is unambiguous; if two labelled
  // branches were ever active at once it would report the last one visited.
  // Returns null when no active node is labelled (e.g. SUPER_FAST, whose phases
  // live in its own logic, not the tree).
  const Cycler* active_phase(const Cycler* c)
  {
    if (!c || !c->active()) {
      return nullptr;
    }
    const Cycler* best = c->phase().empty() ? nullptr : c;
    for (const Cycler* kid : c->children()) {
      if (const Cycler* deeper = active_phase(kid)) {
        best = deeper;
      }
    }
    return best;
  }

  // Accumulate the theme slots sourced by the currently active image lanes. A
  // literal hint sets its slot; a Runtime hint means an image is live but its slot
  // is decided by hidden state -- the caller resolves that from the last-pulled
  // slot. This is "which grammar lane sources images from which theme", not an exact
  // count of rendered layers (an image can outlive the leaf that loaded it).
  void collect_onscreen_slots(const Cycler* c, bool& primary, bool& alternate, bool& runtime)
  {
    if (!c || !c->active()) {
      return;
    }
    switch (c->image_slot()) {
    case ImageSlotHint::Primary:
      primary = true;
      break;
    case ImageSlotHint::Alternate:
      alternate = true;
      break;
    case ImageSlotHint::Runtime:
      runtime = true;
      break;
    default:
      break;
    }
    for (const Cycler* kid : c->children()) {
      collect_onscreen_slots(kid, primary, alternate, runtime);
    }
  }

  // Short tag for an image-bearing node's theme slot.
  const char* slot_str(ImageSlotHint hint)
  {
    switch (hint) {
    case ImageSlotHint::Primary:
      return "slot[1]";
    case ImageSlotHint::Alternate:
      return "slot[2]";
    case ImageSlotHint::Runtime:
      return "slot[~]";
    default:
      return "";
    }
  }

  // Image slots present anywhere in a subtree (regardless of active state).
  void collect_subtree_slots(const Cycler* c, bool& primary, bool& alternate, bool& runtime)
  {
    if (!c) {
      return;
    }
    switch (c->image_slot()) {
    case ImageSlotHint::Primary:
      primary = true;
      break;
    case ImageSlotHint::Alternate:
      alternate = true;
      break;
    case ImageSlotHint::Runtime:
      runtime = true;
      break;
    default:
      break;
    }
    for (const Cycler* kid : c->children()) {
      collect_subtree_slots(kid, primary, alternate, runtime);
    }
  }

  // For a collapsed labelled subtree (e.g. an inactive <FAST>) whose own node is not
  // an image lane, summarise which image slots live below it, so the collapsed line
  // still says "this section shows the alternate theme". Empty if no image lanes.
  std::string descendant_slot_summary(const Cycler* c)
  {
    bool primary = false;
    bool alternate = false;
    bool runtime = false;
    collect_subtree_slots(c, primary, alternate, runtime);
    if (!primary && !alternate && !runtime) {
      return "";
    }
    std::string s = " [img";
    if (primary) {
      s += " slot[1]";
    }
    if (alternate) {
      s += " slot[2]";
    }
    if (runtime) {
      s += " slot[~]";
    }
    s += "]";
    return s;
  }

  // "pos/len" with pos right-padded to len's digit width, so a node's column stays
  // put as its position grows (e.g. 999 -> 1000 won't shove the rest of the line).
  std::string frames(uint32_t pos, uint32_t len)
  {
    std::string ls = std::to_string(len);
    std::string ps = std::to_string(pos);
    std::string pad(ls.size() > ps.size() ? ls.size() - ps.size() : 0, ' ');
    return pad + ps + "/" + ls;
  }

  // Print one cycler's own line (type, frames, progress, section + image tags). When
  // `collapsed`, its subtree is omitted and it is marked as such; otherwise an active
  // node gets a trailing '*'.
  void append_node_line(std::ostringstream& out, const Cycler* c, int depth, bool collapsed,
                        const std::string& extra = "")
  {
    for (int i = 0; i < depth; ++i) {
      out << "  ";
    }
    // Bar from position/length, not progress(): progress() maps position 0 to the
    // last frame (frame() wraps), so a not-yet-started node would read nearly full.
    float frac = c->length() ? float(c->position()) / float(c->length()) : 0.f;
    out << (depth ? "+-" : "") << c->type_name() << " " << frames(c->position(), c->length())
        << " " << bar(frac, 8);
    if (!c->phase().empty()) {
      out << " <" << c->phase() << ">";
    }
    if (c->image_slot() != ImageSlotHint::None) {
      out << " [" << c->image_label() << " " << slot_str(c->image_slot()) << "]";
    }
    if (!extra.empty()) {
      out << extra;
    }
    if (collapsed) {
      out << " (collapsed)";
    } else if (c->active()) {
      out << " *";
    }
    out << "\n";
  }

  // Render the cycler tree, minimised: recurse only ACTIVE children (the live path),
  // and collapse inactive ones. Labelled / image-bearing inactive children get a
  // one-line summary so the skeleton stays visible; the rest are tallied. This keeps
  // a busy visual (e.g. ACCELERATE's 45 segments) down to the active segment plus a
  // count, instead of a wall of dead nodes.
  void append_cycler(std::ostringstream& out, const Cycler* c, int depth, int max_depth)
  {
    if (!c) {
      return;
    }
    append_node_line(out, c, depth, false);

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

    std::size_t inactive_unlabelled = 0;
    std::size_t active_leaf_actions = 0;
    for (const Cycler* kid : kids) {
      bool annotated = !kid->phase().empty() || kid->image_slot() != ImageSlotHint::None;
      if (kid->active()) {
        // Collapse active, unannotated leaf actions (spiral/text/font/upload/timers)
        // into a count: they are the "what is this?" noise, and the image/section
        // nodes carry the meaning. Recurse into everything else.
        if (kid->children().empty() && !annotated) {
          ++active_leaf_actions;
        } else {
          append_cycler(out, kid, depth + 1, max_depth);
        }
      } else if (annotated) {
        // Inactive but meaningful: one collapsed line. If it is a labelled section
        // (not itself an image lane), summarise the image slots it contains.
        std::string extra = kid->image_slot() == ImageSlotHint::None
            ? descendant_slot_summary(kid)
            : std::string{};
        append_node_line(out, kid, depth + 1, true, extra);
      } else {
        ++inactive_unlabelled;
      }
    }
    if (active_leaf_actions) {
      for (int i = 0; i <= depth; ++i) {
        out << "  ";
      }
      out << "+-(" << active_leaf_actions << " effect action(s))\n";
    }
    if (inactive_unlabelled) {
      for (int i = 0; i <= depth; ++i) {
        out << "  ";
      }
      out << "+-(" << inactive_unlabelled << " inactive)\n";
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

  out << "== TRANCE DEBUG (F1 hide  M mute) ==\n";
  out << "visual : "
      << (_last_custom_index >= 0 ? _custom_visual_name + " [custom]"
                                  : visual_type_name(_last_visual_selection))
      << "\n";

  // NOW: the current section (deepest active labelled cycler) and its progress.
  // Visuals with no labelled section (e.g. SUPER_FAST) show "--".
  const Visual* visual = _visual.get();
  const Cycler* section = visual ? active_phase(visual->cycler()) : nullptr;
  out << "now    : section ";
  if (section) {
    float frac = section->length() ? float(section->position()) / float(section->length()) : 0.f;
    out << section->phase() << "  " << frames(section->position(), section->length()) << " "
        << bar(frac, 8);
  } else {
    out << "--";
  }
  out << "\n";

  // Which theme slots the active image lanes source -- shown as '*' in the THEMES
  // block below (no separate line). Literal lanes mark their slot directly.
  // (Runtime-lane resolution to the actual last-pulled slot was dropped with the
  // api debug breadcrumb; a runtime lane won't mark a '*' until the overlay is
  // rebuilt on the render layer.)
  auto snap = _themes.debug_snapshot();
  bool on_primary = false;
  bool on_alternate = false;
  bool on_runtime = false;
  collect_onscreen_slots(visual ? visual->cycler() : nullptr, on_primary, on_alternate, on_runtime);

  // The two active themes; '*' = currently sourced on screen by an active image lane.
  const auto& pri = snap.slots[1];
  const auto& alt = snap.slots[2];
  out << "themes : " << (on_primary ? "*" : " ") << "pri '" << (pri.valid ? pri.name : "(empty)")
      << "'   " << (on_alternate ? "*" : " ") << "alt '" << (alt.valid ? alt.name : "(empty)")
      << "'\n";

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

  // Entrainment bed: the binaural/isochronic layers synthesised under the
  // visuals. Sourced from the active program; mute state from the live Audio.
  const auto& entrainment = _program->entrainment();
  out << "-- ENTRAINMENT (binaural/isochronic bed) --\n";
  if (entrainment.layer().empty()) {
    out << "bed    : (none)\n";
  } else {
    float master_db = entrainment.master_db() != 0.f ? entrainment.master_db() : -28.f;
    std::snprintf(buf, sizeof(buf), "%.1f", master_db);
    out << "bed    : " << entrainment.layer().size() << " layer(s)   master " << buf << " dB"
        << (_audio && _audio->Muted() ? "   [MUTED]" : "") << "\n";
    int idx = 0;
    for (const auto& l : entrainment.layer()) {
      out << "  L" << idx++ << " : carrier ";
      std::snprintf(buf, sizeof(buf), "%.1f", l.center_hz());
      out << buf << " Hz";
      if (l.binaural_hz() != 0.f) {
        std::snprintf(buf, sizeof(buf), "%.2f", l.binaural_hz());
        out << "  binaural " << buf << " Hz";
      }
      if (l.pulse_hz() != 0.f) {
        std::snprintf(buf, sizeof(buf), "%.2f", l.pulse_hz());
        out << "  pulse " << buf << " Hz";
      } else {
        out << "  pulse cont.";
      }
      std::snprintf(buf, sizeof(buf), "%.1f", l.amplitude_db());
      out << "  " << buf << " dB\n";
    }
  }

  // Cycler tree LAST and its legend ABOVE it: the tree's height varies frame to
  // frame, so anything printed after it would jiggle vertically. Keeping the static
  // legend above and the tree at the very bottom holds every fixed row still.
  out << "-- CYCLER (pos/len=frames; Action=beat OneShot=once Parallel=LCM Sequence=sum\n";
  out << "   Repeat=loop Offset=delay; <..>=section [img slot N]=image lane 1/2/~=pri/alt/runtime;\n";
  out << "   *=active; effect/inactive nodes collapsed) --\n";
  append_cycler(out, visual ? visual->cycler() : nullptr, 0, 20);

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
