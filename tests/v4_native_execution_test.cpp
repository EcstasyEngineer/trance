// Direct production V4 parser/compiler/renderer behavior checks.
// No window, GL context, media decoding, flashing display, or wall-clock timing.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_parser_v4.h>
#include <trance/visual/builtin_patterns.h>
#include <common/util.h>
#include <algorithm>
#include <cmath>
#include <iostream>
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


struct Frame {
  std::vector<Draw> draws;
  unsigned picks, loads;
};
std::vector<Frame> trace(const std::string& source, unsigned frames)
{
  auto parsed = patternv4::parse(source);
  require(parsed.ok, parsed.error);
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
  // Parallel has two simultaneous image lanes with independent cadence and offset.
  const auto frames = trace(builtin::pattern_source_v4(5), 4225);
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
  const auto flash = trace(builtin::pattern_source_v4(4), 192);
  require(flash[64].draws.size() == 2 && flash[64].draws[1].alpha == 0.f && flash[96].draws[1].alpha == .5f,
          "Flash Text must retain its distinct offset triangular dissolve");
  const auto super = trace(builtin::pattern_source_v4(6), 192);
  require(super[64].draws.size() == 3 && super[64].draws[1].alpha == .5f && super[64].draws[2].alpha == .33f,
          "Super Parallel must retain three simultaneous image lanes");
  std::cout << "PASS restored Parallel two-lane cadence/offset/constant opacity/animation over 4225 frames, distinct Flash Text dissolve and Super Parallel triple\n";
}
}
int main()
{
  try {
    private_images();
    coherent_either();
    independent_animation_sides();
    restored_parallel();
    std::cout << "COVERAGE: production V4 parser -> compiled visual -> recording renderer.\n"
                 "EXCLUDED: media decoding, GPU pixels and wall-clock timing.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
