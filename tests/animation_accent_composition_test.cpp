#include "support/legacy_builtins.h"
// Authored animation/alternate identity across ramp expansion, through real
// parser -> CompiledVisual -> render dispatch with CPU-only image sentinels.
// No window, GL context, media decoding, flashing display, or wall-clock timing.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include "support/pattern_parser_v3.h"
#include <trance/visual/builtin_patterns.h>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string& message)
{
  if (!value) throw std::runtime_error(message);
}
Image picture(uint32_t width)
{
  std::vector<unsigned char> pixels(width * 4, 255);
  return Image(width, 1, pixels.data());
}
struct Draw {
  uint32_t width;
  float alpha;
  float zoom;
  VisualRender::Anim animation;
  bool operator==(const Draw& other) const {
    return width == other.width && alpha == other.alpha && zoom == other.zoom
        && animation == other.animation;
  }
};
struct Api : VisualControl, VisualRender {
  Image primary = picture(2), secondary = picture(3);
  mutable unsigned picks = 0;
  unsigned loads = 0;
  bool first_pass = true;
  mutable std::vector<Draw> drawn;
  Image get_image(bool alternate, bool* fresh) const override {
    ++picks;
    if (fresh) *fresh = true;
    return alternate ? secondary : primary;
  }
  Image get_current_theme_image(bool alternate) const override {
    return alternate ? secondary : primary;
  }
  uint32_t lane_generation(bool) const override { return 0; }
  const std::string& get_theme_audio(bool) const override { static std::string s; return s; }
  void rotate_spiral(float) override {}
  void change_spiral() override {}
  void set_spiral(uint32_t, uint32_t) override {}
  void change_animation(bool) override { ++loads; }
  void change_font(bool) override {}
  void change_text(SplitType, bool) override {}
  void change_subtext(bool) override {}
  void change_small_subtext(bool, bool) override {}
  bool change_themes() override { return false; }
  void play_theme_audio(const std::string&, bool) override {}
  void stop_theme_audio() override {}
  void set_theme_audio_volume(float) override {}
  void render_animation_or_image(Anim animation, const Image& image, float alpha,
                                 float, float zoom, ThemeSlot) const override {
    drawn.push_back({image.width(), alpha, zoom, animation});
  }
  void render_image(const Image& image, float alpha, float, float zoom, ThemeSlot) const override {
    drawn.push_back({image.width(), alpha, zoom, Anim::NONE});
  }
  void render_text(float, float, float, float) const override {}
  void render_subtext(float, float) const override {}
  void render_small_subtext(float, float) const override {}
  void render_spiral() const override {}
  void set_warp(float, float, float) override {}
  bool render_mutations_enabled() const override { return first_pass; }
  double render_mutation_frames() const override { return first_pass ? 1. : 0.; }
};

