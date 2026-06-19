#ifndef TRANCE_SRC_TRANCE_DIRECTOR_H
#define TRANCE_SRC_TRANCE_DIRECTOR_H
#include <trance/render/render.h>
#include <trance/visual/pattern_parser.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <SFML/Graphics.hpp>
#pragma warning(pop)

namespace trance_pb
{
  class Program;
  class Session;
  class System;
}

class Audio;
class Font;
class Image;
class ThemeBank;
class Visual;
class VisualApiImpl;
class Director
{
public:
  Director(const trance_pb::Session& session, const trance_pb::System& system, ThemeBank& themes,
           const trance_pb::Program& program, Renderer& renderer);
  ~Director();

  // Called from play_session() in main.cpp.
  void set_program(const trance_pb::Program& program);
  bool update();
  void render() const;

  // Toggle the on-screen debug overlay (bound to F1 in main.cpp).
  void toggle_debug_overlay();
  // Optional audio handle, used only by the debug overlay to report the live
  // entrainment bed and mute state. Null in export mode (no realtime audio).
  void set_audio(const Audio* audio);

  const trance_pb::Program& program() const;
  bool vr_enabled() const;

  void render_spiral(float spiral, uint32_t spiral_width, uint32_t spiral_type) const;
  void render_image(const Image& image, float alpha, float zoom_origin, float zoom) const;

  sf::Vector2f text_size(const Font& font, const std::string& text, bool large) const;
  void render_text(const Font& font, const std::string& text, bool large, const sf::Color& colour,
                   float scale, const sf::Vector2f& offset, float zoom_origin, float zoom) const;

private:
  void change_visual(uint32_t length);
  // (Re)parse the program's custom_visual_pattern sources into _custom_patterns,
  // skipping (with a warning) any that fail to parse. Called when the program changes.
  void rebuild_custom_patterns();
  // Parse the built-in pattern DSL (builtin_patterns.h) for any VisualType that has
  // been ported to a compiled pattern. Built once; independent of the program.
  void build_builtin_patterns();
  float far_plane_distance() const;
  float eye_offset() const;
  void draw_debug_overlay() const;

  const trance_pb::Session& _session;
  const trance_pb::System& _system;
  ThemeBank& _themes;
  const trance_pb::Program* _program;

  GLuint _new_program;
  GLuint _spiral_program;
  GLuint _quad_buffer;

  mutable Renderer::State _render_state;
  Renderer& _renderer;
  std::unique_ptr<VisualApiImpl> _visual_api;

  std::uint32_t _last_visual_selection;
  std::unique_ptr<Visual> _visual;

  // Authorable custom patterns (Framing B), parsed from the program. Selected in the
  // same weighted shuffle as the built-in visual types. _last_custom_index is the
  // index of the currently-playing custom pattern, or -1 when a built-in is active;
  // _custom_visual_name feeds the overlay's "visual:" line for customs.
  std::vector<pattern::Parsed> _custom_patterns;
  int _last_custom_index;
  std::string _custom_visual_name;

  // Compiled built-in patterns by Program::VisualType value, for those that have been
  // ported off their hardcoded class. change_visual() prefers these over the C++
  // class when present.
  std::unordered_map<uint32_t, pattern::Parsed> _builtin_compiled;

  // Debug overlay state. The font is loaded lazily on first draw from a known
  // monospace system font; _debug_font_ok records whether that succeeded.
  bool _debug_overlay;
  mutable bool _debug_font_loaded;
  mutable bool _debug_font_ok;
  mutable sf::Font _debug_font;
  const Audio* _audio;
};

#endif
