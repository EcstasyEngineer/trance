#include <trance/director.h>
#include <trance/visual/builtin_visuals.h>
#include <common/session.h>
#include <common/util.h>
#include <trance/media/audio.h>
#include <trance/media/font.h>
#include <trance/theme_bank.h>
#include <trance/visual/api.h>
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/pattern_parser_v3.h>
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

  uint32_t locked_period_frames(const trance_pb::Program& program)
  {
    float best_db = -1e30f;
    float hz = 0.f;
    for (const auto& l : program.entrainment().layer()) {
      if (l.pulse_hz() > 0.f && l.amplitude_db() >= best_db) {
        best_db = l.amplitude_db();
        hz = l.pulse_hz();
      }
    }
    return hz > 0.f ? uint32_t(double(program.global_fps()) / double(hz) + 0.5) : 0;
  }
}
#include "shaders.h"

Director::Director(const trance_pb::Session& session, const trance_pb::System& system,
                   ThemeBank& themes, const trance_pb::Program& program, Renderer& renderer,
                   Audio& audio)
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
, _audio{audio}
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
  // Entrainment beat period in frames for `every locked` (Extension #2): the period of the
  // program's highest-amplitude pulsed layer. 0 when there is no pulsed bed (`locked` then
  // hard-errors). A compile-time snapshot of the program's bed; it does not track a live
  // reconfigure, which is fine -- the bed is static per program.
  uint32_t locked_frames = locked_period_frames(*_program);
  for (uint32_t t = 1; t <= 8; ++t) {
    // v3 (docs/spec-grammar-v3.md) is the only grammar: two nouns (pattern, effect) and one
    // rule, lowering to the same pattern::Node + RenderStmt IR the compiler always ran.
    std::string v3_source = builtin::pattern_source_v3(t);
    if (v3_source.empty()) {
      continue;
    }
    auto v3 = patternv3::parse(v3_source, locked_frames);
    if (!v3.ok) {
      // Built-in sources are compile-time constants, so a parse failure is a build bug --
      // fail fast and loud rather than risk a null visual at selection time.
      throw std::runtime_error("built-in v3 pattern " + std::to_string(t) +
                               " failed to parse: " + v3.error);
    }
    for (const auto& w : v3.warnings) {
      std::cerr << "v3 built-in " << t << " warning: " << w << std::endl;
    }
    pattern::Parsed parsed;
    parsed.name = std::move(v3.name);
    parsed.weight = 1;
    parsed.root = std::move(v3.root);
    parsed.render_block = std::move(v3.render_block);
    _builtin_compiled.emplace(t, std::move(parsed));
  }
}

