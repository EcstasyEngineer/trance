#include "support/legacy_builtins.h"
// Leapfrog source-over composition through real parser/effects/render dispatch.
// No window, GL context, media decoding, flashing display, or wall-clock timing.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include "support/pattern_parser_v3.h"
#include <trance/visual/builtin_patterns.h>
#include <functional>
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

using Contributions = std::map<unsigned, double>;
Contributions compose(const std::vector<Draw>& draws)
{
  Contributions result;
  // Uniform opaque sentinel images; image 0 is empty, never an opaque source.
  // This models one covered pixel, not projection/texture/color validation.
  for (const auto& draw : draws) {
    if (!draw.width) continue;
    const double alpha = std::clamp(double(draw.alpha), 0., 1.);
    for (auto& item : result) item.second *= 1. - alpha;
    result[draw.width] += alpha;
  }
  for (auto it = result.begin(); it != result.end();) {
    if (std::abs(it->second) < 0.000001) it = result.erase(it);
    else ++it;
  }
  return result;
}
void near(double actual, double expected, const std::string& message)
{
  require(std::abs(actual - expected) < .00001, message + ": actual=" + std::to_string(actual));
}
double coverage(const Contributions& c)
{
  double result = 0;
  for (const auto& entry : c) result += entry.second;
  return result;
}
struct Frame {
  std::vector<Draw> draws;
  unsigned picks;
};
std::vector<Frame> trace(const std::string& source, unsigned frames, unsigned reset_at = 0)
{
  const auto parsed = patternv3::parse(source);
  require(parsed.ok, parsed.error);
  Api api;
  CompiledVisual visual(api, parsed.root, parsed.render_block);
  std::vector<Frame> result;
  for (unsigned frame = 0; frame < frames; ++frame) {
    if (frame && frame == reset_at) visual.cycler()->reset();
    visual.cycler()->advance();
    api.drawn.clear(); visual.render(api);
    result.push_back({api.drawn, api.picks});
    const unsigned picks = api.picks, loads = api.loads;
    api.first_pass = false; api.drawn.clear(); visual.render(api);
    require(api.drawn == result.back().draws && picks == api.picks && loads == api.loads,
            "second output pass changes state or render values");
    api.first_pass = true;
    for (const auto& draw : api.drawn)
      require(draw.animation == VisualRender::Anim::NONE,
              "these examples establish still captures only, never animation snapshots");
  }
  return result;
}
std::string lanes(bool symmetric, unsigned span = 1024)
{
  return "pattern scene for " + std::to_string(span) + "f {"
    "pattern back for " + std::to_string(span) + "f {every 128f {image secondary "
    + std::string(symmetric ? "fade inout " : "") + "zoom (curve 0 -> 0.8)}}"
    "pattern front for " + std::to_string(span) + "f {"
    "every 128f offset 64f {image secondary fade inout zoom (curve 0 -> 0.8)}} }";
}
// Independent historical oracle: the former copy/draw recipe selected every 64f,
// painted an opaque outgoing snapshot above a 0.4 zoom baseline, then faded the
// incoming image from 0..1 while zooming 0..0.4. Keep this independent of the parser
// and the current builtin so deleting authored storage cannot alter the reference.
std::vector<Frame> historical_flash_text(unsigned frames)
{
  std::vector<Frame> result;
  for (unsigned frame = 0; frame < frames; ++frame) {
    const unsigned picks = 1 + frame / 64;
    const float progress = float(frame % 64) / 64.f;
    result.push_back({{{picks > 1 ? 9 + picks : 0, 1.f, .4f + .4f * progress,
                       VisualRender::Anim::NONE},
                      {10 + picks, progress, .4f * progress, VisualRender::Anim::NONE}}, picks});
  }
  return result;
}

