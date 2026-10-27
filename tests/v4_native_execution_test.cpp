#include "support/legacy_builtins.h"
// Direct native V4 parser/compiler/renderer verification against the retained V3 oracle.
// No window, GL context, media decoding, flashing display, or wall-clock timing.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include "support/pattern_parser_v3.h"
#include <trance/visual/pattern_parser_v4.h>
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
  mutable std::vector<bool> picked_sides;
  std::vector<bool> loaded_sides;
  unsigned loads = 0;
  bool first_pass = true;
  mutable std::vector<Draw> drawn;
  Image get_image(bool alternate, bool* fresh) const override {
    picked_sides.push_back(alternate);
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
  void change_animation(bool alternate) override { ++loads; loaded_sides.push_back(alternate); }
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
  if (node.type == pattern::Node::Type::Occurrence && node.occurrence_kind == pattern::Node::Composition::Random) {
    for (auto& exit : node.child_exits) if (exit.enabled) exit.numerator = exit.denominator = 1;
    if (node.children.size() > 1) node.children[1].length = node.children[1].duration_max = 64; // V4 uses frames; V3 used eight 8-frame ticks.
  }
  for (auto& child : node.children) deterministic_burst(child);
}
std::vector<Frame> trace(const std::string& source, unsigned frames, bool native_v4 = false)
{
  auto parsed = native_v4 ? patternv4::parse(source) : patternv3::parse(source);
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
  std::ifstream input(directory / (name + ".json"));
  require(bool(input), "missing generated source " + name);
  std::ostringstream source; source << input.rdbuf();
  const unsigned frames = 2 * period + 129; // both root loops and all 32/48/64f offsets
  const auto builtin_trace = trace(builtin::pattern_source_v3(visual_id), frames);
  const auto v4_trace = trace(source.str(), frames, true);
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

void private_images()
{
  const auto parsed = patternv4::parse(R"({"version":4,"root":{"kind":"parallel","duration":16,"children":[{"node":{"op":"image","source":"primary"}},{"node":{"op":"image","source":"secondary"}}]}})");
  require(parsed.ok, parsed.error);
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (unsigned f=0; f<16; ++f) {
    visual.cycler()->advance(); api.drawn.clear(); visual.render(api);
    require(api.picks == 2, "direct parallel images select more than once");
    require(api.drawn.size() == 2, "direct parallel images missing");
    require(api.drawn[0].width == 11 && api.drawn[1].width == 12,
            "direct parallel images share/overwrite implicit image storage");
  }
  std::cout << "PASS private image ownership for two direct children of one parallel occurrence\n";
}
void coherent_either()
{
  const auto parsed = patternv4::parse(R"({"version":4,"root":{"kind":"parallel","duration":256,"cadence":1,"children":[{"node":{"op":"image","source":"either","animation":{"every":1}}}]}})");
  require(parsed.ok, parsed.error);
  get_mersenne_twister().seed(1934);
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (unsigned f=0; f<256; ++f) {
    visual.cycler()->advance(); api.drawn.clear(); visual.render(api);
    require(api.picked_sides.size() == f+1 && api.loaded_sides.size() == f+1,
            "either cadence did not select/load exactly once");
    require(api.picked_sides.back() == api.loaded_sides.back(),
            "either still and animation independently choose different theme sides");
    require(api.drawn.size() == 1, "either image missing");
    const auto expected = api.loaded_sides.back() ? VisualRender::Anim::ANIM_ALTERNATE : VisualRender::Anim::ANIM;
    require(api.drawn[0].animation == expected, "either draw side differs from loaded animation");
    api.first_pass=false; api.drawn.clear(); visual.render(api); api.first_pass=true;
    require(api.picked_sides.size() == f+1 && api.loaded_sides.size() == f+1,
            "second output pass mutates either selection");
  }
  require(std::find(api.picked_sides.begin(),api.picked_sides.end(),true) != api.picked_sides.end()
          && std::find(api.picked_sides.begin(),api.picked_sides.end(),false) != api.picked_sides.end(),
          "either test did not exercise both sides");
  std::cout << "PASS coherent either still/load/draw side for 256 selections and immutable second pass\n";
  std::cout << "MEDIA LIMIT: animation has primary/secondary streamers; simultaneous same-side outputs still share a streamer. Text/audio remain shared.\n";
}

void independent_animation_sides()
{
  const auto parsed = patternv4::parse(R"({"version":4,"root":{"kind":"parallel","duration":16,"children":[{"node":{"op":"image","source":"primary","animation":{"every":1}}},{"node":{"op":"image","source":"secondary","animation":{"every":1}}}]}})");
  require(parsed.ok, parsed.error);
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  for (unsigned f=0; f<16; ++f) {
    visual.cycler()->advance(); api.drawn.clear(); visual.render(api);
    require(api.loaded_sides == std::vector<bool>({false, true}), "parallel animation loads must target both theme streamers");
    require(api.drawn.size() == 2 && api.drawn[0].width == 11 && api.drawn[1].width == 12,
            "parallel animated outputs lost private still captures");
    require(api.drawn[0].animation == VisualRender::Anim::ANIM &&
            api.drawn[1].animation == VisualRender::Anim::ANIM_ALTERNATE,
            "later sibling animation load redirected an earlier output");
    const auto first = api.drawn;
    api.first_pass=false; api.drawn.clear(); visual.render(api); api.first_pass=true;
    require(api.drawn == first && api.loads == 2 && api.picks == 2,
            "second pass changed parallel animation selection or drawing");
  }
  std::cout << "PASS simultaneous primary/secondary animation ownership and immutable second pass\n";
}

void restored_parallel()
{
  // #5 intentionally restores the historical two-image design rather than
  // preserving the later one-image Simple regression used by the V3 oracle.
  const auto frames = trace(builtin::pattern_source_v4(5), 4225, true);
  for (unsigned f=0; f<frames.size(); ++f) {
    const unsigned local = f % 2048;
    const auto& draws = frames[f].draws;
    require(draws.size() == 2, "Parallel must schedule two simultaneous image outputs");
    require((draws[1].width != 0) == (f >= 32), "Parallel secondary capture must begin at its first offset");
    require(draws[0].alpha == 1.f && std::abs(draws[0].zoom - float(local % 64)/128.f) < .00001f,
            "Parallel primary cadence/opacity/zoom changed");
    require((draws[0].animation != VisualRender::Anim::NONE) == (((f/64+1)%3)==0),
            "Parallel primary animation accent changed");
    if (f >= 32) {
      require(draws[1].alpha == .5f, "Parallel secondary must retain constant half opacity, not dissolve");
      // At owner restart, the old secondary capture stays drawable while its
      // phase-shifted clock resumes; selection next occurs at offset 32.
      const unsigned offset_local = (local+32)%64;
      require(std::abs(draws[1].zoom - float(offset_local)/128.f) < .00001f,
              "Parallel secondary offset zoom changed");
      if (draws[0].animation != VisualRender::Anim::NONE)
        require(draws[0].animation == VisualRender::Anim::ANIM, "Parallel primary animation uses wrong streamer");
      if (draws[1].animation != VisualRender::Anim::NONE)
        require(draws[1].animation == VisualRender::Anim::ANIM_ALTERNATE, "Parallel secondary animation uses wrong streamer");
    }
  }
  const auto flash = trace(builtin::pattern_source_v4(4), 192, true);
  require(flash[64].draws.size() == 2 && flash[64].draws[1].alpha == 0.f && flash[96].draws[1].alpha == .5f,
          "Flash Text must retain its distinct offset triangular dissolve");
  const auto super = trace(builtin::pattern_source_v4(6), 192, true);
  require(super[64].draws.size() == 3 && super[64].draws[1].alpha == .5f && super[64].draws[2].alpha == .33f,
          "Super Parallel must retain three simultaneous image lanes");
  std::cout << "PASS restored Parallel two-lane cadence/offset/constant opacity/animation over 4225 frames, distinct Flash Text dissolve and Super Parallel triple\n";
}
}
int main(int argc, char** argv)
{
  try {
    require(argc == 2, "usage: v4_native_execution_test V4_JSON_DIRECTORY");
    const char* names[] = {"accelerate", "slow_flash", "sub_text", "flash_text", "simple", "super_parallel", "animation", "super_fast"};
    const unsigned spans[] = {2048, 3072, 1024, 1024, 2048, 1152, 1024, 2048};
    for (unsigned i = 0; i < 8; ++i) if (i != 4) compare(names[i], i+1, spans[i], argv[1]);
    private_images();
    coherent_either();
    independent_animation_sides();
    restored_parallel();
    std::cout << "COVERAGE: actual parser -> compiled visual -> recording renderer; Super Fast chance=1/fixed 8 ticks.\n"
                 "NORMALIZATION: image sentinels ignore actual theme choice; animation enum compares on/off. V4 either deliberately co-resolves still/animation side.\n"
                 "EXCLUDED: actual legacy RNG trace, text visibility/probability parity, media decoding, GPU pixels and theme refresh.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
