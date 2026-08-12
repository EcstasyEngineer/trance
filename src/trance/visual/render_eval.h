#ifndef TRANCE_SRC_TRANCE_VISUAL_RENDER_EVAL_H
#define TRANCE_SRC_TRANCE_VISUAL_RENDER_EVAL_H
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_runtime.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

class VisualRender;

namespace pattern
{
  // Pure render [expr] evaluator, header-only and deliberately free of any VisualRender /
  // draw-dispatch dependency: eval_render() below (render_eval.cpp) needs the full
  // VisualRender definition, which drags api.h -> media/font.h -> SFML and can't compile
  // headlessly. Keeping the evaluator itself inline here means a headless caller (the v3
  // grammar test) can run it directly on a lowered RenderStmt's [expr] fields -- catching a
  // malformed expr the lowering code emits, which parse-time name resolution can't see --
  // without linking anything SFML-backed.
  //
  // Recursive-descent: ternary ?:, and/or, comparisons (== != < > <= >=), arithmetic
  // (+ - * / % ^, unary - and !), min/max/abs, with identifiers resolved live against
  // `regs`/`nodes`/`root`. Booleans are 1.0/0.0. Precedence (low->high):
  // ?: < or < and < compare < add < mul < power < unary < primary.
  namespace detail
  {
    inline int32_t scalar(const Registers& regs, const std::string& name)
    {
      auto it = regs.scalars.find(name);
      return it == regs.scalars.end() ? 0 : it->second;
    }

    // Resolve a render-time identifier to a number:
    //   "<node-id>.<attr>" -> live cycler state (progress/frame/length/position/index/
    //                         active); node-id "root" is the pattern root.
    //   "<name>"           -> scalar register value (0 if unset).
    inline double resolve_ident(const std::string& id, const Registers& regs,
                                const NodeMap& nodes, const Cycler* root)
    {
      auto dot = id.find('.');
      if (dot == std::string::npos) {
        return double(scalar(regs, id));
      }
      std::string name = id.substr(0, dot);
      std::string attr = id.substr(dot + 1);
      const Cycler* c = root;
      if (name != "root") {
        auto it = nodes.find(name);
        c = it == nodes.end() ? nullptr : it->second;
      }
      if (!c) {
        return 0.0;
      }
      if (attr == "progress") return c->progress();
      if (attr == "frame") return double(c->frame());
      if (attr == "length") return double(c->length());
      if (attr == "position") return double(c->position());
      if (attr == "index") return double(c->index());
      if (attr == "active") return c->active() ? 1.0 : 0.0;
      return 0.0;
    }

    struct Eval {
      const std::string& s;
      std::size_t i;
      const Registers& regs;
      const NodeMap& nodes;
      const Cycler* root;
      Eval(const std::string& s_, const Registers& r_, const NodeMap& n_, const Cycler* root_)
      : s(s_), i(0), regs(r_), nodes(n_), root(root_)
      {
      }

