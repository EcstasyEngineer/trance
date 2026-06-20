#include <trance/visual/render_preset.h>
#include <trance/visual/api.h>
#include <trance/visual/cyclers.h>
#include <algorithm>

namespace
{
  const Cycler* node(const pattern::NodeMap& nodes, const char* id)
  {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : it->second;
  }
  Image reg(const pattern::Registers& regs, const char* name)
  {
    auto it = regs.images.find(name);
    return it == regs.images.end() ? Image{} : it->second;
  }
  int32_t regi(const pattern::Registers& regs, const char* name)
  {
    auto it = regs.scalars.find(name);
    return it == regs.scalars.end() ? 0 : it->second;
  }

  // Default: draw the "current" image with a progress-driven zoom, the spiral, and
  // any current text. Used when a pattern names no (or an unknown) preset.
  void render_default(VisualRender& api, const pattern::Registers& regs, const pattern::NodeMap&,
                      const Cycler* root)
  {
    float progress = root ? root->progress() : 0.f;
    Image current = reg(regs, "current");
    if (current) {
      api.render_animation_or_image(VisualRender::Anim::NONE, current, 1.f, 0.f, 0.5f * progress);
    }
    api.render_spiral();
    api.render_text(0.75f, 0.75f, 0.5f * progress, 0.5f * progress);
  }

  // SLOW_FLASH render: reads cycler state by node id (slow_loop/slow_main/slow_repeat/
  // fast_* set in kSlowFlash) and the "current" image register, then emits the draw calls.
  void render_slow_flash(VisualRender& api, const pattern::Registers& regs,
                         const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* slow_loop = node(nodes, "slow_loop");
    const Cycler* slow_main = node(nodes, "slow_main");
    const Cycler* slow_repeat = node(nodes, "slow_repeat");
    const Cycler* fast_repeat = node(nodes, "fast_repeat");
    const Cycler* fast_loop = node(nodes, "fast_loop");
    const Cycler* fast_cycler = node(nodes, "fast_cycler");
    const Cycler* fast_text = node(nodes, "fast_text");
    const Cycler* fast_main = node(nodes, "fast_main");
    if (!slow_loop || !slow_main || !slow_repeat || !fast_repeat || !fast_loop || !fast_cycler
        || !fast_text || !fast_main) {
      api.render_spiral();
      return;
    }
    Image current = reg(regs, "current");

    auto zoom_origin =
        slow_loop->active() ? .25f * slow_main->progress() : fast_repeat->index() / 48.f;
    auto zoom = slow_loop->active() ? .25f * slow_main->progress() + .5f * slow_loop->progress()
                                    : (fast_repeat->index() + 8.f * fast_loop->progress()) / 48.f;
    api.render_animation_or_image(slow_loop->active() && slow_repeat->index() % 2
                                      ? VisualRender::Anim::ANIM
                                      : VisualRender::Anim::NONE,
                                  current, 1, zoom_origin, zoom);
    api.render_spiral();
    if (fast_loop->active()
        || (slow_loop->active() && slow_loop->frame() < slow_loop->length() / 2)) {
      api.render_small_subtext(1.f / 5, .5f);
    }
    if (slow_loop->active() && slow_loop->frame() >= slow_loop->length() / 2) {
      api.render_text(.8f, .8f, zoom, zoom);
    }
    if (fast_cycler->active() && fast_text->frame() >= fast_text->length() / 2) {
      api.render_text(7.f / 8, 1.f - fast_main->progress() / 8.f, 7.f / 8,
                      1.f - fast_main->progress() / 8.f);
    }
  }
  // FLASH_TEXT render. The `animated` flag and `alt` toggle come from scalar registers;
  // start/end from image registers; image/image_repeat/subtext_counter by node id.
  void render_flash_text(VisualRender& api, const pattern::Registers& regs,
                         const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* image = node(nodes, "image");
    const Cycler* image_repeat = node(nodes, "image_repeat");
    const Cycler* subtext_counter = node(nodes, "subtext_counter");
    if (!image || !image_repeat || !subtext_counter) {
      api.render_spiral();
      return;
    }
    bool animated = regi(regs, "animated") != 0;
    bool alt = regi(regs, "alt") != 0;
    Image start = reg(regs, "start");
    Image end = reg(regs, "end");
    auto progress = image->progress();
    auto anim = alt ? VisualRender::Anim::ANIM_ALTERNATE : VisualRender::Anim::ANIM;
    api.render_animation_or_image(
        !animated || !image_repeat->index() ? VisualRender::Anim::NONE : anim, start, 1.f, 0,
        .4f * (1 + progress));
    api.render_animation_or_image(
        !animated || image_repeat->index() ? VisualRender::Anim::NONE : anim, end,
        image->progress(), 0, .4f * progress);
    api.render_spiral();
    if (subtext_counter->index()) {
      api.render_small_subtext(1.f / 5, .25f);
    }
    if (image_repeat->index()) {
      api.render_text(.85f - .05f * progress, .9f - .1f * progress, .75f, .8f - .05f * progress);
    }
  }

