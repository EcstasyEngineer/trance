#include <trance/visual/pattern_parser_v3.h>

#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// v3 grammar lowering. See docs/spec-grammar-v3.md. The parser turns the two-nouns/one-rule
// surface into the existing pattern::Node tree + RenderStmt block, with ZERO runtime change in
// Phase 1: patterns nest onto Seq/Par/Rep cyclers, draws become Image/Text effects + render
// statements, modulators become render [expr] strings reading a pattern's minted clock id, and
// registers are lexically pattern-scoped via compile-time name qualification.
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

  // ---- tokenizer (shared shape with v2) -------------------------------------
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
      while (_i < _src.size() &&
             (std::isalnum(static_cast<unsigned char>(_src[_i])) || _src[_i] == '_')) {
        ++_i;
      }
      if (_i == start) throw ParseError{"expected a word", start};
      return _src.substr(start, _i - start);
    }
    std::string string_lit()
    {
      skip();
      if (_i >= _src.size() || _src[_i] != '"') throw ParseError{"expected a quoted string", _i};
      const std::size_t start = ++_i;
      while (_i < _src.size() && _src[_i] != '"') ++_i;
      if (_i >= _src.size()) throw ParseError{"unterminated string", start};
      std::string s = _src.substr(start, _i - start);
      ++_i;
      return s;
    }
    uint32_t uint_lit()
    {
      skip();
      const std::size_t start = _i;
      while (_i < _src.size() && std::isdigit(static_cast<unsigned char>(_src[_i]))) ++_i;
      if (_i == start) throw ParseError{"expected an integer", start};
      return static_cast<uint32_t>(std::stoul(_src.substr(start, _i - start)));
    }
    float number_lit()
    {
      skip();
      const std::size_t start = _i;
      bool neg = false;
      if (_i < _src.size() && _src[_i] == '-') {
        neg = true;
        ++_i;
      }
      const std::size_t ds = _i;
      while (_i < _src.size() &&
             (std::isdigit(static_cast<unsigned char>(_src[_i])) || _src[_i] == '.')) {
        ++_i;
      }
      if (_i == ds) throw ParseError{"expected a number", start};
      float v = std::stof(_src.substr(ds, _i - ds));
      return neg ? -v : v;
    }
    void expect(char c)
    {
      skip();
      if (_i >= _src.size() || _src[_i] != c) throw ParseError{std::string("expected '") + c + "'", _i};
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
      while (j < _src.size() &&
             (std::isalnum(static_cast<unsigned char>(_src[j])) || _src[j] == '_')) {
        ++j;
      }
      return _src.substr(_i, j - _i);
    }
    bool next_is_digit()
    {
      skip();
      return _i < _src.size() &&
             (std::isdigit(static_cast<unsigned char>(_src[_i])) ||
              (_src[_i] == '-' && _i + 1 < _src.size() &&
               std::isdigit(static_cast<unsigned char>(_src[_i + 1]))));
    }
    bool next_is_bracket()
    {
      skip();
      return _i < _src.size() && _src[_i] == '[';
    }
    std::string bracket_expr()
    {
      skip();
      if (_i >= _src.size() || _src[_i] != '[') throw ParseError{"expected '['", _i};
      const std::size_t start = ++_i;
      while (_i < _src.size() && _src[_i] != ']') ++_i;
      if (_i >= _src.size()) throw ParseError{"unterminated [expr]", start};
      std::string s = _src.substr(start, _i - start);
      ++_i;
      return s;
    }

  private:
    void skip()
    {
      for (;;) {
        while (_i < _src.size() && std::isspace(static_cast<unsigned char>(_src[_i]))) ++_i;
        if (_i < _src.size() && _src[_i] == '#') {
          while (_i < _src.size() && _src[_i] != '\n') ++_i;
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
  std::string fnum(float v) { return std::to_string(v); }

  Slot content_to_slot(const std::string& w, std::size_t at)
  {
    if (w == "concept") return Slot::Primary;
    if (w == "reward") return Slot::Alternate;
    if (w == "runtime") return Slot::Runtime;
    throw ParseError{"unknown content '" + w + "' (want concept|reward|runtime)", at};
  }

  // ---- parser ---------------------------------------------------------------
  class Parser
  {
  public:
    Parser(const std::string& src, uint32_t locked) : _c(src), _src(src), _locked(locked) {}

    void parse(patternv3::ParseResult& out)
    {
      Node body = parse_pattern(/*top=*/true);
      out.name = _root_name;

      // Top-level wrapper: an init Action (themes/font/spiral_new) then the pattern body, exactly
      // as the v1/v2 front-ends do, so theme cycling + spiral creation happen once up front.
      Node init = action(1, {effect(Effect::Kind::Themes), effect(Effect::Kind::Font),
                             effect(Effect::Kind::SpiralNew)});
      Node root = group(Node::Type::One, {std::move(init), std::move(body)});

      check_register_resolution();

      out.root = std::move(root);
      out.render_block = std::move(_render);
      out.warnings = std::move(_warnings);
      out.ok = true;
    }

  private:
    Cursor _c;
    const std::string& _src;
    uint32_t _locked;
    std::vector<RenderStmt> _render;
    std::vector<std::string> _warnings;
    uint32_t _idc = 0;
    std::string _root_name;

    // Clock scopes (patterns AND named cadences): `this`/`over NAME` resolve here.
    struct Scope { std::string name; std::string cid; };
    std::vector<Scope> _clocks;
    // Register scopes (patterns ONLY): bare reg names qualify against the top.
    std::vector<Scope> _regs;
    std::set<std::string> _written;  // qualified register names written (Image/Copy target)
    std::vector<std::pair<std::string, std::size_t>> _read;  // (qualified reg, pos) for the resolve check

    std::string loc(std::size_t pos) const
    {
      std::size_t line = 1, col = 1;
      for (std::size_t i = 0; i < pos && i < _src.size(); ++i) {
        if (_src[i] == '\n') { ++line; col = 1; } else { ++col; }
      }
      return std::to_string(line) + ":" + std::to_string(col);
    }
    std::string new_id() { return "_n" + std::to_string(_idc++); }

    void expect_word(const char* w)
    {
      const std::size_t at = _c.pos();
      const std::string got = _c.word();
      if (got != w) throw ParseError{std::string("expected '") + w + "', got '" + got + "'", at};
    }

    // The top clock id (`this`). Used when a modulator has no `over`.
    std::string this_clock() const { return _clocks.empty() ? std::string() : _clocks.back().cid; }

    // Resolve `over NAME` (or `this` when over_name empty) to a minted clock id.
    std::string resolve_clock(const std::string& over_name, std::size_t at)
    {
      if (over_name.empty()) return this_clock();
      for (auto it = _clocks.rbegin(); it != _clocks.rend(); ++it) {
        if (it->name == over_name) return it->cid;
      }
      throw ParseError{"`over " + over_name + "`: no enclosing pattern/clock named '" + over_name +
                           "' is in scope",
                       at};
    }

    // Qualify a register reference. Bare `name` -> "<enclosing-pattern-cid>$name". Qualified
    // "Pat.name" -> "<Pat-cid>$name" (Pat resolved in the register-scope stack). This is the
    // whole of the lexical-scoping mechanism (spec-grammar-v3.md 0.6) -- compile-time string
    // transformation, no runtime change.
    std::string qualify_reg(const std::string& ref, std::size_t at)
    {
      const auto dot = ref.find('.');
      if (dot == std::string::npos) {
        if (_regs.empty()) throw ParseError{"register '" + ref + "' outside any pattern", at};
        return _regs.back().cid + "$" + ref;
      }
      const std::string pat = ref.substr(0, dot);
      const std::string nm = ref.substr(dot + 1);
      for (auto it = _regs.rbegin(); it != _regs.rend(); ++it) {
        if (it->name == pat) return it->cid + "$" + nm;
      }
      throw ParseError{"qualified register '" + ref + "': no pattern named '" + pat + "' in scope",
                       at};
    }

    void check_register_resolution()
    {
      for (const auto& r : _read) {
        if (!_written.count(r.first)) {
          // A register drawn/copied-from but never written anywhere would silently render an
          // empty image (the render_eval silent-zero footgun). Fail loud instead.
          _warnings.push_back(loc(r.second) + ": register is read but never written (draws empty)");
        }
      }
    }

    // ---- lengths ----
    uint32_t parse_len()
    {
      const std::size_t at = _c.pos();
      if (_c.next_is_digit()) {
        const uint32_t n = _c.uint_lit();
        const std::string suf = _c.peek_word();
        if (suf == "f") { _c.word(); return n; }
        throw ParseError{"expected 'f' after frame count", _c.pos()};
      }
      const std::string w = _c.peek_word();
      if (w == "beats") {
        _c.word();
        const uint32_t n = _c.uint_lit();
        if (_locked == 0)
          throw ParseError{"`beats` needs a pulsed entrainment bed (none in this program)", at};
        return n * _locked;
      }
      if (w == "locked") {
        _c.word();
        if (_locked == 0)
          throw ParseError{"`locked` needs a pulsed entrainment bed (none in this program)", at};
        return _locked;
      }
      throw ParseError{"expected a length (Nf | beats N | locked)", at};
    }

    // ---- modulators ----
    // Returns a render [expr] string. Optionally paren-wrapped: `(curve A -> B over X)`.
    // A bare literal `0.5` is a CONSTANT; `curve A -> B` rides a clock; `[expr]` is raw.
    std::string parse_modulator()
    {
      const bool paren = _c.accept('(');
      std::string result;
      if (_c.next_is_bracket()) {
        result = subst_this(_c.bracket_expr());
      } else if (_c.peek_word() == "curve") {
        _c.word();
        const float a = _c.number_lit();
        _c.expect('-');
        _c.expect('>');
        const float b = _c.number_lit();
        std::string ease = "linear";
        if (_c.peek_word() == "ease") {
          _c.word();
          const std::size_t eat = _c.pos();
          ease = _c.word();
          if (ease != "linear" && ease != "late")
            throw ParseError{"unknown ease '" + ease + "' (only linear|late)", eat};
        }
        const std::string clk = parse_over();
        // linear: A + (B-A)*p ; late: front-loaded dwell ~ A + (B-A)*p^3 (render_eval has ^).
        const std::string p =
            (ease == "late") ? ("(" + clk + ".progress ^ 3)") : (clk + ".progress");
        result = "(" + fnum(a) + " + " + fnum(b - a) + " * " + p + ")";
      } else if (_c.next_is_digit()) {
        result = fnum(_c.number_lit());  // a literal modulator is a CONSTANT
      } else {
        throw ParseError{"expected a modulator (literal | curve A -> B | [expr])", _c.pos()};
      }
      if (paren) _c.expect(')');
      return result;
    }

    // A register reference: a bare name `cur` or a qualified `Other.reg`.
    std::string read_reg()
    {
      std::string s = _c.word();
      if (_c.peek_char() == '.') {
        _c.expect('.');
        s += "." + _c.word();
      }
      return s;
    }

    // Optional `over NAME`; returns the resolved clock id (this-clock if absent).
    std::string parse_over()
    {
      if (_c.peek_word() == "over") {
        _c.word();
        const std::size_t at = _c.pos();
        const std::string nm = _c.word();
        return resolve_clock(nm, at);
      }
      return this_clock();
    }

    std::string subst_this(const std::string& expr)
    {
      const std::string cid = this_clock();
      std::string out;
      std::size_t i = 0;
      while (i < expr.size()) {
        // Replace whole-word `this` and `self` with the current clock id.
        if ((expr.compare(i, 4, "this") == 0 &&
             (i + 4 >= expr.size() || !std::isalnum(static_cast<unsigned char>(expr[i + 4])))) ||
            (expr.compare(i, 4, "self") == 0 &&
             (i + 4 >= expr.size() || !std::isalnum(static_cast<unsigned char>(expr[i + 4]))))) {
          out += cid;
          i += 4;
        } else {
          out += expr[i++];
        }
      }
      return out;
    }

    // ---- draw / drive / state parameters on a draw ----
    void parse_params(RenderStmt& rs)
    {
      for (;;) {
        const std::string a = _c.peek_word();
        if (a == "zoom") {
          _c.word();
          rs.zoom = parse_modulator();
        } else if (a == "origin") {
          _c.word();
          rs.origin = parse_modulator();
        } else if (a == "alpha" || a == "brightness") {
          _c.word();
          rs.alpha = parse_modulator();
        } else if (a == "fade") {
          _c.word();
          const std::size_t at = _c.pos();
          const std::string dir = _c.word();
          const std::string clk = this_clock();
          if (dir == "in") rs.alpha = clk + ".progress";
          else if (dir == "out") rs.alpha = "(1 - " + clk + ".progress)";
          else if (dir == "inout") rs.alpha = "(1 - abs(2 * " + clk + ".progress - 1))";
          else throw ParseError{"expected fade in|out|inout", at};
        } else {
          break;
        }
      }
    }

    // Parse one statement inside a pattern/cadence body. Schedule effects (image pulls, copies)
    // are appended to `sink`; nested schedule subtrees are appended to `children`; draws also
    // append a RenderStmt to _render. `span` is the enclosing length (for bare draws fired once).
    void parse_statement(uint32_t span, std::vector<Effect>& sink, std::vector<Node>& children)
    {
      const std::size_t at = _c.pos();
      const std::string kw = _c.peek_word();

      if (kw == "pattern") {
        children.push_back(parse_pattern(false));
        return;
      }
      if (kw == "every") {
        children.push_back(parse_cadence(span));
        return;
      }
      if (kw == "look") {
        parse_look(sink);
        return;
      }
      if (kw == "warp" || kw == "drunk") {
        _c.word();
        RenderStmt rs;
        rs.op = RenderStmt::Op::Warp;
        rs.origin = "0.15";  // default wavelength
        rs.speed = "2";      // default speed
        if (kw == "drunk") {
          // sugar: drunk <intensity> == warp amplitude <intensity> (defaults for the rest).
          rs.zoom = parse_modulator();
        } else {
          // warp (amplitude MOD | wavelength MOD | speed MOD)* -- the multi-param form.
          for (;;) {
            const std::string p = _c.peek_word();
            if (p == "amplitude") { _c.word(); rs.zoom = parse_modulator(); }
            else if (p == "wavelength") { _c.word(); rs.origin = parse_modulator(); }
            else if (p == "speed") { _c.word(); rs.speed = parse_modulator(); }
            else break;
          }
        }
        _render.push_back(rs);
        return;
      }
      if (kw == "spiral") {
        _c.word();
        RenderStmt rs;
        rs.op = RenderStmt::Op::Spiral;
        // Spiral SPEED is a curve-drivable render param (constant or curve), advanced per frame
        // by render_eval -- the same modulator class as zoom/fade. Shape/width/color are settings
        // (the `look {}` header), not animated.
        if (_c.peek_word() == "speed") {
          _c.word();
          rs.speed = parse_modulator();
        }
        _render.push_back(rs);
        return;
      }
      if (kw == "copy") {
        _c.word();
        const std::size_t sat = _c.pos();
        const std::string s = read_reg();
        _c.expect('-');
        _c.expect('>');
        const std::size_t dat = _c.pos();
        const std::string d = read_reg();
        Effect e = effect(Effect::Kind::Copy);
        e.src = qualify_reg(s, sat);
        e.target = qualify_reg(d, dat);
        _read.push_back({e.src, sat});
        _written.insert(e.target);
        sink.push_back(e);
        return;
      }
      if (kw == "image" || kw == "word" || kw == "caption" || kw == "draw") {
        parse_draw(span, sink);
        return;
      }
      throw ParseError{"unknown statement '" + kw + "'", at};
    }

    // `look { spiral type=N width=W }` -- SETTINGS, not per-frame: a deterministic SpiralSet that
    // pins the spiral shape/width (replacing change_spiral's random roll). Fires once.
    void parse_look(std::vector<Effect>& sink)
    {
      expect_word("look");
      _c.expect('{');
      while (_c.peek_char() != '}') {
        const std::size_t at = _c.pos();
        const std::string w = _c.word();
        if (w == "spiral") {
          Effect e = effect(Effect::Kind::SpiralSet);
          for (;;) {
            const std::string p = _c.peek_word();
            if (p != "type" && p != "width") break;
            _c.word();
            _c.expect('=');
            const uint32_t v = _c.uint_lit();
            if (p == "type") e.ivalue = static_cast<int32_t>(v);
            else e.mod_literal = static_cast<int32_t>(v);
          }
          sink.push_back(e);
        } else {
          throw ParseError{"unknown look property '" + w + "'", at};
        }
      }
      _c.expect('}');
    }

    void parse_draw(uint32_t /*span*/, std::vector<Effect>& sink)
    {
      const std::size_t at = _c.pos();
      const std::string kw = _c.word();

      // `draw REG`: draw an existing register without pulling a new image.
      if (kw == "draw") {
        const std::size_t rat = _c.pos();
        const std::string ref = read_reg();
        const std::string qreg = qualify_reg(ref, rat);
        _read.push_back({qreg, rat});
        RenderStmt rs;
        rs.op = RenderStmt::Op::Image;
        rs.image_reg = qreg;
        parse_params(rs);
        _render.push_back(rs);
        return;
      }

      const std::size_t cat = _c.pos();
      const std::string content = _c.word();
      const Slot slot = content_to_slot(content, cat);

      if (kw == "image") {
        std::string reg = "cur";
        if (_c.accept('-')) {
          _c.expect('>');
          reg = read_reg();
        }
        const std::string qreg = qualify_reg(reg, at);
        Effect e = effect(Effect::Kind::Image);
        e.slot = slot;
        e.target = qreg;
        _written.insert(qreg);
        sink.push_back(e);
        RenderStmt rs;
        rs.op = RenderStmt::Op::Image;
        rs.image_reg = qreg;
        parse_params(rs);
        _render.push_back(rs);
        return;
      }

      // word / caption: text path (no register in Phase 1; Phase 4 adds a text register + alpha).
      Effect e = effect(kw == "word" ? Effect::Kind::Text : Effect::Kind::SmallSub);
      e.slot = slot;
      if (kw == "caption") e.force = true;
      sink.push_back(e);
      RenderStmt rs;
      rs.op = (kw == "word") ? RenderStmt::Op::Text : RenderStmt::Op::SmallText;
      if (kw == "word") {
        rs.origin = "0.75";
        rs.zoom = "0.75";
      } else {
        rs.alpha = "0.2";
        rs.origin = "0.5";
      }
      // Allow params (origin/zoom) to override the defaults.
      parse_params(rs);
      _render.push_back(rs);
    }

    // `every LEN [-> NAME] { body }` -> Rep(span/LEN, Action(LEN, body-effects)). Opens a CLOCK
    // scope (so modulators inside ride the per-beat clock) but NOT a register scope.
    Node parse_cadence(uint32_t span)
    {
      expect_word("every");
      const std::size_t lat = _c.pos();
      const uint32_t len = parse_len();
      if (len == 0) throw ParseError{"cadence length must be > 0", lat};
      std::string clkname;
      const std::string cid = new_id();
      if (_c.accept('-')) {
        _c.expect('>');
        clkname = _c.word();
      }
      if (span != 0 && span % len != 0) {
        _warnings.push_back(loc(lat) + ": cadence " + std::to_string(len) +
                            " does not divide span " + std::to_string(span));
      }
      _clocks.push_back({clkname, cid});
      std::vector<Effect> leaf_effects;
      std::vector<Node> nested;  // nested patterns inside a cadence are uncommon; supported anyway
      _c.expect('{');
      while (_c.peek_char() != '}') {
        parse_statement(len, leaf_effects, nested);
      }
      _c.expect('}');
      _clocks.pop_back();

      Node leaf = action(len, std::move(leaf_effects));
      leaf.id = cid;
      Node node = (span != 0 && len != 0) ? repeat(span / len, std::move(leaf)) : std::move(leaf);
      // Fold any nested-pattern subtrees beside the cadence leaf (run in parallel).
      if (!nested.empty()) {
        nested.insert(nested.begin(), std::move(node));
        return group(Node::Type::Par, std::move(nested));
      }
      return node;
    }

    // A full `pattern NAME for LEN [seq|loop N] { body }`.
    Node parse_pattern(bool top)
    {
      expect_word("pattern");
      const std::size_t nat = _c.pos();
      const std::string name = _c.word();
      if (top) _root_name = name;
      expect_word("for");
      const uint32_t len = parse_len();

      bool seq = false;
      uint32_t loops = 1;
      for (;;) {
        const std::string w = _c.peek_word();
        if (w == "seq") { _c.word(); seq = true; }
        else if (w == "loop") { _c.word(); loops = _c.uint_lit(); }
        else break;
      }
      if (len == 0) throw ParseError{"pattern length must be > 0", nat};

      const std::string cid = new_id();
      _clocks.push_back({name, cid});
      _regs.push_back({name, cid});

      _c.expect('{');
      std::vector<Node> children;
      std::vector<Effect> bare;  // bare draws/state fire once over the pattern length
      while (_c.peek_char() != '}') {
        parse_statement(len, bare, children);
      }
      _c.expect('}');

      _clocks.pop_back();
      _regs.pop_back();

      if (!bare.empty()) {
        Node a = action(len, std::move(bare));
        children.push_back(std::move(a));
      }
      if (children.empty()) {
        // An empty pattern still needs a body that spans LEN so its clock is well-defined.
        children.push_back(action(len));
      }

      Node body = (children.size() == 1) ? std::move(children.front())
                                         : group(seq ? Node::Type::Seq : Node::Type::Par,
                                                 std::move(children));
      body.id = cid;       // the pattern clock (per-iteration when looped)
      body.phase = name;   // overlay label
      if (loops > 1) return repeat(loops, std::move(body));
      return body;
    }
  };
}

namespace patternv3
{
  ParseResult parse(const std::string& source, uint32_t locked_period_frames)
  {
    ParseResult out;
    try {
      Parser p(source, locked_period_frames);
      p.parse(out);
    } catch (const ParseError& e) {
      out.ok = false;
      std::size_t line = 1, col = 1;
      for (std::size_t i = 0; i < e.pos && i < source.size(); ++i) {
        if (source[i] == '\n') { ++line; col = 1; } else { ++col; }
      }
      out.error = std::to_string(line) + ":" + std::to_string(col) + ": " + e.message;
    } catch (const std::exception& e) {
      out.ok = false;
      out.error = std::string("internal: ") + e.what();
    }
    return out;
  }
}
