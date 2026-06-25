#include <trance/visual/pattern_parser_v2.h>

#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
  using pattern::Effect;
  using pattern::Node;
  using pattern::RenderStmt;
  using pattern::Slot;

  struct ParseError
  {
    std::string message;
    std::size_t pos;
  };

  // A named curve: a value moving A->B across a phase. Drives cadence (`every <curve>`,
  // unrolled into a ramp of segments) or an attribute (`zoom <curve>`).
  struct Curve
  {
    float from = 0.f;
    float to = 0.f;
    std::string ease;
  };

  // One image layer contributed to the render block. Multiple layers with distinct
  // registers composite (super_parallel); layers sharing a register across phases ternary
  // by phase (slow_flash).
  struct Layer
  {
    std::string phase_id;
    std::string reg = "current";
    std::string zoom;
    std::string origin;
    std::string alpha;  // may be empty (=> 1)
    bool anim = false;
    std::string anim_gate;
  };

  // ---- tokenizer ------------------------------------------------------------
  class Cursor
  {
  public:
    explicit Cursor(const std::string& src) : _src(src) {}
    std::size_t pos() const { return _i; }

    char peek_char()
    {
      skip();
      return _i < _src.size() ? _src[_i] : '\0';
    }

    std::string word()
    {
      skip();
      const std::size_t start = _i;
      while (_i < _src.size() && (std::isalnum(static_cast<unsigned char>(_src[_i])) || _src[_i] == '_')) {
        ++_i;
      }
      if (_i == start) {
        throw ParseError{"expected a word", start};
      }
      return _src.substr(start, _i - start);
    }

    std::string string_lit()
    {
      skip();
      if (_i >= _src.size() || _src[_i] != '"') {
        throw ParseError{"expected a quoted string", _i};
      }
      const std::size_t start = ++_i;
      while (_i < _src.size() && _src[_i] != '"') {
        ++_i;
      }
      if (_i >= _src.size()) {
        throw ParseError{"unterminated string", start};
      }
      std::string s = _src.substr(start, _i - start);
      ++_i;
      return s;
    }

    uint32_t uint_lit()
    {
      skip();
      const std::size_t start = _i;
      while (_i < _src.size() && std::isdigit(static_cast<unsigned char>(_src[_i]))) {
        ++_i;
      }
      if (_i == start) {
        throw ParseError{"expected an integer", start};
      }
      return static_cast<uint32_t>(std::stoul(_src.substr(start, _i - start)));
    }

    float number_lit()
    {
      skip();
      const std::size_t start = _i;
      while (_i < _src.size() &&
             (std::isdigit(static_cast<unsigned char>(_src[_i])) || _src[_i] == '.')) {
        ++_i;
      }
      if (_i == start) {
        throw ParseError{"expected a number", start};
      }
      return std::stof(_src.substr(start, _i - start));
    }

    void expect(char c)
    {
      skip();
      if (_i >= _src.size() || _src[_i] != c) {
        throw ParseError{std::string("expected '") + c + "'", _i};
      }
      ++_i;
    }

    bool accept(char c)
    {
      skip();
      if (_i < _src.size() && _src[_i] == c) {
        ++_i;
        return true;
      }
      return false;
    }

    std::string peek_word()
    {
      skip();
      std::size_t j = _i;
      while (j < _src.size() && (std::isalnum(static_cast<unsigned char>(_src[j])) || _src[j] == '_')) {
        ++j;
      }
      return _src.substr(_i, j - _i);
    }

    bool next_is_digit()
    {
      skip();
      return _i < _src.size() && std::isdigit(static_cast<unsigned char>(_src[_i]));
    }

    bool next_is_bracket()
    {
      skip();
      return _i < _src.size() && _src[_i] == '[';
    }

    // Capture the raw text between `[` and the matching `]` (no nesting; render exprs use
    // parens, not brackets), returned verbatim for the render evaluator to run each frame.
    std::string bracket_expr()
    {
      skip();
      if (_i >= _src.size() || _src[_i] != '[') {
        throw ParseError{"expected '['", _i};
      }
      const std::size_t start = ++_i;
      while (_i < _src.size() && _src[_i] != ']') {
        ++_i;
      }
      if (_i >= _src.size()) {
        throw ParseError{"unterminated [expr]", start};
      }
      std::string s = _src.substr(start, _i - start);
      ++_i;  // closing ]
      return s;
    }

  private:
    void skip()
    {
      for (;;) {
        while (_i < _src.size() && std::isspace(static_cast<unsigned char>(_src[_i]))) {
          ++_i;
        }
        if (_i < _src.size() && _src[_i] == '#') {
          while (_i < _src.size() && _src[_i] != '\n') {
            ++_i;
          }
          continue;
        }
        break;
      }
    }

    const std::string& _src;
    std::size_t _i = 0;
  };

  // ---- node helpers ---------------------------------------------------------
  Node action(uint32_t length, std::vector<Effect> effects = {})
  {
    Node n;
    n.type = Node::Type::Action;
    n.length = length;
    n.effects = std::move(effects);
    return n;
  }
  Node repeat(uint32_t count, Node child)
  {
    Node n;
    n.type = Node::Type::Rep;
    n.count = count;
    n.children.push_back(std::move(child));
    return n;
  }
  Node offset(uint32_t frames, Node child)
  {
    Node n;
    n.type = Node::Type::Off;
    n.count = frames;
    n.children.push_back(std::move(child));
    return n;
  }
  Node group(Node::Type type, std::vector<Node> children)
  {
    Node n;
    n.type = type;
    n.children = std::move(children);
    return n;
  }
  Effect effect(Effect::Kind kind)
  {
    Effect e;
    e.kind = kind;
    return e;
  }
  Slot theme_to_slot(const std::string& w, std::size_t at)
  {
    if (w == "concept") return Slot::Primary;
    if (w == "reward") return Slot::Alternate;
    if (w == "runtime") return Slot::Runtime;
    throw ParseError{"unknown theme '" + w + "' (want concept|reward|runtime)", at};
  }
  uint32_t ease_count(const Curve& c, int L)
  {
    if (c.ease == "late") {
      const double base = c.from > c.to ? c.from : c.to;
      const double d = std::fabs(double(c.from) - double(L));
      const double denom = std::pow(base, 5.0);
      return 1u + (denom > 0.0 ? static_cast<uint32_t>(std::pow(d, 6.0) / denom) : 0u);
    }
    return 1u;
  }
  std::string fnum(float v) { return std::to_string(v); }

  // ---- render-block generation ----------------------------------------------
  std::vector<RenderStmt> build_render_block(const std::vector<Layer>& layers, uint32_t phase_count,
                                             bool any_word, bool any_sub, bool any_caption,
                                             bool any_spiral)
  {
    std::vector<RenderStmt> rb;

    // Group layers by register, preserving first-seen order.
    std::vector<std::string> order;
    std::map<std::string, std::vector<const Layer*>> groups;
    for (const auto& l : layers) {
      if (!groups.count(l.reg)) {
        order.push_back(l.reg);
      }
      groups[l.reg].push_back(&l);
    }

    const auto emit_layer = [&](const std::string& reg, const Layer* l) {
      RenderStmt im;
      im.op = RenderStmt::Op::Image;
      im.image_reg = reg;
      im.zoom = l->zoom;
      im.origin = l->origin;
      im.alpha = l->alpha;
      if (l->anim) {
        im.has_anim = true;
        im.anim_gate = l->anim_gate;
      }
      rb.push_back(im);
    };

    for (const auto& reg : order) {
      const auto& g = groups[reg];
      if (g.size() == 1) {
        emit_layer(reg, g.front());
      } else if (phase_count == 1) {
        // One phase compositing several layers on the same register: draw each (the stack).
        for (const Layer* l : g) {
          emit_layer(reg, l);
        }
      } else {
        // Same register across phases: pick the active phase's params via a `.active` ternary
        // (slow_flash). Guard empty zoom with the op default 0, and ternary the anim gate too
        // so animation isn't silently dropped on a cross-phase reuse.
        RenderStmt im;
        im.op = RenderStmt::Op::Image;
        im.image_reg = reg;
        std::string z, a, an;
        bool any_a = false, any_anim = false;
        for (const auto* l : g) {
          z += l->phase_id + ".active ? (" + (l->zoom.empty() ? "0" : l->zoom) + ") : ";
          const std::string av = l->alpha.empty() ? "1" : l->alpha;
          if (!l->alpha.empty()) any_a = true;
          a += l->phase_id + ".active ? (" + av + ") : ";
          std::string gv = "0";
          if (l->anim) {
            any_anim = true;
            gv = l->anim_gate.empty() ? "1" : l->anim_gate;
          }
          an += l->phase_id + ".active ? (" + gv + ") : ";
        }
        z += "0";
        a += "1";
        an += "0";
        im.zoom = z;
        if (any_a) im.alpha = a;
        if (any_anim) {
          im.has_anim = true;
          im.anim_gate = an;
        }
        rb.push_back(im);
      }
    }

    if (any_spiral) {
      RenderStmt spiral;
      spiral.op = RenderStmt::Op::Spiral;
      rb.push_back(spiral);
    }

    if (any_word) {
      RenderStmt t;
      t.op = RenderStmt::Op::Text;
      t.origin = "0.75";
      t.zoom = "0.75";
      rb.push_back(t);
    }
    if (any_sub) {
      RenderStmt s;
      s.op = RenderStmt::Op::Subtext;
      s.alpha = "0.25";
      s.origin = "0.375";
      rb.push_back(s);
    }
    if (any_caption) {
      RenderStmt c;
      c.op = RenderStmt::Op::SmallText;
      c.alpha = "0.2";
      c.origin = "0.5";
      rb.push_back(c);
    }
    return rb;
  }

  // ---- parser ---------------------------------------------------------------
  class Parser
  {
  public:
    explicit Parser(const std::string& src) : _c(src), _src(src) {}

    void parse_pattern(patternv2::ParseResult& out)
    {
      expect_word("pattern");
      out.name = _c.word();
      uint32_t reps = 1;
      if (_c.peek_word() == "repeat") {
        _c.word();
        reps = _c.uint_lit();
      }
      _c.expect('{');
      std::vector<Node> phases;
      while (_c.peek_char() != '}') {
        phases.push_back(parse_phase());
      }
      _c.expect('}');
      if (phases.empty()) {
        throw ParseError{"a pattern needs at least one phase", _c.pos()};
      }

      Node body = phases.size() == 1 ? std::move(phases[0])
                                     : group(Node::Type::Seq, std::move(phases));
      if (reps > 1) {
        body = repeat(reps, std::move(body));
      }
      Node init = action(1, {effect(Effect::Kind::Themes), effect(Effect::Kind::Font),
                             effect(Effect::Kind::SpiralNew)});
      std::vector<Node> rootKids;
      rootKids.push_back(std::move(init));
      rootKids.push_back(std::move(body));
      out.root = group(Node::Type::One, std::move(rootKids));
      out.render_block =
          build_render_block(_layers, _phase_count, _any_word, _any_sub, _any_caption, _any_spiral);
      out.warnings = std::move(_warnings);
    }

  private:
    void expect_word(const char* w)
    {
      const std::size_t at = _c.pos();
      const std::string got = _c.word();
      if (got != w) {
        throw ParseError{std::string("expected '") + w + "', got '" + got + "'", at};
      }
    }

    Node parse_phase()
    {
      const std::size_t at = _c.pos();
      const std::string kw = _c.word();
      if (kw != "phase" && kw != "escalate" && kw != "deepen") {
        throw ParseError{"expected phase|escalate|deepen, got '" + kw + "'", at};
      }
      const std::string label = _c.string_lit();
      expect_word("for");
      uint32_t length = 0;  // 0 = auto
      if (_c.peek_word() == "auto") {
        _c.word();
      } else {
        length = _c.uint_lit();
        _c.expect('f');
      }
      _c.expect('{');
      if (_c.peek_word() == "description") {
        _c.word();
        _c.string_lit();
      }
      _curves.clear();
      while (_c.peek_word() == "curve") {
        parse_curve();
      }

      ++_phase_count;
      const std::size_t layer_start = _layers.size();

      std::vector<Node> streams;
      while (_c.peek_char() != '}') {
        streams.push_back(parse_statement(length, label));
      }
      _c.expect('}');
      if (streams.empty()) {
        throw ParseError{"phase '" + label + "' has no content", _c.pos()};
      }

      // Per-layer opacity ladder (1, 1/2, 1/3, ...) when a phase composites several layers
      // and the author didn't set explicit brightness -- super_parallel's stack.
      const std::size_t n = _layers.size() - layer_start;
      if (n > 1) {
        for (std::size_t k = 0; k < n; ++k) {
          Layer& l = _layers[layer_start + k];
          if (l.alpha.empty()) {
            l.alpha = "1 / " + std::to_string(k + 1);
          }
        }
      }

      Node phase = group(Node::Type::Par, std::move(streams));
      phase.id = label;
      phase.phase = label;
      return phase;
    }

    // A theme reference: `theme N` (index), or the aliases concept/reward/runtime.
    // theme 0 = concept = primary, theme 1 = reward = alternate. theme N>=2 needs the
    // ThemeBank runtime extension (3+ simultaneous live themes) and is rejected here --
    // the grammar surface is ready; the runtime is its own project (spec §7 Extension #1).
    Slot parse_theme()
    {
      const std::size_t at = _c.pos();
      const std::string w = _c.word();
      if (w == "theme") {
        const std::size_t nat = _c.pos();
        const uint32_t n = _c.uint_lit();
        if (n == 0) return Slot::Primary;
        if (n == 1) return Slot::Alternate;
        throw ParseError{"theme " + std::to_string(n) +
                             ": 3+ simultaneous themes need the ThemeBank runtime extension "
                             "(only theme 0/1 are supported today)",
                         nat};
      }
      return theme_to_slot(w, at);
    }

    void parse_curve()
    {
      expect_word("curve");
      const std::string name = _c.word();
      expect_word("from");
      Curve cv;
      cv.from = _c.number_lit();
      expect_word("to");
      cv.to = _c.number_lit();
      if (_c.peek_word() == "ease") {
        _c.word();
        const std::size_t eat = _c.pos();
        cv.ease = _c.word();
        // Only `late` (the accelerate pow6 dwell) is implemented; `linear` is uniform.
        // Reject the rest loudly rather than silently degrading the ramp to flat.
        if (cv.ease != "late" && cv.ease != "linear") {
          throw ParseError{"ease '" + cv.ease +
                               "' not implemented (use 'late' for a front-loaded dwell, or omit "
                               "for linear)",
                           eat};
        }
      }
      _curves[name] = cv;
    }

    // Resolve `over <anchor>` to a clock node-id: section->phase, pattern/root->root,
    // flash->the owning leaf. The three-clock model as one fixed anchor set.
    std::string parse_anchor(const std::string& clock_id, const std::string& phase_id)
    {
      const std::size_t at = _c.pos();
      const std::string w = _c.word();
      if (w == "section") return phase_id;
      if (w == "pattern" || w == "root") return "root";
      if (w == "flash") return clock_id;
      throw ParseError{"unknown clock anchor '" + w + "' (want section|pattern|flash)", at};
    }

    // Substitute the author-facing `self` token (the owning flash) with the real leaf id.
    static std::string subst_self(std::string e, const std::string& clock_id)
    {
      std::string out;
      for (std::size_t i = 0; i < e.size();) {
        const bool boundary_before = (i == 0) || !(std::isalnum((unsigned char)e[i - 1]) || e[i - 1] == '_');
        if (boundary_before && e.compare(i, 4, "self") == 0 &&
            (i + 4 >= e.size() || !(std::isalnum((unsigned char)e[i + 4]) || e[i + 4] == '_'))) {
          out += clock_id;
          i += 4;
        } else {
          out += e[i++];
        }
      }
      return out;
    }

    // `(zoom|brightness|origin|alpha) value` (repeatable), where value is:
    //   <number> [fade in|out|inout|hold] [over <anchor>]   -- a per-clock ramp (or `hold` = const)
    //   <curve>                                              -- a per-frame progress expr (section)
    //   [<raw expr>]                                         -- the escape hatch (reads <node>.attr, `self`)
    void parse_image_attrs(const std::string& clock_id, const std::string& phase_id, Layer& layer)
    {
      for (;;) {
        const std::string a = _c.peek_word();
        if (a != "zoom" && a != "brightness" && a != "origin" && a != "alpha") {
          break;
        }
        _c.word();
        std::string expr;
        if (_c.next_is_bracket()) {
          expr = subst_self(_c.bracket_expr(), clock_id);
        } else if (_c.next_is_digit()) {
          const float v = _c.number_lit();
          int fade = 0;  // 0=in (0->V), 1=out (V->0), 2=inout (triangle), 3=hold (constant V)
          std::string clock = clock_id;
          for (;;) {  // fade direction and over-anchor in any order
            if (_c.peek_word() == "fade") {
              _c.word();
              const std::size_t dat = _c.pos();
              const std::string dir = _c.word();
              if (dir == "out") fade = 1;
              else if (dir == "inout") fade = 2;
              else if (dir == "hold") fade = 3;
              else if (dir != "in") throw ParseError{"expected fade in|out|inout|hold", dat};
            } else if (_c.peek_word() == "hold") {
              _c.word();
              fade = 3;  // constant V (the explicit non-ramp form)
            } else if (_c.peek_word() == "over") {
              _c.word();
              clock = parse_anchor(clock_id, phase_id);
            } else {
              break;
            }
          }
          if (fade == 3) expr = fnum(v);
          else if (fade == 2) expr = fnum(v) + " * (1 - abs(2 * " + clock + ".progress - 1))";
          else if (fade == 1) expr = fnum(v) + " * (1 - " + clock + ".progress)";
          else expr = fnum(v) + " * " + clock + ".progress";
        } else {
          const std::size_t cat = _c.pos();
          const std::string cname = _c.word();
          auto it = _curves.find(cname);
          if (it == _curves.end()) throw ParseError{"unknown curve '" + cname + "'", cat};
          const Curve& cv = it->second;
          expr = "(" + fnum(cv.from) + " + " + fnum(cv.to - cv.from) + " * " + phase_id +
                 ".progress)";
        }
        if (a == "zoom") layer.zoom = expr;
        else if (a == "origin") layer.origin = expr;
        else layer.alpha = expr;  // brightness or alpha
      }
    }

    void parse_anim(Node& leaf, Slot slot, Layer& layer)
    {
      if (_c.peek_word() != "anim") {
        return;
      }
      _c.word();
      layer.anim = true;
      if (_c.peek_word() == "every") {
        _c.word();
        const uint32_t period = _c.uint_lit();
        const std::string suf = _c.peek_word();
        if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th") {
          _c.word();
        }
        const std::string ctr = "_actr" + std::to_string(_node_counter);
        const std::string flag = "_anim" + std::to_string(_node_counter++);
        Effect pulse = effect(Effect::Kind::Pulse);
        pulse.target = ctr;
        pulse.mod_literal = static_cast<int32_t>(period);
        pulse.flag = flag;
        Effect anim = effect(Effect::Kind::Anim);
        anim.slot = slot;
        anim.guard = Effect::Guard::Truthy;
        anim.guard_reg = flag;
        leaf.effects.push_back(pulse);
        leaf.effects.push_back(anim);
        layer.anim_gate = flag;
      } else {
        Effect anim = effect(Effect::Kind::Anim);
        anim.slot = slot;
        leaf.effects.push_back(anim);
      }
    }

    // `chance p` / `chance(p)`: re-roll each fire; gate this effect on a 1-in-n hit.
    // Returns the roll effect to prepend (kind None if no chance present).
    Effect parse_chance(Effect& gated)
    {
      Effect none = effect(Effect::Kind::Set);
      none.target = "";  // sentinel: empty target = "no roll"
      if (_c.peek_word() != "chance") {
        return none;
      }
      _c.word();
      const bool paren = _c.accept('(');
      const float p = _c.number_lit();
      if (paren) {
        _c.expect(')');
      }
      // 100-bucket roll: round(100*p) ones among 100 entries, so 0.25 / 0.4 / 0.9 are
      // distinct (a 1-in-round(1/p) scheme would collapse 0.4 / 0.5 / 0.6 all to 50%).
      uint32_t ones = static_cast<uint32_t>(std::lround(100.0 * double(p)));
      if (ones < 1) ones = 1;
      if (ones > 99) ones = 99;
      const std::string creg = "_chance" + std::to_string(_node_counter++);
      Effect roll = effect(Effect::Kind::Roll);
      roll.target = creg;
      for (uint32_t k = 0; k < 100; ++k) {
        roll.choices.push_back(k < ones ? 1 : 0);
      }
      gated.guard = Effect::Guard::Ge;
      gated.guard_reg = creg;
      gated.guard_value = 1;
      return roll;
    }

    Node parse_statement(uint32_t phase_length, const std::string& phase_id)
    {
      const std::size_t at = _c.pos();
      const std::string kw = _c.word();

      if (kw == "spiral") {
        _any_spiral = true;
        if (_c.peek_word() == "locked") {
          const std::size_t lat = _c.pos();
          _c.word();
          throw ParseError{"spiral locked: entrainment period unavailable -- needs the "
                           "entrainment runtime hook (Extension #2)",
                           lat};
        }
        float rate = 0.f;  // bare `spiral` is allowed; default rate
        if (_c.peek_word() == "rate") {
          _c.word();
          rate = _c.number_lit();
        }
        Effect e = effect(Effect::Kind::SpiralRot);
        e.rate = rate;
        return action(1, {e});
      }

      Effect::Kind kind;
      bool is_image = false;
      if (kw == "image") {
        kind = Effect::Kind::Image;
        is_image = true;
      } else if (kw == "word") {
        kind = Effect::Kind::Text;
        _any_word = true;
      } else if (kw == "caption") {
        kind = Effect::Kind::SmallSub;
        _any_caption = true;
      } else if (kw == "subtext") {
        kind = Effect::Kind::Subtext;
        _any_sub = true;
      } else {
        throw ParseError{"unknown statement '" + kw + "'", at};
      }

      const Slot slot = parse_theme();

      Effect e = effect(kind);
      e.slot = slot;
      if (kind == Effect::Kind::SmallSub) {
        e.force = true;
      }

      // Optional `-> REG` (image layer register; default "current").
      std::string reg = "current";
      if (is_image && _c.accept('-')) {
        _c.expect('>');
        reg = _c.word();
        e.target = reg;
      }

      expect_word("every");

      // `every locked` -- entrainment-synced cadence. Reserved; needs the runtime hook.
      if (_c.peek_word() == "locked") {
        const std::size_t lat = _c.pos();
        _c.word();
        throw ParseError{"every locked: entrainment period unavailable -- needs the entrainment "
                         "runtime hook (Extension #2)",
                         lat};
      }

      // ---- ramped cadence (`every <curve>`) ----
      if (!_c.next_is_digit()) {
        const std::size_t cat = _c.pos();
        const std::string cname = _c.word();
        auto it = _curves.find(cname);
        if (it == _curves.end()) {
          throw ParseError{"unknown curve '" + cname + "' after every", cat};
        }
        std::string ramp_id;
        Node seq = build_ramp(it->second, e, slot, ramp_id);
        if (is_image) {
          Layer layer;
          layer.phase_id = phase_id;
          layer.reg = reg;
          parse_image_attrs(ramp_id, phase_id, layer);
          if (_c.peek_word() == "anim") {
            throw ParseError{"anim on a ramped cadence is not supported yet", _c.pos()};
          }
          _layers.push_back(layer);
        }
        // Non-image ramps (e.g. a ramping subtext cadence -- sub_text's sub_speed) just
        // fire their effect at the ramped interval; no render layer.
        return seq;
      }

      // ---- fixed cadence ----
      const std::size_t every_at = _c.pos();
      const uint32_t every = _c.uint_lit();
      if (every == 0) {
        throw ParseError{"'every 0' is not a valid beat", every_at};
      }
      if (phase_length != 0 && phase_length % every != 0) {
        // Warn, don't reject (spec §5.1 / §9 decision #5): floor division keeps the whole
        // beats; the stream just ends one partial interval early. Allowing this is what lets
        // authors write deliberate polyrhythms.
        _warnings.push_back(_loc(every_at) + ": beat " + std::to_string(every) +
                            " does not divide phase length " + std::to_string(phase_length) +
                            " -- the stream ends " + std::to_string(phase_length % every) +
                            " frames early");
      }

      // `stagger K` (phase-offset this stream).
      uint32_t stagger = 0;
      if (_c.peek_word() == "stagger") {
        _c.word();
        stagger = _c.uint_lit();
      }

      // `chance p`: prepend a roll, guard the content effect.
      std::vector<Effect> effs;
      Effect roll = parse_chance(e);
      if (!roll.target.empty()) {
        effs.push_back(roll);
      }
      effs.push_back(e);

      Node leaf = action(every, std::move(effs));
      Layer layer;
      layer.phase_id = phase_id;
      layer.reg = reg;
      if (is_image) {
        leaf.image_slot = slot;
        const std::string clock_id = "_img" + std::to_string(_node_counter++);
        leaf.id = clock_id;
        parse_image_attrs(clock_id, phase_id, layer);
        parse_anim(leaf, slot, layer);
        _layers.push_back(layer);
      }

      Node node = (phase_length == 0) ? std::move(leaf)
                                      : repeat(phase_length / every, std::move(leaf));
      // Only a real, non-zero, non-full-period shift mints an Offset (stagger 0 would
      // pre-advance a whole period).
      if (stagger != 0) {
        node = offset(stagger, std::move(node));
      }
      return node;
    }

    Node build_ramp(const Curve& c, const Effect& eff, Slot slot, std::string& clock_id_out)
    {
      const int from = static_cast<int>(std::lround(c.from));
      const int to = static_cast<int>(std::lround(c.to));
      const int step = from <= to ? 1 : -1;
      std::vector<Node> segs;
      for (int L = from;; L += step) {
        const uint32_t cnt = ease_count(c, L);
        Node leaf = action(static_cast<uint32_t>(L < 1 ? 1 : L), {eff});
        leaf.image_slot = slot;
        segs.push_back(repeat(cnt, std::move(leaf)));
        if (L == to) {
          break;
        }
      }
      Node seq = group(Node::Type::Seq, std::move(segs));
      clock_id_out = "_ramp" + std::to_string(_node_counter++);
      seq.id = clock_id_out;
      return seq;
    }

    std::string _loc(std::size_t pos) const
    {
      std::size_t line = 1, col = 1;
      for (std::size_t i = 0; i < pos && i < _src.size(); ++i) {
        if (_src[i] == '\n') {
          ++line;
          col = 1;
        } else {
          ++col;
        }
      }
      return std::to_string(line) + ":" + std::to_string(col);
    }

    Cursor _c;
    const std::string& _src;
    std::vector<Layer> _layers;
    std::map<std::string, Curve> _curves;
    uint32_t _node_counter = 0;
    uint32_t _phase_count = 0;
    bool _any_spiral = false;
    bool _any_word = false;
    bool _any_sub = false;
    bool _any_caption = false;
    std::vector<std::string> _warnings;
  };

  std::string locate(const std::string& src, std::size_t pos)
  {
    std::size_t line = 1, col = 1;
    for (std::size_t i = 0; i < pos && i < src.size(); ++i) {
      if (src[i] == '\n') {
        ++line;
        col = 1;
      } else {
        ++col;
      }
    }
    return std::to_string(line) + ":" + std::to_string(col);
  }
}

namespace patternv2
{
  ParseResult parse(const std::string& source)
  {
    ParseResult out;
    try {
      Parser p(source);
      p.parse_pattern(out);
      out.ok = true;
    } catch (const ParseError& e) {
      out.ok = false;
      out.error = locate(source, e.pos) + ": " + e.message;
    } catch (const std::exception& e) {
      out.ok = false;
      out.error = std::string("internal: ") + e.what();
    }
    return out;
  }
}