  // ACCELERATE render. The ramp is one SequenceCycler ("ramp"); the active segment is
  // ramp->children()[index()], whose image leaf (the image-annotated lane) and text leaf
  // the render reads for progress/active. The anim type/alternate and text_on come from
  // scalar registers. Each segment compiles to
  // `repeat N one { par { image, spiral, upload-or-timer } text }`, so the active image
  // leaf is par.children()[0] and the text leaf is oneshot.children()[1].
  void render_accelerate(VisualRender& api, const pattern::Registers& regs,
                         const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* ramp = node(nodes, "ramp");
    if (!ramp) {
      api.render_spiral();
      return;
    }
    auto segments = ramp->children();
    uint32_t idx = ramp->index();
    if (idx >= segments.size()) {
      api.render_spiral();
      return;
    }
    auto seg = segments[idx]->children();          // RepeatCycler -> [OneShot]
    if (seg.empty()) {
      api.render_spiral();
      return;
    }
    auto oneshot = seg[0]->children();             // OneShot -> [par, text]
    if (oneshot.size() < 2) {
      api.render_spiral();
      return;
    }
    auto par = oneshot[0]->children();             // Par -> [image, spiral, upload]
    if (par.empty()) {
      api.render_spiral();
      return;
    }
    const Cycler* image_leaf = par[0];
    const Cycler* text_leaf = oneshot[1];

    bool animation_on = regi(regs, "animation_on") != 0;
    bool animation_alt = regi(regs, "animation_alt") != 0;
    bool text_on = regi(regs, "text_on") != 0;
    auto zoom_origin = .4f * ramp->progress();
    auto zoom = zoom_origin + .1f * image_leaf->progress();
    api.render_animation_or_image(!animation_on ? VisualRender::Anim::NONE
                                                : animation_alt ? VisualRender::Anim::ANIM_ALTERNATE
                                                                : VisualRender::Anim::ANIM,
                                  reg(regs, "current"), 1.f, zoom_origin, zoom);
    api.render_spiral();
    if (text_on && text_leaf->active()) {
      api.render_text(.6f + .2f * ramp->progress(), .6f + .2f * ramp->progress(), zoom, zoom);
    }
  }

  // SUB_TEXT render. `animation_on` and the `alt` toggle come from scalar registers;
  // the image from the "current" register; image leaf by node id.
  void render_sub_text(VisualRender& api, const pattern::Registers& regs,
                       const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* image = node(nodes, "image");
    if (!image) {
      api.render_spiral();
      return;
    }
    bool animation_on = regi(regs, "animation_on") != 0;
    bool alt = regi(regs, "alt") != 0;
    auto image_zoom = .375f * image->progress();
    api.render_animation_or_image(!animation_on ? VisualRender::Anim::NONE
                                                : alt ? VisualRender::Anim::ANIM_ALTERNATE
                                                      : VisualRender::Anim::ANIM,
                                  reg(regs, "current"), 1.f, 0, image_zoom);
    api.render_subtext(1.f / 4, image_zoom);
    api.render_spiral();
    api.render_text(.75f, .75f, image_zoom, image_zoom);
  }

  // SIMPLE render. The animation shows on every third image, gated by the `anim_on`
  // pulse flag (set by `pulse simple_counter every 3` in kSimple).
  void render_simple(VisualRender& api, const pattern::Registers& regs,
                     const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* image = node(nodes, "image");
    const Cycler* counter = node(nodes, "counter");
    if (!image || !counter) {
      api.render_spiral();
      return;
    }
    auto anim = regi(regs, "anim_on") ? VisualRender::Anim::ANIM : VisualRender::Anim::NONE;
    api.render_animation_or_image(anim, reg(regs, "current"), 1, 0, .5f * image->progress());
    api.render_spiral();
    api.render_small_subtext(1.f / 5, .25f);
    if (counter->index() == 1 || counter->index() == 2) {
      api.render_text(.75f, .75f, .5f * image->progress(), .5f * image->progress());
    }
  }

  // SUPER_PARALLEL render. Three offset image lanes read by id (prog0..2 progress,
  // single0..2 active), the alt-animation toggle from a scalar register, and the three
  // image registers img0..2.
  void render_super_parallel(VisualRender& api, const pattern::Registers& regs,
                             const pattern::NodeMap& nodes, const Cycler* root)
  {
    const Cycler* prog[3] = {node(nodes, "prog0"), node(nodes, "prog1"), node(nodes, "prog2")};
    const Cycler* single[3] = {node(nodes, "single0"), node(nodes, "single1"),
                               node(nodes, "single2")};
    const Cycler* text = node(nodes, "text");
    if (!prog[0] || !prog[1] || !prog[2] || !single[0] || !single[1] || !single[2] || !text
        || !root) {
      api.render_spiral();
      return;
    }
    bool alt_anim = regi(regs, "alt_anim") != 0;
    Image images[3] = {reg(regs, "img0"), reg(regs, "img1"), reg(regs, "img2")};
    bool is_single =
        single[0]->active() || single[1]->active() || single[2]->active();
    for (std::size_t i = 0; i < 3; ++i) {
      auto anim = i != 0 ? VisualRender::Anim::NONE
                         : alt_anim ? VisualRender::Anim::ANIM_ALTERNATE : VisualRender::Anim::ANIM;
      if (!is_single || single[i]->active()) {
        auto zoom_origin = .125f * root->progress();
        api.render_animation_or_image(anim, images[i], is_single ? 1.f : 1.f / (1 + i), zoom_origin,
                                      zoom_origin + .875f * prog[i]->progress());
      }
    }
    api.render_spiral();
    if (text->frame() < text->length() / 2) {
      api.render_text(.875f, .875f, .75f, .75f);
    }
  }

