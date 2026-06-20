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

  // Resolve an effect's slot to the get_image / change_* "alternate" bool. A non-empty
  // slot_reg reads the bool from a scalar register (a toggle/flag used as a selector);
  // otherwise the static slot decides. Runtime (and the DSL's `random`) rolls at fire
  // time.
  bool slot_bool(const pattern::Effect& e, const pattern::Registers& regs)
  {
    if (!e.slot_reg.empty()) {
      return scalar(regs, e.slot_reg) != 0;
    }
    switch (e.slot) {
    case pattern::Slot::Alternate:
      return true;
    case pattern::Slot::Runtime:
      return random_chance();
    default:
      return false;
    }
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

  // SUPER_FAST's 4-state FSM, isolated here (the one genuine state machine among the
  // visuals). Ticks once per call; reads/writes its own fixed scalar registers
  // (sf_*) and the current/next image registers. The render_super_fast preset reads
  // the same registers plus the "rapid" leaf's frame. Faithful port of the rapid
  // action lambda in the former SuperFastVisual ctor. States: 0 RAPID, 1 START, 2
  // ANIMATION, 3 END.
  void run_super_fast_tick(VisualControl& api, pattern::Registers& regs)
  {
    auto& s = regs.scalars;
    int32_t& state = s["sf_state"];
    int32_t& atimer = s["sf_anim_timer"];
    int32_t& cooldown = s["sf_cooldown"];
    int32_t& tmod = s["sf_text_mod"];
    int32_t& alt = s["sf_alternate"];

    if (atimer == 4) {
      api.maybe_upload_next();
    }
    if (cooldown) {
      --cooldown;
    }
    if (state == 0) {
      tmod = (1 + tmod) % 4;
    }
    if (state == 3) {
      tmod = 0;
      cooldown = 8;
      state = 0;
    }
    if (state == 2) {
      --atimer;
      if (!atimer) {
        state = 3;
      }
    }
    if (state == 1) {
      state = 2;
      --atimer;
    }
    if (state == 0 && !cooldown && random_chance(12)) {
      state = 1;
      atimer = 8 + static_cast<int32_t>(random(9));
      alt = !alt;
      api.change_animation(alt != 0);
    }
    if (state != 2) {
      Image current = regs.images["next"];
      if (!current) {
        current = api.get_image(alt != 0);
      }
      regs.images["current"] = current;
      regs.images["next"] = api.get_image(alt != 0);
      if (tmod == 0) {
        api.change_text(VisualControl::SPLIT_WORD, alt != 0);
      }
    }
  }

  void run_effect(const pattern::Effect& e, VisualControl& api, pattern::Registers& regs)
  {
    using K = pattern::Effect::Kind;
    if (!guard_passes(e, regs)) {
      return;
    }
    switch (e.kind) {
    case K::Image:
      regs.images[e.target] = api.get_image(slot_bool(e, regs));
      break;
    case K::Text:
      api.change_text(static_cast<VisualControl::SplitType>(e.split), slot_bool(e, regs));
      break;
    case K::Anim:
      api.change_animation(slot_bool(e, regs));
      break;
    case K::Themes:
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
    case K::Copy:
      regs.images[e.target] = regs.images[e.src];
      break;
    case K::SuperFastTick:
      run_super_fast_tick(api, regs);
      break;
    }
  }
}

CompiledVisual::CompiledVisual(VisualControl& api, const pattern::Node& root,
                               const std::string& render_preset_name,
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

  if (!render_block.empty()) {
    // Data-driven render: the block's [expr] params are evaluated each frame against the
    // registers + named cycler nodes. No per-pattern C++.
    auto stmts = render_block;
    set_render([this, stmts](VisualRender& render) {
      pattern::eval_render(stmts, render, _registers, _node_map, cycler());
    });
  } else {
    // Legacy path: a named C++ render preset (render_preset.cpp), being retired as each
    // built-in moves its render into a render_block. An unknown name falls through to a
    // simple single-image default rather than failing playback.
    auto preset = pattern::render_preset(render_preset_name);
    set_render([this, preset](VisualRender& render) {
      preset(render, _registers, _node_map, cycler());
    });
  }
}
