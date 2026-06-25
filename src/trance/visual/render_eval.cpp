#include <trance/visual/render_eval.h>
#include <trance/visual/api.h>
#include <trance/visual/cyclers.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace
{
  int32_t scalar(const pattern::Registers& regs, const std::string& name)
  {
    auto it = regs.scalars.find(name);
    return it == regs.scalars.end() ? 0 : it->second;
  }

  Image image_reg(const pattern::Registers& regs, const std::string& name)
  {
    auto it = regs.images.find(name);
    return it == regs.images.end() ? Image{} : it->second;
  }

  // Resolve a render-time identifier to a number:
  //   "<node-id>.<attr>" -> live cycler state (progress/frame/length/position/index/
  //                         active); node-id "root" is the pattern root.
  //   "<name>"           -> scalar register value (0 if unset).
  double resolve_ident(const std::string& id, const pattern::Registers& regs,
                       const pattern::NodeMap& nodes, const Cycler* root)
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

  // Recursive-descent evaluator for a render [expr]. A superset of the parser's
  // generate-time evaluator (pattern_parser.cpp ExprEval): ternary ?:, and/or,
  // comparisons (== != < > <= >=), arithmetic (+ - * / % ^, unary - and !), min/max/abs,
  // with identifiers resolved live. Booleans are 1.0/0.0. Precedence (low->high):
  // ?: < or < and < compare < add < mul < power < unary < primary.
  struct Eval {
    const std::string& s;
    std::size_t i;
    const pattern::Registers& regs;
    const pattern::NodeMap& nodes;
    const Cycler* root;
    Eval(const std::string& s_, const pattern::Registers& r_, const pattern::NodeMap& n_,
         const Cycler* root_)
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
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
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

  float eval_num(const std::string& expr, double dflt, const pattern::Registers& regs,
                 const pattern::NodeMap& nodes, const Cycler* root)
  {
    if (expr.empty()) return static_cast<float>(dflt);
    Eval e{expr, regs, nodes, root};
    return static_cast<float>(e.run());
  }

  bool eval_cond(const std::string& expr, const pattern::Registers& regs,
                 const pattern::NodeMap& nodes, const Cycler* root)
  {
    if (expr.empty()) return true;
    Eval e{expr, regs, nodes, root};
    return e.run() != 0.0;
  }
}

namespace pattern
{
  std::vector<RenderStmt> default_render_block()
  {
    // Used when a pattern carries no render block: draw the "current" image with a
    // progress-driven zoom, the spiral, and the current text -- so a pattern always
    // renders something rather than a blank frame.
    std::vector<RenderStmt> stmts;
    RenderStmt image;
    image.op = RenderStmt::Op::Image;
    image.image_reg = "current";
    image.zoom = "0.5 * root.progress";
    stmts.push_back(image);
    RenderStmt spiral;
    spiral.op = RenderStmt::Op::Spiral;
    stmts.push_back(spiral);
    RenderStmt text;
    text.op = RenderStmt::Op::Text;
    text.origin = "0.75";
    text.zoom = "0.75";
    text.shadow_origin = "0.5 * root.progress";
    text.shadow_zoom = "0.5 * root.progress";
    stmts.push_back(text);
    return stmts;
  }

  void eval_render(const std::vector<RenderStmt>& stmts, VisualRender& api, const Registers& regs,
                   const NodeMap& nodes, const Cycler* root)
  {
    for (const auto& st : stmts) {
      if (!eval_cond(st.when, regs, nodes, root)) {
        continue;
      }
      switch (st.op) {
      case RenderStmt::Op::Spiral:
        // Spiral speed is a curve-drivable render param (v3): advance the spiral phase by the
        // per-frame speed before drawing, so `spiral speed (curve ...)` reads exactly like zoom.
        if (!st.speed.empty()) {
          api.rotate_spiral(eval_num(st.speed, 0.0, regs, nodes, root));
        }
        api.render_spiral();
        break;
      case RenderStmt::Op::Image: {
        Image image = image_reg(regs, st.image_reg);
        float alpha = eval_num(st.alpha, 1.0, regs, nodes, root);
        float origin = eval_num(st.origin, 0.0, regs, nodes, root);
        float zoom = eval_num(st.zoom, 0.0, regs, nodes, root);
        if (!st.has_anim) {
          api.render_image(image, alpha, origin, zoom);
          break;
        }
        VisualRender::Anim type;
        if (!st.anim_gate.empty() && !eval_cond(st.anim_gate, regs, nodes, root)) {
          type = VisualRender::Anim::NONE;
        } else if (!st.anim_alt.empty() && eval_cond(st.anim_alt, regs, nodes, root)) {
          type = VisualRender::Anim::ANIM_ALTERNATE;
        } else {
          type = VisualRender::Anim::ANIM;
        }
        api.render_animation_or_image(type, image, alpha, origin, zoom);
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
