#include <trance/visual/visual.h>
#include <common/util.h>
#include <trance/director.h>
#include <trance/visual/api.h>
#include <trance/visual/cyclers.h>
#include <algorithm>

void Visual::set_cycler(Cycler* cycler)
{
  _cycler.reset(cycler);
}

void Visual::set_render(const std::function<void(VisualRender& api)>& function)
{
  _render = function;
}

Cycler* Visual::cycler()
{
  return _cycler.get();
}

const Cycler* Visual::cycler() const
{
  return _cycler.get();
}

void Visual::render(VisualRender& api) const
{
  _render(api);
}

AnimationVisual::AnimationVisual(VisualControl& api)
{
  auto image_timer = new ActionCycler{16};
  auto image = new ActionCycler{32, [&] { _animation_backup = api.get_image(); }};
  image->set_image_slot(ImageSlotHint::Primary);
  auto image_alt = new ActionCycler{64, 32, [&] { _current = api.get_image(true); }};
  image_alt->set_image_slot(ImageSlotHint::Alternate, "img_alt");

  auto change = new ActionCycler{64, 0, [&] {
                                   api.change_text(VisualControl::SPLIT_LINE);
                                   api.change_animation(false);
                                 }};
  auto change_alt = new ActionCycler{64, 0, [&] {
                                       api.change_text(VisualControl::SPLIT_LINE, true);
                                       api.change_animation(true);
                                     }};
  auto change_both = new SequenceCycler{{change, change_alt}};
  auto half_counter = new ActionCycler{32};
  auto change_counter = new RepeatCycler{2, half_counter};

  auto spiral = new ActionCycler{[&] { api.rotate_spiral(3.5f); }};
  auto small_subtext =
      new ActionCycler{32, [&] { api.change_small_subtext(true, random_chance()); }};
  auto upload = new ActionCycler{32, 24, [&] { api.maybe_upload_next(); }};

  auto parallel = new ParallelCycler{
      {spiral, small_subtext, upload, image, image_alt, image_timer, change_both, change_counter}};
  auto repeat = new RepeatCycler{8, parallel};
  auto oneshot = new ActionCycler{[&] {
    api.change_spiral();
    api.change_font();
    api.change_themes();
  }};

  auto start_end_timer = new SequenceCycler{{new ActionCycler{32}, new ActionCycler{64 * 15}, new ActionCycler{32}}};
  set_cycler(new OneShotCycler{{oneshot, repeat, start_end_timer}});

  set_render([=](VisualRender& api) {
    auto which_anim =
        change_alt->active() ? VisualRender::Anim::ANIM_ALTERNATE : VisualRender::Anim::ANIM;
    auto image_zoom = .625f * change_counter->progress();
    api.render_animation_or_image(which_anim, _animation_backup, 1.f, 0, image_zoom);
    if (change_counter->frame() < 16 && start_end_timer->index() == 1) {
      auto t = 15 - change_counter->frame();
      api.render_image(_current, std::min(1.f, t / 16.f), .5f,
                       .625f + .125f * change_counter->frame() / 16.f);
    }
    if (change_counter->frame() >= 48 && start_end_timer->index() == 1) {
      auto t = change_counter->frame() - 48;
      api.render_image(_current, std::min(1.f, t / 16.f), .5f, .5f + .125f * t / 16.f);
    }
    api.render_spiral();
    api.render_small_subtext(1.f / 5, .5f);
    if (!change_counter->index()) {
      api.render_text(.75f, .75f, .5f, .5f);
    }
  });
}
