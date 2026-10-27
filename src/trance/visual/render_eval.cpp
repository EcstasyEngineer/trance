#include <trance/visual/render_eval.h>
#include <trance/visual/api.h>
#include <trance/visual/cyclers.h>
#include <string>

namespace
{
  // The numeric/bool evaluator itself (pattern::eval_expr / eval_cond_expr) lives inline in
  // render_eval.h so a headless caller can run it without VisualRender/api.h; these are just
  // short local aliases so the draw-dispatch code below reads the same as before the split.
  float eval_num(const std::string& expr, double dflt, const pattern::Registers& regs,
                 const pattern::NodeMap& nodes, const Cycler* root)
  {
    return static_cast<float>(pattern::eval_expr(expr, dflt, regs, nodes, root));
  }

  bool eval_cond(const std::string& expr, const pattern::Registers& regs,
                 const pattern::NodeMap& nodes, const Cycler* root)
  {
    return pattern::eval_cond_expr(expr, regs, nodes, root);
  }

  Image image_reg(const pattern::Registers& regs, const std::string& name)
  {
    auto it = regs.images.find(name);
    return it == regs.images.end() ? Image{} : it->second;
  }

  VisualRender::ThemeSlot image_slot_reg(const pattern::Registers& regs, const std::string& name)
  {
    auto it = regs.image_slots.find(name);
    if (it == regs.image_slots.end()) {
      return VisualRender::ThemeSlot::None;
    }
    return it->second == pattern::Slot::Secondary ? VisualRender::ThemeSlot::Alternate
                                                  : VisualRender::ThemeSlot::Primary;
  }
}

namespace pattern
{

  void eval_render(const std::vector<RenderStmt>& stmts, VisualRender& api, const Registers& regs,
                   const NodeMap& nodes, const Cycler* root)
  {
    for (const auto& st : stmts) {
      if (!eval_cond(st.when, regs, nodes, root)) {
        continue;
      }
      switch (st.op) {
      case RenderStmt::Op::Spiral:
        // Spiral speed is a curve-drivable render parameter: advance the spiral phase by the
        // per-frame speed before drawing, so `spiral speed (curve ...)` reads exactly like zoom.
        // ... but only once per FRAME, and by the playback time that frame covers: this
        // whole block is evaluated again for the second eye and again for the desktop
        // pass, and advancing there too would spin two or three times too fast and leave
        // the passes on different angles. render_mutation_frames() is the elapsed
        // playback time in 60Hz reference frames -- the authored rate is per such frame --
        // so the spiral turns at the same speed whatever rate the frame was presented at,
        // and freezes (0) while paused (VisualRender::render_mutations_enabled).
        if (!st.speed.empty() && api.render_mutations_enabled()) {
          api.rotate_spiral(float(api.render_mutation_frames()) *
                            eval_num(st.speed, 0.0, regs, nodes, root));
        }
        api.render_spiral();
        break;
      case RenderStmt::Op::Image: {
        Image image = image_reg(regs, st.image_reg);
        VisualRender::ThemeSlot slot = image_slot_reg(regs, st.image_reg);
        float alpha = eval_num(st.alpha, 1.0, regs, nodes, root);
        float origin = eval_num(st.origin, 0.0, regs, nodes, root);
        float zoom = eval_num(st.zoom, 0.0, regs, nodes, root);
        if (!st.has_anim) {
          api.render_image(image, alpha, origin, zoom, slot);
          break;
        }
        VisualRender::Anim type;
        switch (anim_draw_for(st, regs, nodes, root)) {
        case AnimDraw::Still:
          type = VisualRender::Anim::NONE;
          break;
        case AnimDraw::Alternate:
          type = VisualRender::Anim::ANIM_ALTERNATE;
          break;
        default:
          type = VisualRender::Anim::ANIM;
          break;
        }
        api.render_animation_or_image(type, image, alpha, origin, zoom, slot);
        break;
      }
      case RenderStmt::Op::Text: {
        float origin = eval_num(st.origin, 0.0, regs, nodes, root);
        float zoom = eval_num(st.zoom, 0.0, regs, nodes, root);
        float shadow_origin = eval_num(st.shadow_origin, 0.0, regs, nodes, root);
        float shadow_zoom = eval_num(st.shadow_zoom, 0.0, regs, nodes, root);
        api.render_text(origin, zoom, shadow_origin, shadow_zoom);
        break;
      }
      case RenderStmt::Op::Subtext:
        api.render_subtext(eval_num(st.alpha, 1.0, regs, nodes, root),
                           eval_num(st.origin, 0.0, regs, nodes, root));
        break;
      case RenderStmt::Op::SmallText:
        api.render_small_subtext(eval_num(st.alpha, 1.0, regs, nodes, root),
                                 eval_num(st.origin, 0.0, regs, nodes, root));
        break;
      }
    }
  }
}
