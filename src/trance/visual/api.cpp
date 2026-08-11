#include <trance/visual/api.h>
#include <common/media/image.h>
#include <common/util.h>
#include <trance/director.h>
#include <trance/theme_bank.h>
#include <cstdint>
#include <iostream>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  const uint32_t spiral_type_max = 7;

  sf::Color colour2sf(const trance_pb::Colour& colour)
  {
    return sf::Color(std::uint8_t(colour.r() * 255), std::uint8_t(colour.g() * 255),
                     std::uint8_t(colour.b() * 255), std::uint8_t(colour.a() * 255));
  }

  std::vector<std::string> SplitText(const std::string& text, bool split_words)
  {
    std::vector<std::string> result;
    std::string s = text;
    while (!s.empty()) {
      auto of = split_words ? " \t\r\n" : "\r\n";
      auto p = s.find_first_of(of);
      auto q = s.substr(0, p != std::string::npos ? p : s.size());
      if (!q.empty()) {
        result.push_back(q);
      }
      s = s.substr(p != std::string::npos ? 1 + p : s.size());
    }
    if (result.empty()) {
      result.emplace_back();
    }
    return result;
  }
}

VisualApiImpl::VisualApiImpl(Director& director, ThemeBank& themes,
                             const trance_pb::Session& session, const trance_pb::System& system,
                             uint32_t height_pixels)
: _director{director}
, _themes{themes}
, _font_cache{_themes.get_root_path(), session, height_pixels / 3, height_pixels / 12,
              system.font_cache_size()}
, _switch_themes{0}
, _spiral{0}
, _spiral_type{0}
, _spiral_width{60}
, _small_subtext_x{0}
, _small_subtext_y{0}
{
  change_font(true);
  change_spiral();
  change_subtext();
}

void VisualApiImpl::update()
{
  ++_switch_themes;
}

Image VisualApiImpl::get_image(bool alternate) const
{
  return _themes.get_image(alternate);
}

Image VisualApiImpl::get_current_theme_image(bool alternate) const
{
  return _themes.get_current_theme_image(alternate);
}

uint32_t VisualApiImpl::lane_generation(bool alternate) const
{
  return _themes.lane_generation(alternate);
}

const std::string& VisualApiImpl::get_theme_audio(bool alternate) const
{
  return _themes.get_audio(alternate);
}

void VisualApiImpl::rotate_spiral(float amount)
{
  if (!_director.program().reverse_spiral_direction()) {
    amount *= -1;
  }
  _spiral += amount / (32 * sqrt(float(_spiral_width)));
  while (_spiral > 1.f) {
    _spiral -= 1.f;
  }
  while (_spiral < 0.f) {
    _spiral += 1.f;
  }
}

void VisualApiImpl::change_spiral()
{
  if (random_chance(4)) {
    return;
  }
  _spiral_type = random(spiral_type_max);
  _spiral_width = 360 / (1 + random(6));
}

void VisualApiImpl::set_spiral(uint32_t type, uint32_t width)
{
  // Deterministic pin (v3 look{}). type is 1-based in the shader (spiral_type 1..7); clamp.
  _spiral_type = type == 0 ? 0 : (type > spiral_type_max ? spiral_type_max : type) - 1;
  _spiral_width = width == 0 ? _spiral_width : width;
}

void VisualApiImpl::change_animation(bool alternate)
{
  _themes.change_animation(alternate);
}

void VisualApiImpl::change_font(bool force)
{
  if (force || random_chance(4)) {
    _current_font = _themes.get_font(false);
  }
  if (force || random_chance(4)) {
    _current_subfont = _themes.get_font(false);
  }
}

void VisualApiImpl::change_text(SplitType split_type, bool alternate)
{
  bool split_word = split_type == SPLIT_WORD;
  bool once_only = split_type == SPLIT_ONCE_ONLY;

  if (_current_text.empty() && once_only) {
    return;
  }
  if (!_current_text.empty()) {
    _current_text.erase(_current_text.begin());
    if (!_current_text.empty() || once_only) {
      return;
    }
  }
  _current_text = SplitText(_themes.get_text(alternate, true), split_word);
}