void compare_lanes()
{
  const auto original = historical_flash_text(2050);
  const auto asymmetric = trace(lanes(false), 2050);
  const auto symmetric = trace(lanes(true), 2050);
  const auto scoped = trace("pattern scene for 1024f {"
    "pattern back for 1024f {every 128f {image secondary zoom (curve 0 -> 0.8)}}"
    "pattern front for 1024f {every 128f offset 64f {image secondary fade inout zoom (curve 0 -> 0.8)}}}",2050);
  const auto builtin_flash = trace(builtin::pattern_source_v3(4), 2050);
  unsigned mismatches = 0;
  for (unsigned frame = 0; frame < 2050; ++frame) {
    require(scoped[frame].draws == asymmetric[frame].draws
            && scoped[frame].picks == asymmetric[frame].picks,
            "independent lane pattern scopes produce identical execution");
    require(builtin_flash[frame].draws == asymmetric[frame].draws
            && builtin_flash[frame].picks == asymmetric[frame].picks,
            "actual Flash Text builtin follows independent lanes");
    const auto old = compose(original[frame].draws);
    const auto revised = compose(asymmetric[frame].draws);
    // Selection order is 11,12,... in both recipes. Compare actual identity
    // contributions, not alpha sums or a frozen render-statement shape.
    if (old != revised) {
      bool same = old.size() == revised.size();
      for (const auto& item : old) {
        auto it = revised.find(item.first);
        if (it == revised.end() || std::abs(it->second-item.second) > .00001) same = false;
      }
      if (!same) ++mismatches;
    }
    if (frame >= 64) {
      near(coverage(revised), 1, "asymmetric steady coverage");
      require(old.size() == revised.size(), "same contributing image count after startup");
      for (const auto& item : old) near(revised.at(item.first), item.second, "steady identity weights");
    }
    const unsigned expected = 1 + frame / 64;
    require(original[frame].picks == expected && asymmetric[frame].picks == expected,
            "one selection each64 including scene restart");
    // Original image age/zoom follows selected identity, independently of draw order.
    if (frame >= 64) for (const auto& draw : asymmetric[frame].draws) {
      if (!draw.width) continue;
      auto it = revised.find(draw.width);
      if (it == revised.end()) continue;
      bool matched = false;
      for (const auto& old_draw : original[frame].draws)
        if (old_draw.width == draw.width) {near(draw.zoom, old_draw.zoom, "held image zoom"); matched = true;}
      require(matched, "same image identity exists in old render");
    }
  }
  require(mismatches == 64, "only startup differs over two complete1024-frame scenes");
  near(coverage(compose(symmetric[96].draws)), .75, "symmetric fades expose quarter background");
  near(coverage(compose(asymmetric[0].draws)), 1, "new opaque first image");
  near(coverage(compose(original[0].draws)), 0, "old initial empty outgoing plus zero-alpha incoming");
  std::cout << "original vs asymmetric differences=" << mismatches
            << "; identical-fade coverage at96=" << coverage(compose(symmetric[96].draws)) << '\n';
  for (auto at : {0u,32u,63u,64u,96u,127u,128u,1023u,1024u,1056u,1088u}) {
    std::cout << "frame=" << at << " old=";
    for (auto item : compose(original[at].draws)) std::cout << item.first << ':' << item.second << ',';
    std::cout << " asymmetric=";
    for (auto item : compose(asymmetric[at].draws)) std::cout << item.first << ':' << item.second << ',';
    std::cout << '\n';
  }
}
void lifetime_edges()
{
  for (unsigned span : {1024u,1000u,70u}) {
    const auto frames = trace(lanes(false, span), span * 2 + 2);
    const unsigned first_picks = (span + 63) / 64;
    require(frames[span-1].picks == first_picks, "partial final selection count");
    require(frames[span].picks == first_picks+1, "owner repeat resets cadence");
    require(frames[span].draws.size() == 2, "both lane render statements remain active");
    near(frames[span].draws[1].alpha, 1, "offset top begins at half-phase on owner reentry");
    require(frames[span].draws[1].width != 0, "offset top retains stale selection on owner reentry");
    near(coverage(compose(frames[span].draws)),1,"held top covers opaque bottom restart");
    std::cout << "owner=" << span << " reentry top identity=" << frames[span].draws[1].width
              << " bottom=" << frames[span].draws[0].width << '\n';
  }
  const auto reset = trace(lanes(false), 202, 200);
  near(reset[200].draws[1].alpha,1,"explicit reset also retains top while resetting clock");
  require(reset[200].draws[1].width == reset[199].draws[1].width,"reset is not state erasure");
  const auto collision = trace("pattern p for 256f {every 128f {image secondary}"
                               "every 128f offset 64f {image secondary fade inout}}",129);
  require(collision[64].draws[0].width == collision[64].draws[1].width,
          "cadence is not a register scope: two omitted destinations alias");
  const auto branch = trace("pattern scene for 1024f {"
    "burst period 8f chance 1/1 cooldown 64f duration 200f {base {} burst {"
    "pattern back for 200f {every 128f {image secondary zoom (curve 0 -> 0.8)}}"
    "pattern front for 200f {every 128f offset 64f {image secondary fade inout zoom (curve 0 -> 0.8)}}"
    "}}}",266);
  require(branch[200].draws.empty() && branch[263].draws.empty(),
          "inactive branch must hide both draws");
  require(branch[264].draws.size() == 2 && branch[264].draws[1].width == branch[199].draws[1].width,
          "burst reentry resets phase but retains old upper capture");
  near(branch[264].draws[1].alpha,1,"burst reentry upper is fully opaque immediately");
}
}
int main()
{
  try {
    for (const auto& legacy : {"image primary -> saved", "draw cur", "copy cur -> previous"}) {
      const auto result = patternv3::parse(std::string("pattern p for 16f {") + legacy + "}");
      require(!result.ok, "removed named storage syntax must fail: " + std::string(legacy));
    }
    compare_lanes(); lifetime_edges(); return 0;
  }
  catch (const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}

