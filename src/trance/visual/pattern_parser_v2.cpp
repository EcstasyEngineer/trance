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
  // unrolled into a ramp of segments) or an attribute (`zoom <curve>`, a per-frame
  // progress expression). The single time-bending primitive.
  struct Curve
  {
    float from = 0.f;
    float to = 0.f;
    std::string ease;  // "" / "linear" / "late" (front-loaded dwell)
  };

  // What one phase contributes to the generated render block.
  struct PhaseRender
  {
    std::string id;
    bool has_image = false;
    std::string image_zoom;   // full [expr] text, or empty
    std::string image_alpha;  // full [expr] text, or empty
    bool has_word = false;
    bool has_subtext = false;
    bool has_caption = false;
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

  // Per-segment repeat count for a ramped cadence. "late" front-loads the dwell toward
  // the fast (larger-length) end -- the accelerate feel: it lingers as it speeds up.
  uint32_t ease_count(const Curve& c, int L)
  {
    if (c.ease == "late") {
      const double base = c.from > c.to ? c.from : c.to;
      const double d = std::fabs(double(c.from) - double(L));
      const double denom = std::pow(base, 5.0);
      return 1u + (denom > 0.0 ? static_cast<uint32_t>(std::pow(d, 6.0) / denom) : 0u);
    }
    return 1u;  // linear / default: one pass per length
  }

  std::string fnum(float v) { return std::to_string(v); }

  // ---- render-block generation ----------------------------------------------
  std::vector<RenderStmt> build_render_block(const std::vector<PhaseRender>& phases)
  {
    std::vector<RenderStmt> rb;

    std::vector<const PhaseRender*> img;
    bool any_word = false, any_sub = false, any_caption = false;
    for (const auto& p : phases) {
      if (p.has_image) img.push_back(&p);
      any_word = any_word || p.has_word;
      any_sub = any_sub || p.has_subtext;
      any_caption = any_caption || p.has_caption;
    }

    if (!img.empty()) {
      RenderStmt image;
      image.op = RenderStmt::Op::Image;
      image.image_reg = "current";
      if (img.size() == 1 && phases.size() == 1) {
        image.zoom = img[0]->image_zoom;
        image.alpha = img[0]->image_alpha;
      } else {
        std::string zexpr, aexpr;
        bool any_alpha = false;
        for (const auto* p : img) {
          zexpr += p->id + ".active ? (" + p->image_zoom + ") : ";
          const std::string a = p->image_alpha.empty() ? "1" : p->image_alpha;
          if (!p->image_alpha.empty()) any_alpha = true;
          aexpr += p->id + ".active ? (" + a + ") : ";
        }
        zexpr += "0";
        aexpr += "1";
        image.zoom = zexpr;
        if (any_alpha) image.alpha = aexpr;
      }
      rb.push_back(image);
    }

    RenderStmt spiral;
    spiral.op = RenderStmt::Op::Spiral;
    rb.push_back(spiral);

    if (any_word) {
      RenderStmt text;
      text.op = RenderStmt::Op::Text;
      text.origin = "0.75";
      text.zoom = "0.75";
      rb.push_back(text);
    }
    if (any_sub) {
      RenderStmt sub;
      sub.op = RenderStmt::Op::Subtext;
      sub.alpha = "0.25";
      sub.origin = "0.375";
      rb.push_back(sub);
    }
    if (any_caption) {
      RenderStmt cap;
      cap.op = RenderStmt::Op::SmallText;
      cap.alpha = "0.2";
      cap.origin = "0.5";
      rb.push_back(cap);
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
      out.render_block = build_render_block(_phases_render);
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
      const std::string kw = _c.word();  // phase | escalate | deepen
      if (kw != "phase" && kw != "escalate" && kw != "deepen") {
        throw ParseError{"expected phase|escalate|deepen, got '" + kw + "'", at};
      }
      const std::string label = _c.string_lit();
      expect_word("for");
      // `for auto` = length is intrinsic (a cadence ramp decides it); else `for N f`.
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

      PhaseRender pr;
      pr.id = label;

      std::vector<Node> streams;
      while (_c.peek_char() != '}') {
        streams.push_back(parse_statement(length, label, pr));
      }
      _c.expect('}');
      if (streams.empty()) {
        throw ParseError{"phase '" + label + "' has no content", _c.pos()};
      }

      _phases_render.push_back(pr);

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

    // Parse `[zoom <val>] [brightness <val>]` after a stream's cadence, where <val> is a
    // number (per-flash unless `over section`), or a curve name (a progress expression
    // over the phase). Fills the image's render exprs; clock_id is the flash/ramp node.
    void parse_image_attrs(const std::string& clock_id, const std::string& phase_id,
                           PhaseRender& pr)
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
          // value of the curve at the phase's progress: A + (B-A) * phase.progress
          expr = "(" + fnum(cv.from) + " + " + fnum(cv.to - cv.from) + " * " + phase_id +
                 ".progress)";
        }
        if (attr == "zoom") {
          pr.image_zoom = expr;
        } else {
          pr.image_alpha = expr;
        }
      }
    }

    Node parse_statement(uint32_t phase_length, const std::string& phase_id, PhaseRender& pr)
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
        pr.has_word = true;
      } else if (kw == "caption") {
        kind = Effect::Kind::SmallSub;
        pr.has_caption = true;
      } else if (kw == "subtext") {
        kind = Effect::Kind::Subtext;
        pr.has_subtext = true;
      } else {
        throw ParseError{"unknown statement '" + kw + "'", at};
      }

      const std::size_t theme_at = _c.pos();
      const Slot slot = theme_to_slot(_c.word(), theme_at);
      expect_word("every");

      Effect e = effect(kind);
      e.slot = slot;
      if (kind == Effect::Kind::SmallSub) {
        e.force = true;
      }

      // Cadence: a curve name (ramp) or an integer (fixed beat).
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
        pr.has_image = true;
        if (pr.image_zoom.empty()) {
          pr.image_zoom = "0.5 * " + ramp_id + ".progress";  // continuous over the ramp
        }
        parse_image_attrs(ramp_id, phase_id, pr);
        return seq;
      }

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

      Node leaf = action(every, {e});
      std::string clock_id;
      if (is_image) {
        leaf.image_slot = slot;
        clock_id = "_img" + std::to_string(_node_counter++);
        leaf.id = clock_id;
        pr.has_image = true;
        if (pr.image_zoom.empty()) {
          pr.image_zoom = "0.5 * " + clock_id + ".progress";
        }
      }
      if (is_image) {
        parse_image_attrs(clock_id, phase_id, pr);
      }
      // A fixed beat in an auto phase repeats via the enclosing Par; otherwise it fills the
      // declared length with an explicit Repeat.
      if (phase_length == 0) {
        return leaf;
      }
      return repeat(phase_length / every, std::move(leaf));
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
    std::vector<PhaseRender> _phases_render;
    std::map<std::string, Curve> _curves;
    uint32_t _node_counter = 0;
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