void VisualApiImpl::change_subtext(bool alternate)
{
  static const uint32_t count = 16;
  _subtext.clear();
  for (uint32_t i = 0; i < count; ++i) {
    auto s = _themes.get_text(alternate, false);
    for (auto& c : s) {
      if (c == '\n') {
        c = ' ';
      }
    }
    if (!s.empty()) {
      _subtext.push_back(s);
    }
  }
}

void VisualApiImpl::change_small_subtext(bool force, bool alternate)
{
  if (force || _small_subtext.empty()) {
    _small_subtext = _themes.get_text(alternate, false);
    std::replace(_small_subtext.begin(), _small_subtext.end(), '\n', ' ');
    float x = _small_subtext_x;
    float y = _small_subtext_y;
    while (std::abs(x - _small_subtext_x) < 1.f / 8) {
      x = (random_chance() ? 1 : -1) * random(64) / 128.f;
    }
    while (std::abs(y - _small_subtext_y) < 1.f / 4) {
      y = (random_chance() ? 1 : -1) * (16 + random(112)) / 128.f;
    }
    _small_subtext_x = x;
    _small_subtext_y = y;
  } else {
    _small_subtext.clear();
  }
}

bool VisualApiImpl::change_themes()
{
  if (_switch_themes < 2048 || random_chance(4)) {
    return false;
  }
  if (_themes.change_themes()) {
    _switch_themes = 0;
    if (_current_font.empty()) {
      change_font(true);
    }
    return true;
  }
  return false;
}

void VisualApiImpl::begin_pass() const
{
  // A frame is up to three passes (left eye, right eye, desktop) and the render block is
  // evaluated once per pass. The first pass RECORDS its animation-lane pulls; the rest
  // replay them from the start of that recording.
  _pass_animation_cursor = 0;
  if (_director.render_mutations_enabled()) {
    _pass_animations.clear();
  }
}

Image VisualApiImpl::pass_animation(bool alternate) const
{
  // ThemeBank::get_animation is NOT a pure read. When the lane's theme has no gif of its
  // own -- which theme_bank.cpp notes is MOST themes -- it falls through to
  // get_still_image, a weighted/shuffled random pick that also advances the anti-repeat
  // recency bookkeeping. Called once per pass, that draws a different still in the left
  // eye, the right eye and the desktop mirror of the SAME frame (and burns three recency
  // slots for one on-screen image). `anim` appears in nearly every built-in, so this is
  // the ordinary case with a headset attached, and a mirror that disagrees with the
  // headset is exactly what D4 says must not happen.
  //
  // So: resolve on the frame's first pass, replay on the others -- the same
  // once-per-frame-before-all-passes rule as the stale-register refresh in
  // compiled_visual.cpp and the spiral/warp advances in render_eval.cpp (spec trap 1).
  // Replay by ORDER rather than by lane so a block with two `anim` draws still gets two
  // independent picks, exactly as a desktop-only run does today; eval_render is
  // deterministic given the registers and the cycler, neither of which moves between the
  // passes of one frame, so every pass makes the identical sequence of calls.
  if (_director.render_mutations_enabled()) {
    Image resolved = _themes.get_animation(alternate);
    _pass_animations.push_back(resolved);
    return resolved;
  }
  if (_pass_animation_cursor < _pass_animations.size()) {
    return _pass_animations[_pass_animation_cursor++];
  }
  // Unreachable while the call sequence matches the recording; pulling live is a strictly
  // better failure than drawing nothing if it ever stops matching.
  return _themes.get_animation(alternate);
}

void VisualApiImpl::render_animation_or_image(Anim type, const Image& image, float alpha,
                                              float zoom_origin, float zoom,
                                              ThemeSlot slot) const
{
  Image anim;
  if (type != Anim::NONE) {
    anim = pass_animation(type == Anim::ANIM_ALTERNATE);
  }

  if (anim) {
    ThemeSlot anim_slot = type == Anim::ANIM_ALTERNATE ? ThemeSlot::Alternate : ThemeSlot::Primary;
    // Already the live frame, pulled a line ago -- no substitution to do.
    render_image_raw(anim, alpha, zoom_origin, zoom, anim_slot);
  } else {
    render_image(image, alpha, zoom_origin, zoom, slot);
  }
}