      void ws()
      {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
      }
      bool starts(const char* p)
      {
        ws();
        std::size_t j = i;
        for (const char* q = p; *q; ++q, ++j) {
          if (j >= s.size() || s[j] != *q) return false;
        }
        return true;
      }
      bool kw(const char* w)
      {
        ws();
        std::size_t j = i;
        const char* q = w;
        for (; *q && j < s.size() && s[j] == *q; ++q, ++j) {
        }
        if (*q) return false;
        if (j < s.size()
            && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == '.')) {
          return false;  // a longer identifier, not the keyword
        }
        i = j;
        return true;
      }
      double run() { return ternary(); }
      double ternary()
      {
        double c = b_or();
        ws();
        if (i < s.size() && s[i] == '?') {
          ++i;
          double a = ternary();
          ws();
          if (i < s.size() && s[i] == ':') ++i;
          double b = ternary();
          return c != 0.0 ? a : b;
        }
        return c;
      }
      double b_or()
      {
        double v = b_and();
        while (kw("or")) {
          double r = b_and();
          v = (v != 0.0 || r != 0.0) ? 1.0 : 0.0;
        }
        return v;
      }
      double b_and()
      {
        double v = cmp();
        while (kw("and")) {
          double r = cmp();
          v = (v != 0.0 && r != 0.0) ? 1.0 : 0.0;
        }
        return v;
      }
      double cmp()
      {
        double v = add();
        if (starts("==")) { i += 2; return v == add() ? 1.0 : 0.0; }
        if (starts("!=")) { i += 2; return v != add() ? 1.0 : 0.0; }
        if (starts("<=")) { i += 2; return v <= add() ? 1.0 : 0.0; }
        if (starts(">=")) { i += 2; return v >= add() ? 1.0 : 0.0; }
        if (starts("<")) { i += 1; return v < add() ? 1.0 : 0.0; }
        if (starts(">")) { i += 1; return v > add() ? 1.0 : 0.0; }
        return v;
      }
      double add()
      {
        double v = mul();
        for (ws(); i < s.size() && (s[i] == '+' || s[i] == '-'); ws()) {
          char op = s[i++];
          double r = mul();
          v = op == '+' ? v + r : v - r;
        }
        return v;
      }
      double mul()
      {
        double v = pw();
        for (ws(); i < s.size() && (s[i] == '*' || s[i] == '/' || s[i] == '%'); ws()) {
          char op = s[i++];
          double r = pw();
          if (op == '*') {
            v = v * r;
          } else if (op == '/') {
            v = r != 0.0 ? v / r : 0.0;
          } else {
            v = r != 0.0 ? std::fmod(v, r) : 0.0;
          }
        }
        return v;
      }
      double pw()
      {
        double b = unary();
        ws();
        if (i < s.size() && s[i] == '^') {
          ++i;
          return std::pow(b, pw());
        }
        return b;
      }
      double unary()
      {
        ws();
        if (i < s.size() && s[i] == '!') {
          ++i;
          return unary() == 0.0 ? 1.0 : 0.0;
        }
        if (i < s.size() && s[i] == '-') {
          ++i;
          return -unary();
        }
        return primary();
      }
      double primary()
      {
        ws();
        if (i >= s.size()) return 0.0;
        if (s[i] == '(') {
          ++i;
          double v = ternary();
          ws();
          if (i < s.size() && s[i] == ')') ++i;
          return v;
        }
        if (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.') {
          std::size_t st = i;
          while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
            ++i;
          return std::stod(s.substr(st, i - st));
        }
        if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') {
          std::size_t st = i;
          while (i < s.size()
                 && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.'))
            ++i;
          std::string name = s.substr(st, i - st);
          ws();
          if (i < s.size() && s[i] == '(') {  // function call: min/max/abs
            ++i;
            double a = ternary();
            double b = 0.0;
            bool two = false;
            ws();
            if (i < s.size() && s[i] == ',') {
              ++i;
              b = ternary();
              two = true;
            }
            ws();
            if (i < s.size() && s[i] == ')') ++i;
            if (name == "min") return two ? std::min(a, b) : a;
            if (name == "max") return two ? std::max(a, b) : a;
            if (name == "abs") return std::fabs(a);
            return a;  // unknown function: pass through
          }
          return resolve_ident(name, regs, nodes, root);
        }
        ++i;  // skip an unexpected char rather than spin
        return 0.0;
      }
    };
  }

  // `dflt` is returned for an empty expr; otherwise the expr is parsed and evaluated.
  inline double eval_expr(const std::string& expr, double dflt, const Registers& regs,
                          const NodeMap& nodes, const Cycler* root)
  {
    if (expr.empty()) return dflt;
    detail::Eval e{expr, regs, nodes, root};
    return e.run();
  }

  // As eval_expr, but for a `when [cond]` guard: empty => true, else expr != 0.
  inline bool eval_cond_expr(const std::string& expr, const Registers& regs, const NodeMap& nodes,
                             const Cycler* root)
  {
    if (expr.empty()) return true;
    detail::Eval e{expr, regs, nodes, root};
    return e.run() != 0.0;
  }

  // Which animation stream a `... anim` draw pulls from this frame. Pure -- no
  // VisualRender -- for the same reason the evaluator above is: it is the one decision the
  // draw side makes about animations, and it has to be assertable headlessly. It got that
  // way by being wrong: the draw used to consult a RenderStmt field the parser never
  // filled, so it always pulled from the primary streamer no matter what the Anim effect
  // had loaded, and every test that "covered" it was inspecting the lowered effect list
  // instead of the draw. eval_render() maps the result onto VisualRender::Anim.
  //
  // `Still` is not "the other animation" -- it is the `anim if [gate]` case, where the
  // draw falls back to the still image for this frame.
  enum class AnimDraw { Still, Primary, Alternate };

  inline AnimDraw anim_draw_for(const RenderStmt& st, const Registers& regs,
                                const NodeMap& nodes, const Cycler* root)
  {
    if (!st.has_anim) return AnimDraw::Still;
    if (!st.anim_gate.empty() && !eval_cond_expr(st.anim_gate, regs, nodes, root)) {
      return AnimDraw::Still;
    }
    // Follow the LOAD, don't re-decide: the Anim effect already resolved primary vs secondary
    // (and rolled `runtime`) at fire time. See Registers::anim_slot.
    return regs.anim_slot == Slot::Secondary ? AnimDraw::Alternate : AnimDraw::Primary;
  }

  // Run a pattern's data-driven render block for one frame: evaluate each statement's
  // [expr] params against live cycler state (via the node-id map / root) and the
  // registers, and emit the matching VisualRender draw call. This is the generic
  // replacement for the named C++ render presets -- the render is data, not code.
  void eval_render(const std::vector<RenderStmt>& stmts, VisualRender& api,
                   const Registers& regs, const NodeMap& nodes, const Cycler* root);

  // The render block used when a pattern declares none (draws the "current" image +
  // spiral + text), so playback never shows a blank frame.
  std::vector<RenderStmt> default_render_block();
}

#endif
