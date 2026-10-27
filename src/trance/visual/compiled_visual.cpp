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
  // The authored `either` source rolls at fire time. Image effects store this beside
  // the image capture so later draws keep exact debug metadata.
  pattern::Slot resolved_slot(const pattern::Effect& e, const pattern::Registers& regs)
  {
    if (!e.slot_reg.empty()) {
      return scalar(regs, e.slot_reg) != 0 ? pattern::Slot::Secondary : pattern::Slot::Primary;
    }
    switch (e.slot) {
    case pattern::Slot::Secondary:
      return pattern::Slot::Secondary;
    default:
      return pattern::Slot::Primary;
    }
  }

  bool slot_bool(const pattern::Effect& e, const pattern::Registers& regs)
  {
    return resolved_slot(e, regs) == pattern::Slot::Secondary;
  }

  bool guard_passes(const pattern::Effect& e, const pattern::Registers& regs)
  {
    return e.guard == pattern::Effect::Guard::None || scalar(regs, e.guard_reg) != 0;
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
      const bool alternate = slot == pattern::Slot::Secondary;
      bool from_current_theme = false;
      Image image = api.get_image(alternate, &from_current_theme);
      // A not-yet-ready lane must not erase a drawable capture. Keep it until the
      // current theme produces a replacement, just as refresh does after a swap.
      if (image || !regs.images[e.target]) {
        regs.images[e.target] = image;
      }
      regs.image_slots[e.target] = slot;
      // Stamp the lane generation this was pulled at, so a theme swap can be detected
      // later and the register refreshed rather than left holding a dead theme's frame.
      const uint32_t generation = api.lane_generation(alternate);
      // A last-good fallback is drawable but not current. Leave its stamp behind
      // so readiness within this same generation triggers another attempt.
      regs.image_gens[e.target] = from_current_theme ? generation : generation - 1;
      break;
    }
    case K::Text:
      api.change_text(static_cast<VisualControl::SplitType>(e.split), slot_bool(e, regs));
      break;
    case K::Anim:
      api.change_animation(resolved_slot(e, regs) == pattern::Slot::Secondary);
      break;
    case K::Subtext:
      api.change_subtext(slot_bool(e, regs));
      break;
    case K::SmallSub:
      api.change_small_subtext(e.force, slot_bool(e, regs));
      break;
    case K::Toggle:
      regs.scalars[e.target] ^= 1;
      break;
    case K::Chance: {
      // Exact endpoints and rational odds; these are authored V4 policies.
      const uint32_t denominator = e.probability_denominator;
      const uint32_t numerator = e.probability_numerator;
      regs.scalars[e.target] = numerator && denominator
          && (numerator >= denominator || random(denominator) < numerator) ? 1 : 0;
      break;
    }
    case K::Roll:
      if (!e.choices.empty()) {
        regs.scalars[e.target] = e.choices[random(e.choices.size())];
      }
      break;
    case K::Pulse: {
      const int32_t mod = e.mod_literal;
      int32_t& counter = regs.scalars[e.target];
      if (mod > 0 && ++counter >= mod) {
        counter = 0;
        regs.scalars[e.flag] = 1;
      } else {
        regs.scalars[e.flag] = 0;
      }
      break;
    }
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

  // Render is data: compiled parameters read capture state and occurrence clocks.
  // An empty V4 document has no outputs. Do not synthesize historical layers.
  auto stmts = render_block;
  set_render([this, stmts, &api](VisualRender& render) {
    // Once per FRAME, on its first pass -- never per pass. A refresh mid-frame would let
    // the two eyes draw DIFFERENT images from the same registers (spec trap 1).
    if (render.render_mutations_enabled()) {
      refresh_stale_registers(api);
    }
    pattern::eval_render(stmts, render, _registers, _node_map, cycler());
  });
}

void CompiledVisual::refresh_stale_registers(VisualControl& api)
{
  // An `image` effect captures ONE Image and the render block redraws it until that
  // effect fires again. Across a theme swap that means a register can sit on screen
  // holding a frame of a theme that is no longer live -- and because Image is
  // ref-counted, the frame stays perfectly valid, so it displays cleanly rather than
  // failing visibly. Re-pull the ones that have fallen behind.
  //
  // Done here, once per frame (on the frame's first pass -- see the call site), rather
  // than at the draw site: get_image runs the selection shuffle on every call, so
  // re-pulling per draw would hand a still register a different random image every frame
  // (and a different one per eye in stereo). This
  // is also why it is not hooked to the `themes` EFFECT -- the playlist's own swap
  // (main.cpp, swaps_to_match_theme) never runs a pattern effect at all, whereas the
  // lane generation is bumped by advance_theme, which every swap goes through.
  for (auto& entry : _registers.images) {
    auto gen = _registers.image_gens.find(entry.first);
    if (gen == _registers.image_gens.end()) {
      // No captured generation is available to compare.
      continue;
    }
    auto slot = _registers.image_slots.find(entry.first);
    if (slot == _registers.image_slots.end()) {
      continue;
    }
    const bool alternate = slot->second == pattern::Slot::Secondary;
    const uint32_t generation = api.lane_generation(alternate);
    if (gen->second == generation && entry.second) {
      continue;
    }
    // get_current_theme_image, NOT get_image: get_image's never-black fallback returns
    // the PREVIOUS theme's frame, which is indistinguishable from a real pick by
    // operator bool. Accepting one of those and stamping it with the new generation
    // would mark the register current while it still held the dead theme's image, and it
    // would never be retried -- reinstating the exact bug this fixes. An empty result
    // means the new theme has nothing yet: keep drawing what is there (never black) and
    // leave the generation mismatched so the next frame tries again.
    Image fresh = api.get_current_theme_image(alternate);
    if (!fresh) {
      continue;
    }
    entry.second = fresh;
    gen->second = generation;
  }
}