  // SUPER_FAST render. The FSM state, animation timer, text_mod and alternate come from
  // the sf_* scalar registers (written by the super_fast_tick effect); current/next from
  // image registers; the rapid leaf supplies frame()/length()/progress().
  // States: 0 RAPID, 1 START, 2 ANIMATION, 3 END.
  void render_super_fast(VisualRender& api, const pattern::Registers& regs,
                         const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* rapid = node(nodes, "rapid");
    if (!rapid) {
      api.render_spiral();
      return;
    }
    int32_t state = regi(regs, "sf_state");
    int32_t atimer = regi(regs, "sf_anim_timer");
    int32_t tmod = regi(regs, "sf_text_mod");
    bool alt = regi(regs, "sf_alternate") != 0;
    Image current = reg(regs, "current");
    Image next = reg(regs, "next");

    auto anim_progress = float(8 * (16 - atimer) + static_cast<int32_t>(rapid->frame())) / 128;
    auto next_alpha =
        (5 - static_cast<int32_t>(rapid->length()) + static_cast<int32_t>(rapid->frame())) / 5.f;
    auto image_zoom = .125f * (.5f + rapid->progress());
    auto next_zoom = .125f * (rapid->progress() - .5f);
    auto anim = alt ? VisualRender::Anim::ANIM_ALTERNATE : VisualRender::Anim::ANIM;
    if (state == 2 || state == 3) {
      api.render_animation_or_image(anim, current, 1.f, 0.f, anim_progress);
    } else {
      api.render_image(current, 1.f, 0.f, image_zoom);
    }
    if (rapid->frame() >= rapid->length() - 4) {
      if (state == 1) {
        api.render_animation_or_image(anim, {}, next_alpha, 0.f, anim_progress);
      } else if (state == 0 || state == 3) {
        api.render_image(next, next_alpha, next_zoom, next_zoom);
      }
    }
    if (state == 0 && !tmod) {
      api.render_text(.75f, .75f, image_zoom, image_zoom);
    }
    api.render_spiral();
  }

  // ANIMATION render. Reads change_alt/change_counter/start_end_timer by id and the
  // backup/current image registers.
  void render_animation(VisualRender& api, const pattern::Registers& regs,
                        const pattern::NodeMap& nodes, const Cycler*)
  {
    const Cycler* change_alt = node(nodes, "change_alt");
    const Cycler* change_counter = node(nodes, "change_counter");
    const Cycler* start_end = node(nodes, "start_end_timer");
    if (!change_alt || !change_counter || !start_end) {
      api.render_spiral();
      return;
    }
    Image backup = reg(regs, "backup");
    Image current = reg(regs, "current");

    auto which_anim =
        change_alt->active() ? VisualRender::Anim::ANIM_ALTERNATE : VisualRender::Anim::ANIM;
    auto image_zoom = .625f * change_counter->progress();
    api.render_animation_or_image(which_anim, backup, 1.f, 0, image_zoom);
    if (change_counter->frame() < 16 && start_end->index() == 1) {
      auto t = 15 - change_counter->frame();
      api.render_image(current, std::min(1.f, t / 16.f), .5f,
                       .625f + .125f * change_counter->frame() / 16.f);
    }
    if (change_counter->frame() >= 48 && start_end->index() == 1) {
      auto t = change_counter->frame() - 48;
      api.render_image(current, std::min(1.f, t / 16.f), .5f, .5f + .125f * t / 16.f);
    }
    api.render_spiral();
    api.render_small_subtext(1.f / 5, .5f);
    if (!change_counter->index()) {
      api.render_text(.75f, .75f, .5f, .5f);
    }
  }
}

namespace pattern
{
  RenderFn render_preset(const std::string& name)
  {
    if (name == "slow_flash") {
      return render_slow_flash;
    }
    if (name == "animation") {
      return render_animation;
    }
    if (name == "super_parallel") {
      return render_super_parallel;
    }
    if (name == "simple") {
      return render_simple;
    }
    if (name == "sub_text") {
      return render_sub_text;
    }
    if (name == "flash_text") {
      return render_flash_text;
    }
    if (name == "accelerate") {
      return render_accelerate;
    }
    if (name == "super_fast") {
      return render_super_fast;
    }
    return render_default;
  }
}