void VisualApiImpl::render_image(const Image& image, float alpha, float zoom_origin,
                                 float zoom, ThemeSlot slot) const
{
  // An `image` effect CAPTURES one Image into a register (compiled_visual.cpp) and the
  // render block then redraws that captured value every frame until the next capture.
  // For a still that is exactly right. For a theme that is nothing but gifs -- where
  // get_image can only answer with a frame of an animation -- it freezes the gif on
  // whichever frame happened to be on screen when the effect fired, so the same theme
  // looks animated under a pattern that draws `anim` and like a stuck photograph under
  // one that draws a plain `image`. Re-read the lane's live frame instead: the register
  // carries the slot it was filled from, which is all this needs to know.
  //
  // Deliberately gated on the theme having NO stills, rather than on "this Image came
  // from the animation lane": that is a durable property of the theme, cheap to test,
  // and it leaves every ordinary still theme on the captured-value path untouched.
  //
  // Known edge: a fade whose `prev` register was captured before a theme swap will show
  // the NEW lane's live frame for the rest of the fade instead of the old theme's frame,
  // because the register records the slot and not which theme filled it. Bounded to
  // fades that straddle a swap, and the alternative is a gif that never moves.
  if ((slot == ThemeSlot::Primary || slot == ThemeSlot::Alternate) &&
      _themes.lane_is_animation_only(slot == ThemeSlot::Alternate)) {
    // Through the per-frame latch like every other get_animation call. On this path the
    // lane has no stills by definition, so the random fallback inside get_animation is out
    // of reach and the live frame is already pass-stable -- but routing it here keeps
    // "one get_animation per frame, whatever the caller" a property of the class rather
    // than of a case analysis that a later theme-lane change could quietly invalidate.
    Image live = pass_animation(slot == ThemeSlot::Alternate);
    if (live) {
      render_image_raw(live, alpha, zoom_origin, zoom, slot);
      return;
    }
  }
  render_image_raw(image, alpha, zoom_origin, zoom, slot);
}

void VisualApiImpl::render_image_raw(const Image& image, float alpha, float zoom_origin,
                                     float zoom, ThemeSlot slot) const
{
  // Debug capture is a per-FRAME tally, so it records on the frame's first pass only:
  // pushing per pass would triple the F1 overlay's layer count the moment a headset
  // attached (trap 1). The later passes redraw the identical layers.
  if (_director.render_mutations_enabled()) {
    _debug_layers.push_back({alpha, slot});
  }
  _director.render_image(image, alpha, zoom_origin, zoom_intensity(zoom_origin, zoom));
}

void VisualApiImpl::render_text(float zoom_origin, float zoom, float shadow_zoom_origin,
                                float shadow_zoom) const
{
  if (_current_font.empty() || _current_text.empty() || _current_text.front().empty()) {
    return;
  }
  const auto& text = _current_text.front();
  const auto& font = _font_cache.get_font(_current_font);

  auto size = _director.text_size(font, text, true);
  if (!size.x || !size.y) {
    return;
  }
  // Per PASS, not per run (trap 2): the same frame's eye passes and desktop pass want
  // different text targets, since the headset quad subtends a far wider field.
  auto target_x = _director.vr_pass() ? 3.f / 8.f : 5.f / 8.f;
  auto target_y = 1.f / 3.f;
  auto scale = std::min(target_x / size.x, target_y / size.y);

  auto shadow_colour = colour2sf(_director.program().shadow_text_colour());
  auto main_colour = colour2sf(_director.program().main_text_colour());
  _director.render_text(font, text, true, shadow_colour, 1.2f * scale, {}, shadow_zoom_origin,
                        zoom_intensity(shadow_zoom_origin, shadow_zoom));
  _director.render_text(font, text, true, main_colour, scale, {}, zoom_origin,
                        zoom_intensity(zoom_origin, zoom));
}

