#ifndef TRANCE_SRC_TRANCE_VISUAL_API_H
#define TRANCE_SRC_TRANCE_VISUAL_API_H
#include <trance/media/font.h>
#include <string>
#include <vector>

class Director;
class Image;
class ThemeBank;
namespace trance_pb
{
  class Session;
  class System;
}

class VisualControl
{
public:
  virtual ~VisualControl() = default;

  // 2/3 were SPLIT_WORD_GAPS/SPLIT_LINE_GAPS, reachable only from the retired v1 grammar
  // (no v3 authoring surface, never serialized to a .session). Values kept stable rather
  // than renumbered; the gap is intentional.
  enum SplitType {
    SPLIT_WORD = 0,
    SPLIT_LINE = 1,
    SPLIT_ONCE_ONLY = 4,
  };

  virtual Image get_image(bool alternate = false) const = 0;
  virtual void maybe_upload_next() const = 0;
  // Random pick from the active theme's precanned audio pool (ThemeBank::get_audio),
  // same shape as get_image -- the v3 `audio` effect resolves content
  // (concept/reward/runtime) to a path with this before calling play_theme_audio.
  virtual const std::string& get_theme_audio(bool alternate = false) const = 0;

  virtual void rotate_spiral(float amount) = 0;
  virtual void change_spiral() = 0;
  // Deterministically pin the spiral shape/width (v3 `look { spiral type/width }`), instead of
  // change_spiral()'s random roll. Does not touch the rotation phase.
  virtual void set_spiral(uint32_t type, uint32_t width) = 0;
  virtual void change_animation(bool alternate = false) = 0;
  virtual void change_font(bool force = false) = 0;
  virtual void change_text(SplitType split_type, bool alternate = false) = 0;
  virtual void change_subtext(bool alternate = false) = 0;
  virtual void change_small_subtext(bool force = false, bool alternate = false) = 0;
  virtual bool change_themes() = 0;

  // Grammar-driven theme audio: bridges to Audio::play_theme_audio /
  // stop_theme_audio / set_theme_audio_volume via the director. Single-slot v0,
  // same shape as the single live text slot -- starting a new play stops
  // whatever grammar audio was already playing. No-op (graceful) when there is
  // no live Audio object (export/muted case).
  virtual void play_theme_audio(const std::string& path, bool loop) = 0;
  virtual void stop_theme_audio() = 0;
  virtual void set_theme_audio_volume(float volume) = 0;
};

class VisualRender
{
public:
  virtual ~VisualRender() = default;

  enum class ThemeSlot {
    None,
    Primary,
    Alternate,
  };
  struct DebugLayer {
    float alpha;
    ThemeSlot slot;
  };

  enum class Anim {
    NONE,
    ANIM,
    ANIM_ALTERNATE,
  };
  virtual void render_animation_or_image(Anim type, const Image& image, float alpha,
                                         float zoom_origin, float zoom,
                                         ThemeSlot slot = ThemeSlot::None) const = 0;
  virtual void
  render_image(const Image& image, float alpha, float zoom_origin, float zoom,
               ThemeSlot slot = ThemeSlot::None) const = 0;
  virtual void
  render_text(float zoom_origin, float zoom, float shadow_zoom_origin, float shadow_zoom) const = 0;
  virtual void render_subtext(float alpha, float zoom_origin) const = 0;
  virtual void render_small_subtext(float alpha, float zoom_origin) const = 0;
  virtual void render_spiral() const = 0;
  // Exposed on the render interface so the v3 render block can advance the spiral by a
  // curve-driven per-frame speed (spiral speed is a render param like zoom). Same method as
  // VisualControl::rotate_spiral; VisualApiImpl's single override satisfies both.
  virtual void rotate_spiral(float amount) = 0;
  // v3 wave warp: set the per-frame image-displacement state (amp 0 = disabled).
  virtual void set_warp(float amp, float wavelength, float speed) = 0;
  // Exposed on the render interface so a curve/expr `volume` modulator on the v3 `audio`
  // effect can be applied every frame, the same dual-declaration shape as
  // rotate_spiral above: same method as VisualControl::set_theme_audio_volume;
  // VisualApiImpl's single override satisfies both.
  virtual void set_theme_audio_volume(float volume) = 0;

