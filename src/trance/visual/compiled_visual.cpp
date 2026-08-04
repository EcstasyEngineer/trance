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
      // Stamp the lane generation this was pulled at, so a theme swap can be detected
      // later and the register refreshed rather than left holding a dead theme's frame.
      // Presence of the entry is also what marks the register as a LIVE pull rather than
      // a `copy` snapshot (see Registers::image_gens).
      regs.image_gens[e.target] = api.lane_generation(slot == pattern::Slot::Alternate);
      break;
    }
    case K::Text:
      api.change_text(static_cast<VisualControl::SplitType>(e.split), slot_bool(e, regs));
      break;
    case K::Anim:
      // Record the slot as well as loading it: the draw side reads regs.anim_slot to know
      // which streamer to pull the frame from. Without that the load and the draw disagree
      // and every `anim` renders the primary streamer -- `anim reward` and the `alternate`
      // ping-pong were invisible on screen even though the load was correct.
      regs.anim_slot = resolved_slot(e, regs);
      api.change_animation(regs.anim_slot == pattern::Slot::Alternate);
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
    case K::Subtext:
      api.change_subtext(slot_bool(e, regs));
      break;
    case K::SmallSub:
      api.change_small_subtext(e.force, slot_bool(e, regs));
      break;
    case K::Set:
      regs.scalars[e.target] = e.ivalue;
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
      // A copy is a SNAPSHOT of a past state, not a live alias -- that is the whole of
      // how crossfade works (`copy cur -> prev`, then draw prev under the fading new
      // cur). Dropping the generation stamp marks it as such, so the swap refresh leaves
      // it alone: auto-refreshing it would rewrite the outgoing frame and turn old->new
      // into new->new. It stays exactly what was copied until another copy or image
      // effect overwrites it.
      regs.image_gens.erase(e.target);
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
  set_render([this, stmts, &api](VisualRender& render) {
    refresh_stale_registers(api);
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
  // Done here, once per frame, rather than at the draw site: get_image runs the
  // selection shuffle on every call, so re-pulling per draw would hand a still register
  // a different random image every frame (and a different one per eye in stereo). This
  // is also why it is not hooked to the `themes` EFFECT -- the playlist's own swap
  // (main.cpp, swaps_to_match_theme) never runs a pattern effect at all, whereas the
  // lane generation is bumped by advance_theme, which every swap goes through.
  for (auto& entry : _registers.images) {
    auto gen = _registers.image_gens.find(entry.first);
    if (gen == _registers.image_gens.end()) {
      // A `copy` snapshot: deliberately frozen, never auto-refreshed.
      continue;
    }
    auto slot = _registers.image_slots.find(entry.first);
    if (slot == _registers.image_slots.end()) {
      continue;
    }
    const bool alternate = slot->second == pattern::Slot::Alternate;
    const uint32_t generation = api.lane_generation(alternate);
    if (gen->second == generation) {
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