void VisualApiImpl::render_subtext(float alpha, float zoom_origin) const
{
  if (_current_subfont.empty() || _subtext.empty()) {
    return;
  }
  const auto& font = _font_cache.get_font(_current_subfont);
  auto target_y = _director.vr_pass() ? 1.f / 32.f : 1.f / 16.f;

  sf::Vector2f size;
  std::string text;
  size_t n = 0;
  auto make_text = [&] {
    text.clear();
    size_t iterations = 0;
    do {
      text += " " + _subtext[n];
      n = (n + 1) % _subtext.size();
      size = _director.text_size(font, text, false);
      ++iterations;
    } while (size.x * target_y / size.y < 1.f && iterations < 64);
  };

  make_text();
  if (!size.x || !size.y) {
    return;
  }
  auto scale = target_y / size.y;

  auto colour = colour2sf(_director.program().shadow_text_colour());
  colour.a = uint8_t(colour.a * alpha);
  _director.render_text(font, text, false, colour, scale, {}, 0, 0);

  auto offset = 2 * target_y + 1.f / 512;
  for (int i = 1; (i - 1) * 2 * target_y < 1.f; ++i) {
    make_text();
    if (size.x && size.y) {
      _director.render_text(font, text, false, colour, scale, sf::Vector2f{0, i * offset},
                            zoom_origin, zoom_intensity(zoom_origin, zoom_origin));
    }

    make_text();
    if (size.x && size.y) {
      _director.render_text(font, text, false, colour, scale, -sf::Vector2f{0, i * offset},
                            zoom_origin, zoom_intensity(zoom_origin, zoom_origin));
    }
  }
}

void VisualApiImpl::render_small_subtext(float alpha, float zoom_origin) const
{
  if (_current_subfont.empty() || _small_subtext.empty()) {
    return;
  }
  const auto& font = _font_cache.get_font(_current_subfont);

  auto size = _director.text_size(font, _small_subtext, false);
  if (!size.x || !size.y) {
    return;
  }
  auto target_y = _director.vr_pass() ? 1.f / 24.f : 1.f / 8.f;
  auto scale = target_y / size.y;

  auto colour = colour2sf(_director.program().shadow_text_colour());
  colour.a = uint8_t(colour.a * alpha);
  _director.render_text(font, _small_subtext, false, colour, scale,
                        {_small_subtext_x / 2, _small_subtext_y / 2}, zoom_origin,
                        zoom_intensity(zoom_origin, zoom_origin));
}

void VisualApiImpl::play_theme_audio(const std::string& path, bool loop)
{
  _director.play_theme_audio(path, loop);
}

void VisualApiImpl::stop_theme_audio()
{
  _director.stop_theme_audio();
}

void VisualApiImpl::set_theme_audio_volume(float volume)
{
  _director.set_theme_audio_volume(volume);
}

bool VisualApiImpl::render_mutations_enabled() const
{
  return _director.render_mutations_enabled();
}

double VisualApiImpl::render_mutation_frames() const
{
  return _director.render_mutation_frames();
}

void VisualApiImpl::set_warp(float amp, float wavelength, float speed)
{
  _director.set_warp(amp, wavelength, speed);
}

void VisualApiImpl::render_spiral() const
{
  _director.render_spiral(_spiral, _spiral_width, _spiral_type);
}

float VisualApiImpl::zoom_intensity(float zoom_origin, float zoom) const
{
  return zoom_origin + (zoom - zoom_origin) * _director.program().zoom_intensity();
}

void VisualApiImpl::debug_begin_frame() const
{
  _debug_layers.clear();
}

const std::vector<VisualRender::DebugLayer>& VisualApiImpl::debug_layers() const
{
  return _debug_layers;
}

float VisualApiImpl::debug_spiral() const
{
  return _spiral;
}

uint32_t VisualApiImpl::debug_spiral_type() const
{
  return _spiral_type;
}

uint32_t VisualApiImpl::debug_spiral_width() const
{
  return _spiral_width;
}
