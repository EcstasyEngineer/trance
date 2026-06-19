// Render-log equivalence: the compiled SLOW_FLASH (DSL + the `slow_flash` render
// preset) must emit the SAME render calls as the hardcoded SlowFlashVisual, frame
// for frame. This is the gate that lets a built-in be retired -- schedule + effects
// + render all proven identical. Runs headless: a fake VisualControl/VisualRender
// intercepts every call; no GL context, no window.
#include <trance/visual/api.h>
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/compiled_visual.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_parser.h>
#include <trance/visual/visual.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
  struct RenderCall {
    enum Method { AnimImg, Img, Text, Subtext, SmallSub, Spiral } method;
    int anim = 0;
    int img = 0;  // identity tag of the drawn image (0 = none), see FakeApi::get_image
    float a = 0, b = 0, c = 0, d = 0;
    bool operator==(const RenderCall& o) const
    {
      return method == o.method && anim == o.anim && img == o.img && a == o.a && b == o.b
          && c == o.c && d == o.d;
    }
  };

  // Implements both engine interfaces. Controls are no-ops returning deterministic
  // values; render calls are logged. The log is what the two visuals are compared on.
  class FakeApi : public VisualControl, public VisualRender
  {
  public:
    mutable std::vector<RenderCall> log;

    // VisualControl. get_image returns a tagged placeholder so the harness can tell the
    // primary slot (tag 1) from the alternate slot (tag 2) -- otherwise every image is
    // the same blank and routing the wrong slot/register to a draw would go unnoticed.
    // The drawn tag is captured in each render call below. (Both tags are truthy, which
    // also exercises the `if (!image)` paths the way real content does.)
    Image get_image(bool alternate) const override
    {
      return Image{alternate ? 2u : 1u, 1u, nullptr};
    }
    void maybe_upload_next() const override
    {
    }
    void rotate_spiral(float) override
    {
    }
    void change_spiral() override
    {
    }
    void change_animation(bool) override
    {
    }
    void change_font(bool) override
    {
    }
    void change_text(SplitType, bool) override
    {
    }
    void change_subtext(bool) override
    {
    }
    void change_small_subtext(bool, bool) override
    {
    }
    bool change_themes() override
    {
      return false;
    }

    // VisualRender
    void render_animation_or_image(Anim type, const Image& image, float alpha, float zoom_origin,
                                   float zoom) const override
    {
      log.push_back({RenderCall::AnimImg, int(type), int(image.width()), alpha, zoom_origin, zoom, 0});
    }
    void render_image(const Image& image, float alpha, float zoom_origin, float zoom) const override
    {
      log.push_back({RenderCall::Img, 0, int(image.width()), alpha, zoom_origin, zoom, 0});
    }
    void render_text(float zoom_origin, float zoom, float shadow_zoom_origin,
                     float shadow_zoom) const override
    {
      log.push_back({RenderCall::Text, 0, 0, zoom_origin, zoom, shadow_zoom_origin, shadow_zoom});
    }
    void render_subtext(float alpha, float zoom_origin) const override
    {
      log.push_back({RenderCall::Subtext, 0, 0, alpha, zoom_origin, 0, 0});
    }
    void render_small_subtext(float alpha, float zoom_origin) const override
    {
      log.push_back({RenderCall::SmallSub, 0, 0, alpha, zoom_origin, 0, 0});
    }
    void render_spiral() const override
    {
      log.push_back({RenderCall::Spiral, 0, 0, 0, 0, 0, 0});
    }
  };

  // Compare two render logs. With compare_anim=false the anim-type field is ignored:
  // for several visuals the ONLY render output that depends on (independently rolled)
  // randomness is the animation type passed to render_animation_or_image -- every call
  // still fires with the same schedule-driven float args. Ignoring just that field lets
  // the harness prove the structure + geometry frame-for-frame; the anim type itself is
  // then a (small) review item.
  bool logs_equal(const std::vector<RenderCall>& a, const std::vector<RenderCall>& b,
                  bool compare_anim)
  {
    if (a.size() != b.size()) {
      return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
      RenderCall x = a[i];
      RenderCall y = b[i];
      if (!compare_anim) {
        x.anim = 0;
        y.anim = 0;
      }
      if (!(x == y)) {
        return false;
      }
    }
    return true;
  }

  // Advance a hardcoded visual and the compiled built-in source side by side, and
  // require identical render calls every frame. The render math is deterministic
  // (it reads cycler state), so randomness in the effects doesn't affect the log.
  // compare_anim=false additionally tolerates a divergent animation type (see above).
  bool validate(const char* name, Visual& real, FakeApi& real_api, uint32_t visual_type,
                uint32_t frames, bool compare_anim = true)
  {
    FakeApi comp_api;
    pattern::ParseResult parsed = pattern::parse(builtin::pattern_source(visual_type));
    if (!parsed.ok) {
      std::cerr << name << " parse failed: " << parsed.error << "\n";
      return false;
    }
    CompiledVisual comp{comp_api, parsed.pattern.root, parsed.pattern.render};
    for (uint32_t f = 0; f < frames; ++f) {
      real.cycler()->advance(true);
      comp.cycler()->advance(true);
      real_api.log.clear();
      real.render(real_api);
      comp_api.log.clear();
      comp.render(comp_api);
      if (!logs_equal(real_api.log, comp_api.log, compare_anim)) {
        std::cerr << name << " render diverged at frame " << f << ": real emitted "
                  << real_api.log.size() << " calls, compiled " << comp_api.log.size() << "\n";
        return false;
      }
    }
    std::cout << "  " << name << ": render == hardcoded over " << frames << " frames"
              << (compare_anim ? "" : " (anim type review-validated)") << "\n";
    return true;
  }
}

int main()
{
  // SLOW_FLASH was validated here and then retired (its hardcoded class is gone); the
  // harness now tracks the still-hardcoded references. ANIMATION proves the compiled
  // built-in renders identically to AnimationVisual.
  bool ok = true;
  {
    FakeApi api;
    AnimationVisual real{api};
    ok = validate("ANIMATION", real, api, 7, 1024) && ok;
  }
  {
    // SUPER_FAST's render structure depends on its random FSM, so it can't be compared
    // frame-for-frame. Smoke test: the compiled built-in must actually drive its render
    // preset (emit image draws) rather than fall through to the spiral-only guard.
    FakeApi api;
    pattern::ParseResult parsed = pattern::parse(builtin::pattern_source(8));
    if (!parsed.ok) {
      std::cerr << "SUPER_FAST parse failed: " << parsed.error << "\n";
      ok = false;
    } else {
      CompiledVisual comp{api, parsed.pattern.root, parsed.pattern.render};
      bool drew_image = false;
      for (uint32_t f = 0; f < 1024; ++f) {
        comp.cycler()->advance(true);
        api.log.clear();
        comp.render(api);
        for (const auto& c : api.log) {
          drew_image = drew_image || c.method == RenderCall::AnimImg || c.method == RenderCall::Img;
        }
      }
      if (!drew_image) {
        std::cerr << "SUPER_FAST never drew an image (render preset degraded?)\n";
        ok = false;
      } else {
        std::cout << "  SUPER_FAST: compiled drives its render preset over 1024 frames\n";
      }
    }
  }
  if (!ok) {
    return 1;
  }
  std::cout << "PASS: compiled built-ins render == hardcoded\n";
  return 0;
}
