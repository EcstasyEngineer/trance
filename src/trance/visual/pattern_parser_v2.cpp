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
                                             bool any_word, bool any_sub, bool any_caption)
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

    for (const auto& reg : order) {
      const auto& g = groups[reg];
      RenderStmt im;
      im.op = RenderStmt::Op::Image;
      im.image_reg = reg;
      if (g.size() == 1 || phase_count == 1) {
        // One layer (or a single-phase pattern): draw each directly. For a single phase
        // with several layers sharing a reg we still take the first (degenerate).
        const Layer* l = g.front();
        im.zoom = l->zoom;
        im.alpha = l->alpha;
        if (l->anim) {
          im.has_anim = true;
          im.anim_gate = l->anim_gate;
        }
      } else {
        // Same register across phases: pick the active phase's params (slow_flash).
        std::string z, a;
        bool any_a = false;
        for (const auto* l : g) {
          z += l->phase_id + ".active ? (" + l->zoom + ") : ";
          const std::string av = l->alpha.empty() ? "1" : l->alpha;
          if (!l->alpha.empty()) any_a = true;
          a += l->phase_id + ".active ? (" + av + ") : ";
        }
        z += "0";
        a += "1";
        im.zoom = z;
        if (any_a) im.alpha = a;
      }
      rb.push_back(im);
    }

    RenderStmt spiral;
    spiral.op = RenderStmt::Op::Spiral;
    rb.push_back(spiral);

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
    explicit Parser(const std::string& src) : _c(src) {}

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
          build_render_block(_layers, _phase_count, _any_word, _any_sub, _any_caption);
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
        cv.ease = _c.word();
      }
      _curves[name] = cv;
    }

    // `zoom`/`brightness` <number [over section] | curve> on an image layer.
    void parse_image_attrs(const std::string& clock_id, const std::string& phase_id, Layer& layer)
    {
      while (_c.peek_word() == "zoom" || _c.peek_word() == "brightness") {
        const std::string attr = _c.word();
        std::string expr;
        if (_c.next_is_digit()) {
          const float v = _c.number_lit();
          std::string clock = clock_id;
          if (_c.peek_word() == "over") {
            _c.word();
            expect_word("section");
            clock = phase_id;
          }
          expr = fnum(v) + " * " + clock + ".progress";
        } else {
          const std::size_t cat = _c.pos();
          const std::string cname = _c.word();
          auto it = _curves.find(cname);
          if (it == _curves.end()) {
            throw ParseError{"unknown curve '" + cname + "'", cat};
          }
          const Curve& cv = it->second;
          expr = "(" + fnum(cv.from) + " + " + fnum(cv.to - cv.from) + " * " + phase_id +
                 ".progress)";
        }
        if (attr == "zoom") {
          layer.zoom = expr;
        } else {
          layer.alpha = expr;
        }
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
      uint32_t n = p > 0.f ? static_cast<uint32_t>(std::lround(1.0 / double(p))) : 2u;
      if (n < 2) {
        n = 2;
      }
      const std::string creg = "_chance" + std::to_string(_node_counter++);
      Effect roll = effect(Effect::Kind::Roll);
      roll.target = creg;
      roll.choices.push_back(1);
      for (uint32_t k = 1; k < n; ++k) {
        roll.choices.push_back(0);
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
        expect_word("rate");
        const float rate = _c.number_lit();
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

      const std::size_t theme_at = _c.pos();
      const Slot slot = theme_to_slot(_c.word(), theme_at);

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

      // ---- ramped cadence (image only) ----
      if (!_c.next_is_digit()) {
        const std::size_t cat = _c.pos();
        const std::string cname = _c.word();
        auto it = _curves.find(cname);
        if (it == _curves.end()) {
          throw ParseError{"unknown curve '" + cname + "' after every", cat};
        }
        if (!is_image) {
          throw ParseError{"ramped cadence is only supported on image streams (for now)", cat};
        }
        std::string ramp_id;
        Node seq = build_ramp(it->second, e, slot, ramp_id);
        Layer layer;
        layer.phase_id = phase_id;
        layer.reg = reg;
        layer.zoom = "0.5 * " + ramp_id + ".progress";
        parse_image_attrs(ramp_id, phase_id, layer);
        if (_c.peek_word() == "anim") {
          throw ParseError{"anim on a ramped cadence is not supported yet", _c.pos()};
        }
        _layers.push_back(layer);
        return seq;
      }

      // ---- fixed cadence ----
      const std::size_t every_at = _c.pos();
      const uint32_t every = _c.uint_lit();
      if (every == 0) {
        throw ParseError{"'every 0' is not a valid beat", every_at};
      }
      if (phase_length != 0 && phase_length % every != 0) {
        throw ParseError{"beat " + std::to_string(every) + " does not divide phase length " +
                             std::to_string(phase_length),
                         every_at};
      }

      // `stagger K` (phase-offset this stream).
      uint32_t stagger = 0;
      bool has_stagger = false;
      if (_c.peek_word() == "stagger") {
        _c.word();
        stagger = _c.uint_lit();
        has_stagger = true;
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
        layer.zoom = "0.5 * " + clock_id + ".progress";
        parse_image_attrs(clock_id, phase_id, layer);
        parse_anim(leaf, slot, layer);
        _layers.push_back(layer);
      }

      Node node = (phase_length == 0) ? std::move(leaf)
                                      : repeat(phase_length / every, std::move(leaf));
      if (has_stagger) {
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

    Cursor _c;
    std::vector<Layer> _layers;
    std::map<std::string, Curve> _curves;
    uint32_t _node_counter = 0;
    uint32_t _phase_count = 0;
    bool _any_word = false;
    bool _any_sub = false;
    bool _any_caption = false;
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
