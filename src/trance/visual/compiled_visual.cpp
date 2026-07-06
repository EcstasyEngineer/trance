#include <trance/visual/compiled_visual.h>
#include <common/util.h>
#include <trance/visual/api.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/render_eval.h>

namespace
{
  int32_t scalar(const pattern::Registers& regs, const std::string& name)
  {
    auto it = regs.scalars.find(name);
    return it == regs.scalars.end() ? 0 : it->second;
  }

  // Resolve an effect's slot to the concrete ThemeBank side used this firing. A non-empty
  // slot_reg reads the bool from a scalar register (a toggle/flag used as a selector);
  // Runtime (and the DSL's `random`) rolls at fire time. Image effects store this beside
  // the image register so later draws/copies keep exact debug metadata.
  pattern::Slot resolved_slot(const pattern::Effect& e, const pattern::Registers& regs)
  {
    if (!e.slot_reg.empty()) {
      return scalar(regs, e.slot_reg) != 0 ? pattern::Slot::Alternate : pattern::Slot::Primary;
    }
    switch (e.slot) {
    case pattern::Slot::Alternate:
      return pattern::Slot::Alternate;
    case pattern::Slot::Runtime:
      return random_chance() ? pattern::Slot::Alternate : pattern::Slot::Primary;
    default:
      return pattern::Slot::Primary;
    }
  }

  bool slot_bool(const pattern::Effect& e, const pattern::Registers& regs)
  {
    return resolved_slot(e, regs) == pattern::Slot::Alternate;
  }

  // `when` guard: compare a scalar register against a literal (or test truthiness).
  bool guard_passes(const pattern::Effect& e, const pattern::Registers& regs)
  {
    using G = pattern::Effect::Guard;
    if (e.guard == G::None) {
      return true;
    }
    int32_t v = scalar(regs, e.guard_reg);
    switch (e.guard) {
    case G::Truthy:
      return v != 0;
    case G::Eq:
      return v == e.guard_value;
    case G::Ge:
      return v >= e.guard_value;
    default:
      return true;
    }
  }

  void run_effect(const pattern::Effect& e, VisualControl& api, pattern::Registers& regs)
  {
    using K = pattern::Effect::Kind;
    if (!guard_passes(e, regs)) {
      return;
    }
    switch (e.kind) {
    case K::Image: {
      pattern::Slot slot = resolved_slot(e, regs);
      regs.images[e.target] = api.get_image(slot == pattern::Slot::Alternate);
      regs.image_slots[e.target] = slot;
      break;
    }
    case K::Text:
      api.change_text(static_cast<VisualControl::SplitType>(e.split), slot_bool(e, regs));
      break;
    case K::Anim:
      api.change_animation(slot_bool(e, regs));
      break;
    case K::Themes:
      // Fire-and-forget by design: patterns request a swap, they don't depend on it
      // landing (the bank may still be loading). Nothing branches on the bool.
      api.change_themes();
      break;
    case K::Font:
      api.change_font(e.force);
      break;
    case K::SpiralNew:
      api.change_spiral();
      break;
    case K::SpiralRot:
      api.rotate_spiral(e.rate);
      break;
    case K::Subtext:
      api.change_subtext(slot_bool(e, regs));
      break;
    case K::SmallSub:
      api.change_small_subtext(e.force, slot_bool(e, regs));
      break;
    case K::Upload:
      api.maybe_upload_next();
      break;
    case K::Set:
      regs.scalars[e.target] = e.ivalue;
      break;
    case K::Inc:
      regs.scalars[e.target] += e.ivalue;
      break;
    case K::Toggle:
      regs.scalars[e.target] ^= 1;
      break;
    case K::Roll:
      if (!e.choices.empty()) {
        regs.scalars[e.target] = e.choices[random(e.choices.size())];
      }
      break;
    case K::Pulse: {
      int32_t mod = e.mod_reg.empty() ? e.mod_literal : scalar(regs, e.mod_reg);
      int32_t& counter = regs.scalars[e.target];
      if (mod > 0 && ++counter >= mod) {
        counter = 0;
        regs.scalars[e.flag] = 1;
      } else {
        regs.scalars[e.flag] = 0;
      }
      break;
    }
    case K::Copy: {
      regs.images[e.target] = regs.images[e.src];
      auto it = regs.image_slots.find(e.src);
      if (it == regs.image_slots.end()) {
        regs.image_slots.erase(e.target);
      } else {
        regs.image_slots[e.target] = it->second;
      }
      break;
    }
    case K::SpiralSet:
      api.set_spiral(static_cast<uint32_t>(e.ivalue), static_cast<uint32_t>(e.mod_literal));
      break;
    case K::Audio: {
      // Resolve content (concept/reward/runtime) to a precanned path at FIRE time, exactly
      // like Image resolves its slot -- `runtime` rolls concept-vs-reward per firing, not
      // once at parse time (resolved_slot handles all three uniformly).
      pattern::Slot slot = resolved_slot(e, regs);
      const std::string& path = api.get_theme_audio(slot == pattern::Slot::Alternate);
      api.play_theme_audio(path, e.force);
      // A literal `volume` modulator (no curve/expr) is applied once here, at fire time;
      // a curve/expr volume instead rides the per-frame RenderStmt{Op::AudioVolume} the
      // parser emits alongside this effect (see pattern_parser_v3.cpp / render_eval.cpp).
      // rate < 0 is the "no volume written" sentinel: keep the channel's current volume
      // (initially full). rate == 0 is an explicit, honored mute.
      if (e.rate >= 0.f) {
        api.set_theme_audio_volume(e.rate);
      }
      break;
    }
    case K::AudioStop:
      api.stop_theme_audio();
      break;
    }
  }
}

CompiledVisual::CompiledVisual(VisualControl& api, const pattern::Node& root,
                               const std::vector<pattern::RenderStmt>& render_block)
{
  // Each Action leaf's behaviour is its ordered effect list. The descriptor IS the
  // behaviour here -- there is no separate hand-written lambda to drift from.
  auto make_action = [this, &api](const pattern::Node& n) -> std::function<void()> {
    if (n.effects.empty()) {
      return {};
    }
    auto effects = n.effects;
    return [this, &api, effects] {
      for (const auto& e : effects) {
        run_effect(e, api, _registers);
      }
    };
  };
  set_cycler(pattern::compile(root, make_action, _node_map));

  // Render is data: the block's [expr] params are evaluated each frame against the
  // registers + named cycler nodes (render_eval.cpp). An empty block falls back to a
  // single-image default so playback never shows a blank frame.
  auto stmts = render_block.empty() ? pattern::default_render_block() : render_block;
  set_render([this, stmts](VisualRender& render) {
    pattern::eval_render(stmts, render, _registers, _node_map, cycler());
  });
}