void Director::rebuild_custom_patterns()
{
  _custom_patterns.clear();
  _last_custom_index = -1;
  _pinned_builtin_type = 0;
  _pinned_custom_index = -1;
  for (const auto& type : _program->visual_type()) {
    if (type.pinned()) {
      _pinned_builtin_type = static_cast<uint32_t>(type.type());
      break;
    }
  }
  const uint32_t locked_frames = locked_period_frames(*_program);
  for (const auto& src : _program->custom_visual_pattern()) {
    if (!src.enabled()) {
      continue;
    }

    auto v3 = patternv3::parse(src.source_text(), locked_frames);
    if (!v3.ok) {
      // A custom pattern that fails to parse is surfaced and skipped -- not a crash, and
      // not a silent black screen: the rest of the program's visuals still play.
      std::cerr << "skipping custom pattern '" << src.name() << "': " << v3.error << std::endl;
      continue;
    }
    for (const auto& w : v3.warnings) {
      std::cerr << "custom v3 pattern '" << src.name() << "' warning: " << w << std::endl;
    }
    pattern::Parsed parsed;
    // Carry the proto's name/weight (authoritative) over the in-source ones.
    parsed.name = src.name();
    parsed.weight = src.random_weight();
    parsed.root = std::move(v3.root);
    parsed.render_block = std::move(v3.render_block);
    // Recorded here, not from the proto index: a pattern that is disabled or fails
    // to parse never lands in _custom_patterns, so the pin has to follow the entry
    // that actually made it in. A pinned-but-unparseable pattern leaves the pin
    // unset and the program falls back to its normal shuffle.
    if (src.pinned() && _pinned_builtin_type == 0) {
      _pinned_custom_index = int(_custom_patterns.size());
    }
    _custom_patterns.push_back(std::move(parsed));
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

bool Director::render_mutations_enabled() const
{
  // See the declaration: VR_RIGHT is the second of a stereo frame's two passes, so the
  // accumulating render-time state must not advance again on it. (This also covers the
  // old OpenVR stereo path, which has always double-ticked for the same reason.)
  return _render_state != Renderer::State::VR_RIGHT;
}

void Director::toggle_debug_overlay()
{
  _debug_overlay = !_debug_overlay;
}

void Director::play_theme_audio(const std::string& path, bool loop)
{
  _audio.play_theme_audio(path, loop);
}

void Director::stop_theme_audio()
{
  _audio.stop_theme_audio();
}

void Director::set_theme_audio_volume(float volume)
{
  _audio.set_theme_audio_volume(volume);
}

const trance_pb::Program& Director::program() const
{
  return *_program;
}

bool Director::vr_enabled() const
{
  return _renderer.vr_enabled();
}

void Director::force_builtin_visual(uint32_t visual_type)
{
  _forced_builtin_type = visual_type;
  _forced_pattern.reset();
  change_visual(0);
}

std::string Director::force_pattern_from_source(const std::string& source, const std::string& name)
{
  // Same parse path + locked-beat-period source as rebuild_custom_patterns(): themes and
  // entrainment still come from the session/program, only the visual schedule is forced.
  const uint32_t locked_frames = locked_period_frames(*_program);
  auto v3 = patternv3::parse(source, locked_frames);
  if (!v3.ok) {
    return v3.error;
  }
  for (const auto& w : v3.warnings) {
    std::cerr << "--pattern '" << name << "' warning: " << w << std::endl;
  }
  pattern::Parsed parsed;
  parsed.name = name;
  parsed.weight = 1;
  parsed.root = std::move(v3.root);
  parsed.render_block = std::move(v3.render_block);
  _forced_pattern.reset(new pattern::Parsed{std::move(parsed)});
  _forced_builtin_type = 0;
  change_visual(0);
  return {};
}

void Director::set_warp(float amp, float wavelength, float speed)
{
  // v3 wave warp. Called once per frame from the render block (before the image draws); the
  // time base advances per call so the wave animates. amp 0 => the shader leaves coords untouched.
  _warp_amp = amp;
  _warp_wavelength = wavelength;
  _warp_speed = speed;
  // The time base is the accumulating part: it advances once per FRAME, not once per
  // render pass. eval_render suppresses this call entirely on a stereo frame's second
  // pass (VisualRender::render_mutations_enabled), which is what keeps the wave from
  // animating at double speed in VR.
  _warp_time += 1.f / 60.f;
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
  if (!image) {
    // An empty image (failed load / theme with nothing drawable) must not
    // draw: zero dimensions make the scale maths NaN and texture 0 renders a
    // black quad over the frame.
    return;
  }
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

  // The vertex shader projects each quad scaled by
  // k = ((1 - origin) * far + origin) / (far - zoom * (far - 1)) (near = 1):
  // k == 1 exactly when zoom == origin, and k < 1 whenever origin > zoom, which
  // contracts the whole grid toward screen centre. The coverage loops below must
  // measure with the projected size or the uncovered edge clears to black bars.
  // Floored at 1/16: tile count grows as 1/k, and past that point the shrink is
  // an authoring error rather than a coverage bug worth thousands of quads.
  const auto far_p = 1.f + far_plane_distance();
  auto cover = ((1.f - zoom_origin) * far_p + zoom_origin) / (far_p - zoom * (far_p - 1.f));
  cover = std::max(1.f / 16.f, std::min(1.f, cover));

  for (int x = 0;
       cover * x_size * (2 * x - 1) < 1 + std::abs(_system.eye_spacing().eye_spacing()); ++x) {
    for (int y = 0; cover * y_size * (2 * y - 1) < 1; ++y) {
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
  glUniform1f(glGetUniformLocation(_new_program, "warp_amp"), _warp_amp);
  glUniform1f(glGetUniformLocation(_new_program, "warp_wavelength"), _warp_wavelength);
  glUniform1f(glGetUniformLocation(_new_program, "warp_speed"), _warp_speed);
  glUniform1f(glGetUniformLocation(_new_program, "warp_time"), _warp_time);

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
  glUniform1f(glGetUniformLocation(_new_program, "warp_amp"), 0.f);  // text never warps

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
  // --visual / --pattern override (main.cpp): every selection returns the forced
  // built-in or custom pattern, bypassing the weighted shuffle below entirely. Still
  // goes through reset()-vs-rebuild the same way an unforced repeat pick would, so the
  // cycler restarts cleanly each time the forced visual "completes".
  if (_forced_pattern) {
    if (_visual && _last_custom_index == 0) {
      _visual->reset();
      return;
    }
    _visual.reset(
        new CompiledVisual{*_visual_api, _forced_pattern->root, _forced_pattern->render_block});
    _last_custom_index = 0;
    _custom_visual_name = _forced_pattern->name;
    _last_visual_selection = trance_pb::Program_VisualType_NONE;
    return;
  }
  if (_forced_builtin_type) {
    auto compiled = _builtin_compiled.find(_forced_builtin_type);
    if (compiled == _builtin_compiled.end()) {
      // Caller (main.cpp) validates the name against the same table before calling
      // force_builtin_visual(), so this should be unreachable; no-op rather than crash.
      return;
    }
    if (_visual && _last_custom_index < 0 && _last_visual_selection == _forced_builtin_type) {
      _visual->reset();
      return;
    }
    _last_custom_index = -1;
    _custom_visual_name.clear();
    _visual.reset(
        new CompiledVisual{*_visual_api, compiled->second.root, compiled->second.render_block});
    _last_visual_selection = _forced_builtin_type;
    return;
  }

  // Program-level pin (the F2 panel's pin button, VisualTypeConfig/VisualPatternSource
  // .pinned): same force semantics as the CLI overrides above, but sourced from the
  // session rather than the command line, so the CLI still wins. Unlike the overrides
  // this falls THROUGH to the lottery when the pinned visual isn't available (a type
  // with no compiled built-in), rather than no-opping into a frozen screen.
  if (_pinned_custom_index >= 0 && _pinned_custom_index < int(_custom_patterns.size())) {
    if (_visual && _last_custom_index == _pinned_custom_index) {
      _visual->reset();
      return;
    }
    const auto& p = _custom_patterns[_pinned_custom_index];
    _visual.reset(new CompiledVisual{*_visual_api, p.root, p.render_block});
    _last_custom_index = _pinned_custom_index;
    _custom_visual_name = p.name;
    _last_visual_selection = trance_pb::Program_VisualType_NONE;
    return;
  }
  if (_pinned_builtin_type) {
    auto compiled = _builtin_compiled.find(_pinned_builtin_type);
    if (compiled != _builtin_compiled.end()) {
      if (_visual && _last_custom_index < 0 && _last_visual_selection == _pinned_builtin_type) {
        _visual->reset();
        return;
      }
      _last_custom_index = -1;
      _custom_visual_name.clear();
      _visual.reset(
          new CompiledVisual{*_visual_api, compiled->second.root, compiled->second.render_block});
      _last_visual_selection = _pinned_builtin_type;
      return;
    }
  }

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
  // Every built-in VisualType is now a compiled v3 pattern (builtin_patterns_v3.cpp); the enum
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
  // Label a built-in by its v3 name + blurb (the shared catalog) -- the same name
  // --visual accepts and the F2 Visuals section shows.
  std::string visual_type_name(uint32_t t)
  {
    for (const auto& visual : builtin_visuals()) {
      if (visual.type == t) {
        return std::string{visual.name} + " [" + visual.blurb + "]";
      }
    }
    return "(none)";
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

  int debug_theme_index(VisualRender::ThemeSlot slot)
  {
    switch (slot) {
    case VisualRender::ThemeSlot::Primary:
      return 1;
    case VisualRender::ThemeSlot::Alternate:
      return 2;
    default:
      return -1;
    }
  }

  const char* debug_theme_slot_name(int slot)
  {
    switch (slot) {
    case 0:
      return "unloaded ";
    case 1:
      return "primary  ";
    case 2:
      return "secondary";
    case 3:
      return "loading  ";
    default:
      return "unknown  ";
    }
  }

  const char* layer_slot_str(VisualRender::ThemeSlot slot)
  {
    switch (slot) {
    case VisualRender::ThemeSlot::Primary:
      return "pri";
    case VisualRender::ThemeSlot::Alternate:
      return "sec";
    default:
      return "--";
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

std::string Director::status_visual_name() const
{
  // Same logic as draw_debug_overlay()'s "visual :" line -- the status verb reuses what
  // the F1 overlay already knows.
  return _last_custom_index >= 0 ? _custom_visual_name : visual_type_name(_last_visual_selection);
}

bool Director::status_bed_active() const
{
  return !_program->entrainment().layer().empty();
}

void Director::draw_debug_overlay() const
{
  // Lazily load a monospace system font so the theme glyph rows line up. These
  // paths are Windows-specific, which matches the rest of the build.
  if (!_debug_font_loaded) {
    _debug_font_loaded = true;
    _debug_font_ok = _debug_font.openFromFile("C:/Windows/Fonts/consola.ttf") ||
        _debug_font.openFromFile("C:/Windows/Fonts/cour.ttf") ||
        _debug_font.openFromFile("C:/Windows/Fonts/arial.ttf");
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

  auto snap = _themes.debug_snapshot();

  // Image layers composited this frame -- this is the on-screen overlay depth.
  const auto& layers = _visual_api->debug_layers();
  bool theme_on_screen[4] = {false, false, false, false};
  for (const auto& layer : layers) {
    const int slot = debug_theme_index(layer.slot);
    if (slot >= 0 && slot < 4 && layer.alpha > 0.001f) {
      theme_on_screen[slot] = true;
    }
  }

  // ThemeBank's four queue slots, vertically. '*' means at least one image layer from
  // that concrete slot was drawn this frame (alpha > 0), not merely that a cycler lane
  // could source it.
  out << "-- THEMES (*=image drawn this frame) --\n";
  for (int i = 0; i < 4; ++i) {
    const auto& slot = snap.slots[std::size_t(i)];
    out << "  " << (theme_on_screen[i] ? "*" : " ") << debug_theme_slot_name(i) << " : '"
        << (slot.valid ? slot.name : "(empty)") << "'  " << slot.loaded << "/" << slot.total
        << "\n";
  }

  out << "layers : " << layers.size() << " image(s) drawn  alpha=";
  for (const auto& layer : layers) {
    std::snprintf(buf, sizeof(buf), "%.2f", layer.alpha);
    out << buf << "[" << layer_slot_str(layer.slot) << "] ";
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
        << (_audio.Muted() ? "   [MUTED]" : "") << "\n";
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

  sf::Text text{_debug_font};
  text.setCharacterSize(14);
  text.setFillColor(sf::Color(std::uint8_t(120), std::uint8_t(255), std::uint8_t(160)));
  text.setString(out.str());
  text.setPosition({14.f, 12.f});

  auto bounds = text.getLocalBounds();
  sf::RectangleShape backing(sf::Vector2f(bounds.position.x + bounds.size.x + 28.f,
                                          bounds.position.y + bounds.size.y + 24.f));
  backing.setPosition({6.f, 6.f});
  backing.setFillColor(
      sf::Color(std::uint8_t(0), std::uint8_t(0), std::uint8_t(0), std::uint8_t(175)));
  window.draw(backing);
  window.draw(text);

  window.popGLStates();
}