  // False during the SECOND of the two render passes a stereo frame makes (VR_RIGHT).
  // The render block is evaluated once per eye in stereo, so any render op that
  // ACCUMULATES state -- rotate_spiral's angle, set_warp's time base -- would advance
  // twice per frame, running at double speed and desyncing the two eyes. eval_render
  // consults this and skips those advances on the second pass. Every other state
  // (NONE / VR_MONO / VR_LEFT / export) is the frame's only pass and ticks normally.
  // Note this gates only the RENDER-path callers: the same rotate_spiral reached from a
  // cycler action already runs once per frame and must keep ticking.
  virtual bool render_mutations_enabled() const = 0;
};

class VisualApiImpl : public VisualControl, public VisualRender
{
public:
  VisualApiImpl(Director& director, ThemeBank& themes, const trance_pb::Session& session,
                const trance_pb::System& system, uint32_t height_pixels);
  void update();

  Image get_image(bool alternate = false) const override;
  void maybe_upload_next() const override;
  const std::string& get_theme_audio(bool alternate = false) const override;

  void rotate_spiral(float amount) override;
  void change_spiral() override;
  void set_spiral(uint32_t type, uint32_t width) override;
  void change_animation(bool alternate = false) override;
  void change_font(bool force = false) override;
  void change_text(SplitType split_type, bool alternate = false) override;
  void change_subtext(bool alternate = false) override;
  void change_small_subtext(bool force = false, bool alternate = false) override;
  bool change_themes() override;

  void play_theme_audio(const std::string& path, bool loop) override;
  void stop_theme_audio() override;
  void set_theme_audio_volume(float volume) override;

  void render_animation_or_image(Anim type, const Image& image, float alpha, float zoom_origin,
                                 float zoom, ThemeSlot slot = ThemeSlot::None) const override;
  void render_image(const Image& image, float alpha, float zoom_origin, float zoom,
                    ThemeSlot slot = ThemeSlot::None) const override;
  void render_text(float zoom_origin, float zoom, float shadow_zoom_origin,
                   float shadow_zoom) const override;
  void render_subtext(float alpha, float zoom_origin) const override;
  void render_small_subtext(float alpha, float zoom_origin) const override;
  void render_spiral() const override;
  void set_warp(float amp, float wavelength, float speed) override;
  bool render_mutations_enabled() const override;

  // Debug overlay accessors. Reset the per-frame layer capture at the start of
  // a rendered frame; render_image() then records the alpha of each image layer
  // it draws, which reveals how many images the current visual overlays (e.g.
  // the 3-image SUPER_PARALLEL fade).
  void debug_begin_frame() const;
  const std::vector<VisualRender::DebugLayer>& debug_layers() const;
  float debug_spiral() const;
  uint32_t debug_spiral_type() const;
  uint32_t debug_spiral_width() const;
  const std::string& debug_font() const;
  const std::string& debug_subfont() const;

private:
  float zoom_intensity(float zoom_origin, float zoom) const;

  Director& _director;
  ThemeBank& _themes;
  FontCache _font_cache;

  uint32_t _switch_themes;
  float _spiral;
  uint32_t _spiral_type;
  uint32_t _spiral_width;
  std::string _current_font;
  std::string _current_subfont;
  std::vector<std::string> _subtext;
  std::string _small_subtext;
  float _small_subtext_x;
  float _small_subtext_y;

  std::vector<std::string> _current_text;

  // Image layers drawn during the current frame (debug overlay): alpha + concrete theme slot.
  mutable std::vector<VisualRender::DebugLayer> _debug_layers;
};

#endif
