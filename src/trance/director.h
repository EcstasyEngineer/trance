#ifndef TRANCE_SRC_TRANCE_DIRECTOR_H
#define TRANCE_SRC_TRANCE_DIRECTOR_H
#include <trance/render/render.h>
#include <trance/visual/pattern_ast.h>
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
  // Optional audio handle: used by the debug overlay to report the live
  // entrainment bed and mute state, and bridged to VisualApiImpl for the
  // grammar-driven theme-audio verbs (issue #23). Null in export mode (no
  // realtime audio) -- callers must treat that as a graceful no-op.
  void set_audio(Audio* audio);
  // Theme-audio bridge for VisualApiImpl (mirrors set_warp/render_image's
  // director-as-relay shape). No-ops when _audio is null (export/muted case).
  void play_theme_audio(const std::string& path, bool loop);
  void stop_theme_audio();
  void set_theme_audio_volume(float volume);

  const trance_pb::Program& program() const;
  bool vr_enabled() const;

  // Testing/authoring overrides (--visual / --pattern in main.cpp): pin every visual
  // selection to a single built-in type or a single externally-supplied pattern, instead
  // of the program's weighted shuffle. Call before the first change_visual() runs (i.e.
  // right after construction) so the initial visual is already the forced one.
  // `visual_type` must be a key already in _builtin_compiled (checked by the caller,
  // which knows the valid v3 names); an unknown value here would silently no-op.
  void force_builtin_visual(uint32_t visual_type);
  // Parses `source` with the same v3 path + locked-beat-period as a program's
  // custom_visual_pattern (rebuild_custom_patterns) and pins the result as the only
  // visual ever selected. Returns "" on success; on a parse failure returns the
  // parser's "line:col: message" diagnostic and leaves the current visual untouched.
  // `name` is used only for the debug overlay's "visual:" line.
  std::string force_pattern_from_source(const std::string& source, const std::string& name);

  void render_spiral(float spiral, uint32_t spiral_width, uint32_t spiral_type) const;
  void render_image(const Image& image, float alpha, float zoom_origin, float zoom) const;
  // v3 wave warp: set the per-frame sinusoidal image-displacement state (amp 0 = no warp).
  void set_warp(float amp, float wavelength, float speed);

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

  // v3 wave-warp state (set per frame by set_warp; read by render_image). 0 amp = disabled.
  float _warp_amp = 0.f;
  float _warp_wavelength = 0.2f;
  float _warp_speed = 0.f;
  float _warp_time = 0.f;

  mutable Renderer::State _render_state;
  Renderer& _renderer;
  std::unique_ptr<VisualApiImpl> _visual_api;

  std::uint32_t _last_visual_selection;
  std::unique_ptr<Visual> _visual;

  // Set by force_builtin_visual / force_pattern_from_source (--visual / --pattern in
  // main.cpp). When set, change_visual() skips the weighted shuffle entirely and always
  // (re)selects this one. _forced_pattern set means "forced custom pattern"; otherwise
  // _forced_builtin_type (if nonzero) means "forced built-in".
  uint32_t _forced_builtin_type = 0;
  std::unique_ptr<pattern::Parsed> _forced_pattern;

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
  Audio* _audio;
};

#endif
