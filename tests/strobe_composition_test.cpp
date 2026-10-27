// Real parser -> CompiledVisual -> render dispatch, with CPU-only image sentinels.
// No window, GL context, media decoding, flashing display, or wall-clock timing.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include "support/pattern_parser_v3.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
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
patternv3::ParseResult recipe(const std::string& directory, const char* name)
{
  const std::string path = directory + "/" + name + ".pattern";
  std::ifstream input(path);
  require(bool(input), "cannot read " + path);
  auto parsed = patternv3::parse(std::string(std::istreambuf_iterator<char>(input), {}));
  require(parsed.ok, path + ": " + parsed.error);
  require(parsed.warnings.empty(), path + ": unexpected parser warnings");
  return parsed;
}
std::vector<Draw> render(CompiledVisual& visual, Api& api)
{
  api.first_pass = true;
  api.drawn.clear();
  visual.render(api);
  const auto first = api.drawn;
  api.first_pass = false;
  api.drawn.clear();
  visual.render(api);
  require(first == api.drawn, "later output pass changed gate output");
  return first;
}
void held(const std::string& directory, bool animation)
{
  const auto parsed = recipe(directory, animation ? "held-animation" : "held-still");
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (uint32_t frame = 0; frame < 96; ++frame) {
    visual.cycler()->advance();
    const auto draws = render(visual, api);
    const bool on = frame % 12 < 3;
    require(draws.size() == (on ? 1u : 0u), "held gate duty/boundary mismatch");
    if (on) {
      require(draws[0].width == 2 && std::abs(draws[0].alpha - 1.f) < 0.00001f,
              "held draw lost image or alpha");
      require(draws[0].animation == (animation ? VisualRender::Anim::ANIM : VisualRender::Anim::NONE),
              "held animation dispatch mismatch");
    }
    require(api.picks == 1u + frame / 48, "visibility pulse reselected held image");
    require(api.loads == (animation ? 1u + frame / 48 : 0u), "visibility pulse reloaded animation");
  }
}
void reveal(const std::string& directory)
{
  const auto parsed = recipe(directory, "reveal-underlay");
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (uint32_t frame = 0; frame < 48; ++frame) {
    visual.cycler()->advance();
    const auto draws = render(visual, api);
    const bool on = frame % 12 < 3;
    require(draws.size() == (on ? 2u : 1u), "accent gate hid the underlay");
    require(draws[0].width == 2, "underlay must draw first");
    if (on) require(draws[1].width == 3, "accent must composite last");
    require(api.picks == 2, "visibility must not pull new content");
  }
}
void switching(const std::string& directory)
{
  const auto parsed = recipe(directory, "switch-media");
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  uint32_t previous = 0;
  for (uint32_t frame = 0; frame < 48; ++frame) {
    visual.cycler()->advance();
    const auto draws = render(visual, api);
    require(draws.size() == 1, "media switching must have no dark gap");
    if (frame > 0)
      require((draws[0].width == previous) == (frame % 6 != 0), "alternate side cadence mismatch");
    previous = draws[0].width;
    require(api.picks == 1u + frame / 6, "switching must select exactly once per cut");
  }
}
void nested(const std::string& directory)
{
  const auto parsed = recipe(directory, "nested-gate");
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (uint32_t frame = 0; frame < 46; ++frame) {
    visual.cycler()->advance();
    const auto draws = render(visual, api);
    const auto branch = (frame % 23) % 14;
    const bool on = branch < 10 && branch % 6 < 3;
    require(draws.size() == (on ? 1u : 0u), "nested gate failed re-entry, inactivity or clipping");
    require(api.picks == 2u * (frame / 23) + 1u + ((frame % 23) >= 14 ? 1u : 0u), "nested image must select once per interrupt entry");
  }
}
void prototype_default(const std::string& directory)
{
  const auto parsed = recipe(directory, "prototype-default");
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  unsigned visible_frames = 0, windows = 0;
  bool previously_on = false;
  for (uint32_t frame = 0; frame < 192; ++frame) {
    visual.cycler()->advance();
    const auto draws = render(visual, api);
    const bool on = frame % 12 < 4;
    // The subject itself is gated; no invisible bookkeeping draw is emitted.
    require(draws.size() == (on ? 2u : 1u), "prototype default draw count mismatch");
    require(draws[0].width == 3 && draws[0].alpha == 1.f,
            "prototype backdrop must remain visible behind the gate");
    if (on) {
      ++visible_frames;
      if (!previously_on) ++windows;
      require(draws[1].width == 2 && draws[1].alpha == 1.f,
              "prototype gate must draw selected primary above backdrop");
      const float expected = 0.5f * static_cast<float>(frame % 48) / 48.f;
      require(std::abs(draws[1].zoom - expected) < 0.00001f,
              "prototype curve must follow cut, not gate, clock");
    }
    require(api.picks == 2u + frame / 48,
            "prototype must select one backdrop and one primary per cut");
    previously_on = on;
  }
  require(visible_frames == 64 && windows == 16 && api.picks == 5,
          "prototype default trace must match static authoring model");
}
}
int main(int argc, char** argv)
{
  try {
    require(argc == 2, "expected strobe recipe directory argument");
    held(argv[1], false);
    held(argv[1], true);
    reveal(argv[1]);
    switching(argv[1]);
    nested(argv[1]);
    prototype_default(argv[1]);
    std::cout << "strobe composition: 526 ticks, repeated render passes, six recipes passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
