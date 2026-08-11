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

// Entrainment beat period in frames for `beats N` / `locked` lengths (spec §4.16): the
// period of the program's highest-amplitude pulsed layer, 0 when there is no pulsed bed.
// Shared by Director's pattern builds and main.cpp's --lint, so both parse a program's
// patterns against the same clock.
uint32_t locked_period_frames(const trance_pb::Program& program);

class Director
{
public:
  Director(const trance_pb::Session& session, const trance_pb::System& system, ThemeBank& themes,
           const trance_pb::Program& program, ScreenRenderer& renderer, Audio& audio);
  ~Director();

  // Called from play_session() in main.cpp.
  void set_program(const trance_pb::Program& program);
  void update();
  // One presented frame: the render-mutation epoch, then every pass of it (headset eyes
  // when attached, then always the desktop).
  // `elapsed_seconds` is the PLAYBACK time this frame covers -- wall-clock excluding
  // pause and hide -- and is what render-time accumulating state advances on (trap 1).
  // Not the presentation rate: that changes the moment a 90Hz headset attaches to a 60Hz
  // desktop, which would speed accelerating visuals up by half.
  // `blank` (paused/hidden) reaches the renderer as "no content in the headset this
  // frame" while the desktop pass repaints normally for the F2 panel (D8, trap 9).
  void render(double elapsed_seconds, bool blank) const;

  // Toggle the on-screen debug overlay (bound to F1 in main.cpp).
  void toggle_debug_overlay();
  // `status` verb accessors (docs/spec-mcp-ambient-daemon.md sec 4/5): reports exactly
  // what the F1 debug overlay already knows -- same visual-name logic as
  // draw_debug_overlay()'s "visual :" line, and whether the active program has any
  // entrainment bed layers configured (not muted/playing state; just "is there a bed").
  std::string status_visual_name() const;
  bool status_bed_active() const;
  // The playing visual's IDENTITY, for the F2 panel's row marking -- status_visual_name
  // is a display string (built-ins carry their blurb), so matching rows against it would
  // be string surgery. Built-in type is 0 when a custom pattern (or nothing) is playing;
  // the custom name is empty otherwise.
  uint32_t current_builtin_type() const;
  const std::string& current_custom_name() const;
  // Theme-audio bridge for VisualApiImpl (mirrors set_warp/render_image's
  // director-as-relay shape). Held by reference, not pointer: audio is always live
  // now that the offline video-export path (the one configuration that ran without
  // an Audio) is gone, so there is no null case left to guard.
  void play_theme_audio(const std::string& path, bool loop);
  void stop_theme_audio();
  void set_theme_audio_volume(float volume);

  const trance_pb::Program& program() const;
  // A headset output is attached. Reported by the F2 status section; NOT a question any
  // render code should ask -- what a draw wants to know is whether the CURRENT pass is an
  // eye pass, which is vr_pass().
  bool vr_enabled() const;
  // The pass being rendered targets a headset eye (VR_LEFT/VR_RIGHT) rather than the
  // desktop window. Everything that used to key on "is this a VR renderer" keys on this
  // instead, because with both outputs live a single frame is both: the /2.5 image scale
  // and the text size targets differ per pass, not per run (trap 2).
  bool vr_pass() const;

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

  // True only on the FIRST pass of the frame currently being rendered.
  // A frame is now up to three passes (left eye, right eye, desktop) and the whole render
  // block is evaluated once per pass, so any render-time state that ACCUMULATES per call
  // -- the spiral angle, the warp time base, the stale-image refresh, the F1 layer
  // counters -- would advance two or three times per presented frame, and the eyes would
  // draw different content. Every such op runs on the first pass only; the later passes
  // redraw exactly what it produced.
  bool render_mutations_enabled() const;
  // How much playback time the frame covers, in 60Hz reference frames -- the multiplier
  // for a per-frame accumulation rate. 0 while paused/hidden (state freezes) and on every
  // pass after the first. This is what decouples accumulating visuals from the
  // presentation rate: attaching a 90Hz headset to a 60Hz desktop must not make them run
  // 1.5x faster (trap 1).
  double render_mutation_frames() const;

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

  mutable ScreenRenderer::State _render_state;
  // Render-mutation epoch, set once per frame by render() before any pass runs:
  // the frame's playback-elapsed seconds, and how many passes have started so far.
  mutable double _mutation_seconds;
  mutable uint32_t _pass_index;
  ScreenRenderer& _renderer;
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

  // The program's pinned visual, if any: a pin forces every selection to that one
  // visual, skipping the weight lottery (the program-level equivalent of the
  // --visual/--pattern overrides above, which still win over it). At most one is
  // set -- validate_program enforces a single pin across built-ins and customs.
  // _pinned_custom_index indexes _custom_patterns (post-skip), not the proto's
  // repeated field; both are recomputed by rebuild_custom_patterns/set_program.
  uint32_t _pinned_builtin_type = 0;
  int _pinned_custom_index = -1;

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
  Audio& _audio;
};

#endif