void trace(const std::string& label, const std::string& source, uint32_t span, unsigned scenes,
           unsigned expected_cuts, unsigned nth = 0, bool alternating = false)
{
  const auto parsed = patternv3::parse(source);
  require(parsed.ok, parsed.error);
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (unsigned scene = 0; scene < scenes; ++scene) {
    if (scene) visual.cycler()->reset();
    unsigned accents = 0, cuts = 0, animated_frames = 0;
    unsigned before = api.picks, loads_before = api.loads;
    std::vector<unsigned> first_sides;
    for (uint32_t frame = 0; frame < span; ++frame) {
      const auto previous = api.picks;
      visual.cycler()->advance();
      api.drawn.clear();
      visual.render(api);
      require(api.drawn.size() == 1, "expected one active image draw");
      const bool animated = api.drawn.front().animation != VisualRender::Anim::NONE;
      const auto first_draws = api.drawn;
      const auto loads_after = api.loads;
      const auto picks_after = api.picks;
      api.first_pass = false;
      api.drawn.clear();
      visual.render(api);
      require(api.drawn == first_draws && api.loads == loads_after && api.picks == picks_after,
              label + ": second output pass must not advance selection/cadence state");
      api.first_pass = true;
      if (animated) ++animated_frames;
      if (api.picks != previous) {
        ++cuts;
        const auto firing = scene * expected_cuts + cuts;
        if (nth) require(animated == (firing % nth == 0), label + ": accent must count authored cuts");
        if (animated) ++accents;
        if (alternating) require(api.drawn.front().width == (firing % 2 ? 3u : 2u),
                                 label + ": alternate must flip on consecutive cuts");
        if (first_sides.size() < 8) first_sides.push_back(api.drawn.front().width);
      }
    }
    require(cuts == expected_cuts, label + ": expected cut count");
    if (nth) {
      require(accents == ((scene + 1) * cuts) / nth - (scene * cuts) / nth,
              label + ": expected animation accent count");
      require(api.loads - loads_before == cuts, label + ": keep existing animation load side effects");
    }
    std::cout << label << " scene=" << scene + 1 << " cuts=" << cuts
              << " picks=" << api.picks - before << " loads=" << api.loads - loads_before
              << " animated_cuts=" << accents << " animated_frames=" << animated_frames
              << " first_sides=";
    for (auto side : first_sides) std::cout << side << ',';
    std::cout << '\n';
  }
}
void identities()
{
  const auto parsed = patternv3::parse("pattern p for 64f {every ramp 8f -> 8f steps 8 {image alternate chance 0.5 anim every 4th chance 0.5 word primary chance 0.5}}");
  require(parsed.ok, parsed.error);
  std::set<std::string> rolls, pulses, toggles;
  std::function<void(const pattern::Node&)> visit = [&](const pattern::Node& n) {
    for (const auto& e : n.effects) {
      if (e.kind == pattern::Effect::Kind::Roll) rolls.insert(e.target);
      if (e.kind == pattern::Effect::Kind::Pulse) pulses.insert(e.target);
      if (e.kind == pattern::Effect::Kind::Toggle) toggles.insert(e.target);
    }
    for (const auto& child : n.children) visit(child);
  };
  visit(parsed.root);
  require(rolls.size() == 3 && pulses.size() == 1 && toggles.size() == 1,
          "three distinct authored chance sites; one shared authored animation/alternate policy");

  const auto sibling = patternv3::parse("pattern p for 16f {every 4f {image primary anim every 4th image secondary anim every 2nd}}");
  require(sibling.ok, sibling.error);
  Api api;
  CompiledVisual visual(api, sibling.root, sibling.render_block);
  for (unsigned frame = 0; frame < 16; ++frame) {
    visual.cycler()->advance(); api.drawn.clear(); visual.render(api);
    require(api.drawn.size() == 2, "both sibling statements draw");
    const auto cut = frame / 4 + 1;
    require((api.drawn[0].animation != VisualRender::Anim::NONE) == (cut % 4 == 0),
            "first authored statement owns an independent every-fourth counter");
    require((api.drawn[1].animation != VisualRender::Anim::NONE) == (cut % 2 == 0),
            "second authored statement owns an independent every-second counter");
  }
}
}
int main()
{
  try {
    trace("accelerate", builtin::pattern_source_v3(1), 2048, 4, 140, 4);
    trace("plain", "pattern p for 64f { every 8f {image primary anim every 4th}}", 64, 2, 8, 4);
    trace("ramp_alternate", "pattern p for 64f {every ramp 8f -> 8f steps 8 {image alternate}}", 64, 2, 8, 0, true);
    trace("plain_alternate", "pattern p for 64f {every 8f {image alternate}}", 64, 2, 8, 0, true);
    trace("nested_ramp", "pattern p for 64f {every ramp 16f -> 16f steps 4 {every ramp 4f -> 4f steps 4 {image primary anim every 4th}}}", 64, 2, 16, 4);
    trace("policy_lifetime", "pattern p for 24f {every 8f {image alternate anim every 4th}}", 24, 3, 3, 4, true);
    identities();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
