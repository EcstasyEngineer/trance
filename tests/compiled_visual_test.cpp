// Exercise production image effects and render-time recovery without a window.
#include <trance/visual/api.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message)
{
  if (!value) throw std::runtime_error(message);
}
Image picture(uint32_t width)
{
  std::vector<unsigned char> pixels(width * 4, 255);
  return Image(width, 1, pixels.data());
}

struct Api : VisualControl, VisualRender {
  Image current, fallback;
  uint32_t generation = 0;
  mutable unsigned refreshes = 0;
  bool first_pass = true;
  mutable std::vector<uint32_t> drawn;

  Image get_image(bool, bool* fresh) const override {
    if (fresh) *fresh = bool(current);
    return current ? current : fallback;
  }
  Image get_current_theme_image(bool) const override { ++refreshes; return current; }
  uint32_t lane_generation(bool) const override { return generation; }
  const std::string& get_theme_audio(bool) const override { static std::string s; return s; }
  void rotate_spiral(float) override {}
  void change_spiral() override {}
  void set_spiral(uint32_t, uint32_t) override {}
  void change_animation(bool) override {}
  void change_font(bool) override {}
  void change_text(SplitType, bool) override {}
  void change_subtext(bool) override {}
  void change_small_subtext(bool, bool) override {}
  bool change_themes() override { return false; }
  void play_theme_audio(const std::string&, bool) override {}
  void stop_theme_audio() override {}
  void set_theme_audio_volume(float) override {}
  void render_animation_or_image(Anim, const Image& image, float, float, float,
                                 ThemeSlot) const override { drawn.push_back(image.width()); }
  void render_image(const Image& image, float, float, float,
                    ThemeSlot) const override { drawn.push_back(image.width()); }
  void render_text(float, float, float, float) const override {}
  void render_subtext(float, float) const override {}
  void render_small_subtext(float, float) const override {}
  void render_spiral() const override {}
  void set_warp(float, float, float) override {}
  bool render_mutations_enabled() const override { return first_pass; }
  double render_mutation_frames() const override { return first_pass ? 1. : 0.; }
};

pattern::Node capture(bool snapshot)
{
  pattern::Node root;
  root.type = pattern::Node::Type::Action;
  root.length = 64;
  pattern::Effect image;
  image.kind = pattern::Effect::Kind::Image;
  image.slot = pattern::Slot::Primary;
  root.effects.push_back(image);
  if (snapshot) {
    pattern::Effect copy;
    copy.kind = pattern::Effect::Kind::Copy;
    copy.src = "current";
    copy.target = "previous";
    root.effects.push_back(copy);
  }
  return root;
}
std::vector<pattern::RenderStmt> draws(bool snapshot)
{
  std::vector<pattern::RenderStmt> result(1);
  if (snapshot) {
    pattern::RenderStmt previous;
    previous.image_reg = "previous";
    result.push_back(previous);
  }
  return result;
}
void expect(CompiledVisual& visual, Api& api, std::vector<uint32_t> widths)
{
  api.drawn.clear();
  visual.render(api);
  require(api.drawn == widths, "unexpected captured images on render");
}

void initial_empty_recovers()
{
  Api api;
  CompiledVisual visual(api, capture(false), draws(false));
  visual.cycler()->advance();
  expect(visual, api, {0});
  api.current = picture(2);
  // No theme swap and no further action firing: async readiness alone repairs it.
  expect(visual, api, {2});
  const unsigned calls = api.refreshes;
  api.current = picture(3);
  expect(visual, api, {2});
  require(api.refreshes == calls, "a valid capture must not reshuffle every frame");
}
void fallback_retries_and_snapshot_stays()
{
  Api api;
  api.fallback = picture(1);
  CompiledVisual visual(api, capture(true), draws(true));
  visual.cycler()->advance();
  expect(visual, api, {1, 1});
  api.current = picture(2);
  const unsigned calls = api.refreshes;
  api.first_pass = false;
  expect(visual, api, {1, 1});
  require(api.refreshes == calls, "later stereo passes must not refresh registers");
  api.first_pass = true;
  expect(visual, api, {2, 1});

  ++api.generation;
  api.current = {};
  expect(visual, api, {2, 1});
  api.current = picture(3);
  expect(visual, api, {3, 1});
}
void empty_reselection_preserves_drawable_capture()
{
  Api api;
  api.current = picture(2);
  CompiledVisual visual(api, capture(false), draws(false));
  visual.cycler()->advance();
  expect(visual, api, {2});
  api.current = {};
  for (unsigned i = 0; i < 64; ++i) visual.cycler()->advance();
  expect(visual, api, {2});
  api.current = picture(3);
  expect(visual, api, {3});
}
}

int main()
{
  try {
    initial_empty_recovers();
    fallback_retries_and_snapshot_stays();
    empty_reselection_preserves_drawable_capture();
    std::cout << "compiled image recovery checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
