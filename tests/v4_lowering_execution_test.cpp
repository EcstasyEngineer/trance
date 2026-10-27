#include "support/legacy_builtins.h"
// Compare current builtins with freshly lowered V4 examples through native execution.
// No window, GL context, media decoding, flashing display, or wall-clock timing.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include "support/pattern_parser_v3.h"
#include <trance/visual/builtin_patterns.h>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <common/util.h>
#include <algorithm>
#include <cmath>
#include <map>
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
  float origin;
  VisualRender::Anim animation;
  bool operator==(const Draw& other) const {
    return width == other.width && alpha == other.alpha && zoom == other.zoom
        && origin == other.origin && animation == other.animation;
  }
};
struct Api : VisualControl, VisualRender {
  Image primary = picture(2), secondary = picture(3);
  mutable unsigned picks = 0;
  unsigned loads = 0;
  bool first_pass = true;
  mutable std::vector<Draw> drawn;
  Image get_image(bool alternate, bool* fresh) const override {
    (void)alternate;
    ++picks;
    if (fresh) *fresh = true;
    return picture(10 + picks);
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
                                 float origin, float zoom, ThemeSlot) const override {
    drawn.push_back({image.width(), alpha, zoom, origin, animation});
  }
  void render_image(const Image& image, float alpha, float origin, float zoom, ThemeSlot) const override {
    drawn.push_back({image.width(), alpha, zoom, origin, Anim::NONE});
  }
  void render_text(float, float, float, float) const override {}
  void render_subtext(float, float) const override {}
  void render_small_subtext(float, float) const override {}
  void render_spiral() const override {}
  void set_warp(float, float, float) override {}
  bool render_mutations_enabled() const override { return first_pass; }
  double render_mutation_frames() const override { return first_pass ? 1. : 0.; }
};


// Random media side is intentionally outside exact equivalence: V4 either
// resolves a coherent still/animation side, unlike the old independent rolls.
// Identity sentinels ignore side; animation comparison checks gating only.
struct Frame {
  std::vector<Draw> draws;
  unsigned picks, loads;
};
void deterministic_burst(pattern::Node& node)
{
  if (node.type == pattern::Node::Type::Burst) {
    node.burst_chance_den = 1;
    node.burst_dur_min = 8;
    node.burst_dur_max = 8;
  }
  for (auto& child : node.children) deterministic_burst(child);
}
std::vector<Frame> trace(const std::string& source, unsigned frames)
{
  auto parsed = patternv3::parse(source);
  require(parsed.ok, parsed.error);
  deterministic_burst(parsed.root);
  get_mersenne_twister().seed(7411);
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  std::vector<Frame> result;
  result.reserve(frames);
  for (unsigned frame = 0; frame < frames; ++frame) {
    visual.cycler()->advance();
    api.drawn.clear(); visual.render(api);
    result.push_back({api.drawn, api.picks, api.loads});
    api.first_pass = false; api.drawn.clear(); visual.render(api);
    require(api.drawn == result.back().draws && api.picks == result.back().picks
            && api.loads == result.back().loads,
            "second output pass changes draws, picks, or animation loads at " + std::to_string(frame));
    api.first_pass = true;
  }
  return result;
}
void compare(const std::string& name, unsigned visual_id, unsigned period,
             const std::filesystem::path& directory)
{
  std::ifstream input(directory / (name + ".pattern"));
  require(bool(input), "missing generated source " + name);
  std::ostringstream source; source << input.rdbuf();
  const unsigned frames = 2 * period + 129; // both root loops and all 32/48/64f offsets
  const auto builtin_trace = trace(builtin::pattern_source_v3(visual_id), frames);
  const auto v4_trace = trace(source.str(), frames);
  for (unsigned f = 0; f < frames; ++f) {
    const auto& a = builtin_trace[f]; const auto& b = v4_trace[f];
    const auto context = name + " frame " + std::to_string(f);
    require(a.picks == b.picks, context + " pick count mismatch");
    require(a.loads == b.loads, context + " animation load count mismatch");
    require(a.draws.size() == b.draws.size(), context + " image draw count mismatch");
    for (size_t i = 0; i < a.draws.size(); ++i) {
      const auto& x = a.draws[i]; const auto& y = b.draws[i];
      require(x.width == y.width, context + " image identity/order mismatch");
      require(std::abs(x.alpha-y.alpha) < 0.00001f, context + " alpha mismatch");
      require(std::abs(x.zoom-y.zoom) < 0.00001f, context + " zoom mismatch " + std::to_string(x.zoom) + " vs " + std::to_string(y.zoom));
      require(std::abs(x.origin-y.origin) < 0.00001f, context + " origin mismatch");
      require((x.animation != VisualRender::Anim::NONE) == (y.animation != VisualRender::Anim::NONE),
              context + " animation gate mismatch");
    }
  }
  std::cout << "PASS " << name << ": " << frames << " frames, two complete loops plus offset edges; "
            << "image identity/order/alpha/zoom/origin/animation gates, picks/loads, second pass\n";
}
}
int main(int argc, char** argv)
{
  try {
    require(argc == 2, "usage: v4_lowering_execution_test GENERATED_PATTERN_DIRECTORY");
    const char* names[] = {"accelerate", "slow_flash", "sub_text", "flash_text", "simple", "super_parallel", "animation", "super_fast"};
    const unsigned spans[] = {2048, 3072, 1024, 1024, 2048, 1152, 1024, 2048};
    for (unsigned i = 0; i < 8; ++i) compare(names[i], i+1, spans[i], argv[1]);
    std::cout << "COVERAGE: actual parser -> compiled visual -> recording renderer; Super Fast chance=1/fixed 8 ticks.\n"
                 "NORMALIZATION: image sentinels ignore actual theme choice; animation enum compares on/off. V4 either deliberately co-resolves still/animation side.\n"
                 "EXCLUDED: actual theme choice/RNG trace, text visibility/probability parity, media decoding, GPU pixels, theme refresh, generic V4 runtime.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
