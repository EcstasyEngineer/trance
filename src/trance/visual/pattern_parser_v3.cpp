#include <trance/visual/pattern_parser_v3.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// v3 grammar lowering. See docs/spec-grammar-v3.md. The parser turns the two-nouns/one-rule
// surface into the existing pattern::Node tree + RenderStmt block: patterns nest onto
// Seq/Par/Rep cyclers, draws become Image/Text effects + render statements, modulators become
// render [expr] strings reading a pattern's minted clock id, and registers are lexically
// pattern-scoped via compile-time name qualification.
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
    // Rewind/jump to a previously-recorded position. Used by the ramp cadence (13.2) to
    // re-parse one captured body span once per sampled segment, instead of duplicating the
    // statement-parsing switch or deep-copying an AST.
    void seek(std::size_t p) { _i = p; }
    // Consume exactly one character (after skipping whitespace/comments), whatever it is.
    // Used only to scan for a body's matching '}' without tokenizing its contents.
    void advance_one()
    {
      skip();
      if (_i < _src.size()) ++_i;
    }

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

      // Top-level wrapper: an init Action (themes/font/spiral_new) then the pattern body,
      // so theme cycling + spiral creation happen once up front.
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

    // Append a render statement, gated by the enclosing pattern's `.active` so that in a `seq`
    // only the running sub-pattern's draws paint (in a `par` every child is active together, so
    // the gate is a harmless always-true). This is what keeps sequenced phases from overdrawing.
    void push_render(RenderStmt rs)
    {
      if (!_regs.empty()) {
        const std::string gate = _regs.back().cid + ".active";
        rs.when = rs.when.empty() ? gate : ("(" + rs.when + ") and " + gate);
      }
      _render.push_back(std::move(rs));
    }

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

    std::string display_reg(const std::string& qreg) const
    {
      const auto p = qreg.find('$');
      return p == std::string::npos ? qreg : qreg.substr(p + 1);
    }

    Slot hint_slot(const Effect& e) const
    {
      if (!e.slot_reg.empty() || e.slot == Slot::Runtime) {
        return Slot::Runtime;
      }
      return e.slot;
    }

    void apply_image_hint(Node& n, const std::vector<Effect>& effects) const
    {
      Slot hint = Slot::None;
      std::string label;
      uint32_t image_effects = 0;
      for (const auto& e : effects) {
        if (e.kind != Effect::Kind::Image) {
          continue;
        }
        ++image_effects;
        const Slot s = hint_slot(e);
        if (hint == Slot::None) {
          hint = s;
          label = display_reg(e.target);
        } else if (hint != s) {
          hint = Slot::Runtime;
          label = "img";
        }
      }
      if (hint != Slot::None) {
        n.image_slot = hint;
        n.image_label = image_effects == 1 && !label.empty() ? label : "img";
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
          if (ease != "linear" && ease != "late" && ease != "early")
            throw ParseError{"unknown ease '" + ease + "' (only linear|late|early)", eat};
        }
        const std::string clk = parse_over();
        // linear: A + (B-A)*p ; late: dwell at the START value ~ A + (B-A)*p^3 ;
        // early: the mirror image, rush off the start and dwell at the END value ~
        // A + (B-A)*(1-(1-p)^3). Cubic on both sides: for a once-per-sample ramp this
        // reproduces the original accelerate's time-at-fast distribution (d^6-repeat
        // curve: ~25% of runtime at <=16f cuts, ~38% at <=20f) -- quadratic gave 16%.
        const std::string p = (ease == "late") ? ("(" + clk + ".progress ^ 3)")
            : (ease == "early")               ? ("(1 - ((1 - " + clk + ".progress) ^ 3))")
                                              : (clk + ".progress");
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
      if (kw == "burst") {
        children.push_back(parse_burst(span));
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
        push_render(rs);
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
        push_render(rs);
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
      if (kw == "audio") {
        parse_audio(sink);
        return;
      }
      // Standalone `anim <content>`: switch which animation the streamer plays, without
      // pulling an image or drawing anything -- one-shot setup (e.g. a burst `enter`
      // block picks the burst's animation once, instead of re-rolling it every period).
      if (kw == "anim") {
        _c.word();
        const std::size_t cat = _c.pos();
        Effect e = effect(Effect::Kind::Anim);
        e.slot = content_to_slot(_c.word(), cat);
        sink.push_back(e);
        return;
      }
      if (kw == "image" || kw == "word" || kw == "caption" || kw == "subtext" || kw == "draw") {
        parse_draw(span, sink);
        return;
      }
      throw ParseError{"unknown statement '" + kw + "'", at};
    }

    // `audio <content> [loop] [volume <modulator>]` / `audio stop` -- PRECANNED theme audio
    // only (no TTS, ever). Two nouns still apply: `content` is the same bi-thematic
    // vocabulary as `image`/`word` (concept/reward/runtime); `volume` is an ordinary
    // modulator, not a bespoke keyword class. Lowers to Effect{Kind::Audio} (or AudioStop
    // for `audio stop`); a literal volume sets Effect::rate (fired once), a curve/[expr]
    // volume instead emits a RenderStmt{Op::AudioVolume} that rides the enclosing pattern's
    // clock every frame, exactly like `spiral speed`. Single-slot v0 (docs/audio.md): a
    // second `audio` fire replaces whatever grammar audio was already playing, the same
    // shape as the single live text slot.
    void parse_audio(std::vector<Effect>& sink)
    {
      expect_word("audio");
      if (_c.peek_word() == "stop") {
        _c.word();
        sink.push_back(effect(Effect::Kind::AudioStop));
        return;
      }
      const std::size_t cat = _c.pos();
      const std::string content = _c.word();
      const Slot slot = content_to_slot(content, cat);

      Effect e = effect(Effect::Kind::Audio);
      e.slot = slot;
      // Sentinel: rate < 0 means "no literal volume written" -- keep whatever volume is
      // in effect. Distinct from an explicit `volume 0`, which is a real mute (rate=0).
      e.rate = -1.f;

      if (_c.peek_word() == "loop") {
        _c.word();
        e.force = true;
      }

      if (_c.peek_word() == "volume") {
        _c.word();
        const std::size_t vat = _c.pos();
        const std::string mod = parse_modulator();
        float literal = 0.f;
        if (is_bare_literal(mod, literal)) {
          // A constant volume needs no per-frame render machinery: set it once at fire
          // time (Effect::rate), same "constant folds to a fire-time value" shape §4.3
          // documents for the shared curve-drive param class.
          e.rate = literal;
        } else {
          RenderStmt rs;
          rs.op = RenderStmt::Op::AudioVolume;
          rs.speed = mod;
          push_render(rs);
        }
        (void)vat;
      }

      sink.push_back(e);
    }

    // Does `s` parse as exactly one bare floating-point literal (what parse_modulator's
    // literal branch emits via fnum(), e.g. "0.500000")? Used to fold a constant `volume`
    // modulator to a fire-time Effect field instead of a per-frame RenderStmt -- a curve/
    // [expr] modulator always contains non-numeric characters (a clock id, an operator, a
    // paren) and falls through to the per-frame path.
    static bool is_bare_literal(const std::string& s, float& out)
    {
      if (s.empty()) return false;
      char* end = nullptr;
      out = std::strtof(s.c_str(), &end);
      return end == s.c_str() + s.size();
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

      // `draw REG [params] [anim]`: draw an existing register without pulling a new
      // image. Trailing `anim` renders the streamer's animation layer instead of the
      // still (pure render -- no change-animation effect; pair with a standalone
      // `anim <content>` statement, e.g. in a burst `enter` block, to pick WHICH).
      if (kw == "draw") {
        const std::size_t rat = _c.pos();
        const std::string ref = read_reg();
        const std::string qreg = qualify_reg(ref, rat);
        _read.push_back({qreg, rat});
        RenderStmt rs;
        rs.op = RenderStmt::Op::Image;
        rs.image_reg = qreg;
        parse_params(rs);
        if (_c.peek_word() == "anim") {
          _c.word();
          rs.has_anim = true;
        }
        push_render(rs);
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
        RenderStmt rs;
        rs.op = RenderStmt::Op::Image;
        rs.image_reg = qreg;
        parse_params(rs);
        std::vector<Effect> extra;  // anim load / anim-gate pulse
        parse_anim(rs, slot, extra);
        Effect roll = parse_chance(e);
        if (!roll.target.empty()) sink.push_back(roll);
        sink.push_back(e);
        for (auto& x : extra) sink.push_back(x);
        push_render(rs);
        return;
      }

      // word / caption / subtext: the text path. (Text content is a single live slot, not a
      // register, so text cannot crossfade/stash today -- that is the one deferred runtime
      // extension; see docs/spec-grammar-v3.md Ext#4.)
      Effect::Kind ek = kw == "word" ? Effect::Kind::Text
                        : kw == "subtext" ? Effect::Kind::Subtext
                                          : Effect::Kind::SmallSub;
      Effect e = effect(ek);
      e.slot = slot;
      if (kw == "caption") e.force = true;
      RenderStmt rs;
      rs.op = kw == "word" ? RenderStmt::Op::Text
              : kw == "subtext" ? RenderStmt::Op::Subtext
                                : RenderStmt::Op::SmallText;
      if (kw == "word") {
        rs.origin = "0.75";
        rs.zoom = "0.75";
      } else if (kw == "subtext") {
        rs.alpha = "0.25";
        rs.origin = "0.375";
      } else {
        rs.alpha = "0.2";
        rs.origin = "0.5";
      }
      // Allow params (origin/zoom/alpha) to override the defaults, then an optional `chance`.
      parse_params(rs);
      Effect roll = parse_chance(e);
      if (!roll.target.empty()) {
        sink.push_back(roll);
        rs.when = e.guard_reg + " >= 1";  // draw the text only on a chance hit (it flashes)
      }
      sink.push_back(e);
      push_render(rs);
    }

    // `anim` (always animate) / `anim every Nth` (animate every Nth fire, gated by a pulse).
    // Adds the change-animation load (+ pulse) to `extra`; sets has_anim/anim_gate on the draw.
    void parse_anim(RenderStmt& rs, Slot slot, std::vector<Effect>& extra)
    {
      if (_c.peek_word() != "anim") return;
      _c.word();
      rs.has_anim = true;
      Effect load = effect(Effect::Kind::Anim);
      load.slot = slot;
      extra.push_back(load);
      if (_c.peek_word() == "every") {
        _c.word();
        const uint32_t n = _c.uint_lit();
        const std::string suf = _c.peek_word();
        if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th") _c.word();
        const std::string ctr = "_actr" + std::to_string(_idc);
        const std::string flag = "_anim" + std::to_string(_idc);
        ++_idc;
        Effect pulse = effect(Effect::Kind::Pulse);
        pulse.target = ctr;
        pulse.mod_literal = static_cast<int32_t>(n);
        pulse.flag = flag;
        extra.push_back(pulse);
        rs.anim_gate = flag;  // render animates only when the flag is set (every Nth)
      }
    }

    // `chance p` / `chance(p)`: a 100-bucket roll that gates the draw on a 1-in-... hit.
    // Returns the roll effect to prepend (empty target => no chance).
    Effect parse_chance(Effect& gated)
    {
      Effect none = effect(Effect::Kind::Set);
      none.target = "";
      if (_c.peek_word() != "chance") return none;
      _c.word();
      const bool paren = _c.accept('(');
      const float p = _c.number_lit();
      if (paren) _c.expect(')');
      uint32_t ones = static_cast<uint32_t>(std::lround(100.0 * double(p)));
      if (ones < 1) ones = 1;
      if (ones > 99) ones = 99;
      const std::string creg = "_chance" + std::to_string(_idc++);
      Effect roll = effect(Effect::Kind::Roll);
      roll.target = creg;
      for (uint32_t k = 0; k < 100; ++k) roll.choices.push_back(k < ones ? 1 : 0);
      gated.guard = Effect::Guard::Ge;
      gated.guard_reg = creg;
      gated.guard_value = 1;
      return roll;
    }

    // Sample N integer segment durations from A->B along an ease curve, compile-time only
    // (spec-grammar-v3.md 13.2 / Extension #3), SCALED so the segments sum to exactly `span`
    // (the invariant is "sum equals the span the ramp occupies" -- A/B set the ramp's SHAPE,
    // not an absolute frame budget of their own, same as a `curve A -> B` always normalizes
    // against a 0..1 clock regardless of the clock's length). Reuses the exact ease formulas
    // the `curve` modulator uses (linear / late == progress^3) so the shape matches what a
    // `curve A -> B` would render if you could ride it continuously. `steps` >= 2 and every
    // segment must be able to be >= 1f (checked by the caller before this runs).
    std::vector<uint32_t> sample_ramp(float a, float b, uint32_t steps, const std::string& ease,
                                      uint32_t span)
    {
      std::vector<double> raw(steps);
      double raw_sum = 0.0;
      for (uint32_t i = 0; i < steps; ++i) {
        // Sample at segment midpoints (i+0.5)/steps: matches how a `curve` reads a clock's
        // continuous progress rather than biasing toward either endpoint.
        const double p = (double(i) + 0.5) / double(steps);
        const double eased = (ease == "late") ? (p * p * p)
            : (ease == "early")              ? (1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p))
                                             : p;
        raw[i] = double(a) + (double(b) - double(a)) * eased;
        raw_sum += raw[i];
      }
      // Scale the raw curve so its sum is exactly `span` before rounding -- this is what keeps
      // the invariant "sum == span" true by construction rather than by a large after-the-fact
      // patch; only integer round-off remains to be folded in below.
      const double scale = raw_sum > 0.0 ? (double(span) / raw_sum) : 0.0;
      std::vector<uint32_t> out(steps);
      for (uint32_t i = 0; i < steps; ++i) {
        const long r = std::lround(raw[i] * scale);
        out[i] = r < 1 ? 1u : static_cast<uint32_t>(r);
      }
      // Fold the residual rounding error (at most a handful of frames, never the whole
      // curve-vs-span mismatch) one frame at a time into the tail segments, walking backward --
      // a minimal perturbation that keeps the sampled shape close to the ease curve instead of
      // dumping the whole remainder into a single segment (which could both blow past the last
      // sample's value and, if negative, violate the >=1f floor).
      const int64_t total = std::accumulate(out.begin(), out.end(), int64_t{0});
      int64_t diff = int64_t(span) - total;
      int32_t idx = static_cast<int32_t>(steps) - 1;
      // Bounded by construction: the caller already rejects span < steps, so the all-1f floor
      // can always absorb a negative diff down to total == steps <= span. The explicit budget
      // is a cheap belt-and-braces guard against ever hanging the parser, not a real limit.
      for (uint64_t budget = 2 * uint64_t(steps) + uint64_t(span) + 4; diff != 0 && budget > 0;
           --budget) {
        if (diff > 0) {
          ++out[idx];
          --diff;
        } else if (out[idx] > 1) {
          --out[idx];
          ++diff;
        }
        idx = idx > 0 ? idx - 1 : static_cast<int32_t>(steps) - 1;
      }
      return out;
    }

    // `every ramp A -> B steps N [ease linear|late] [-> NAME] { body }` -- compile-time sampled
    // ramp cadence (spec-grammar-v3.md 13.2, Extension #3). Samples N fixed-length segment
    // durations from A..B along the ease curve, then lowers to a plain Seq of N Action leaves
    // that each re-run the same body-effects/render-stmts, mirroring how `every Nf` builds its
    // per-beat leaf -- NO live/dynamic cycler length, no compiler/cycler change. Each segment
    // gets its own minted id (NAME_00, NAME_01, ... if named, else anonymous _nK ids) and its
    // own register-scope push so `push_render`'s existing `.active`-gate mechanism (the same one
    // that already isolates sibling nested patterns, see EX3) makes only the currently-firing
    // segment's draws paint -- this is what lets body modulators ride "the active segment's
    // clock" via a bare `this`/no-`over` even though NodeMap only maps one id to one Cycler*.
    // Called by parse_cadence AFTER it has consumed `every` and peeked (not consumed) `ramp`.
    Node parse_ramp_cadence(uint32_t span)
    {
      expect_word("ramp");
      const std::size_t aat = _c.pos();
      const float a = _c.number_lit();
      _c.expect('f');
      _c.expect('-');
      _c.expect('>');
      const float b = _c.number_lit();
      _c.expect('f');
      expect_word("steps");
      const std::size_t sat = _c.pos();
      const uint32_t steps = _c.uint_lit();
      if (steps < 2) throw ParseError{"ramp `steps` must be >= 2", sat};

      std::string ease = "linear";
      if (_c.peek_word() == "ease") {
        _c.word();
        const std::size_t eat = _c.pos();
        ease = _c.word();
        if (ease != "linear" && ease != "late" && ease != "early")
          throw ParseError{"unknown ease '" + ease + "' (only linear|late|early)", eat};
      }

      std::string clkname;
      if (_c.accept('-')) {
        _c.expect('>');
        clkname = _c.word();
      }

      if (span == 0) throw ParseError{"ramp cadence needs a bounded enclosing span", aat};
      if (span < steps)
        throw ParseError{"ramp span " + std::to_string(span) + "f can't fit " +
                             std::to_string(steps) + " segments of >=1f each",
                         aat};

      const std::vector<uint32_t> lens = sample_ramp(a, b, steps, ease, span);

      // Locate the body ONCE as a source span [body_open, body_close), then re-parse that exact
      // span once per segment by seeking `_c` back to body_open each time: each segment is its
      // own tiny clock+register scope (like a nested pattern) so `this`/bare modulators anchor
      // to that segment and `push_render` gates its draws by its own `.active`. Re-parsing the
      // captured span (rather than deep-copying an AST) reuses every existing statement-parsing
      // path (draw/copy/state/nested-pattern) with zero new code for effect kinds -- the cost is
      // O(steps) reparse of one small body, paid once at load time, not per frame.
      _c.expect('{');
      const std::size_t body_open = _c.pos();
      skip_balanced_braces();
      const std::size_t body_close = _c.pos() - 1;  // position of the closing '}'

      std::vector<Node> segs;
      segs.reserve(steps);
      for (uint32_t i = 0; i < steps; ++i) {
        const uint32_t len = lens[i];
        std::string segname;
        if (!clkname.empty()) {
          char buf[16];
          std::snprintf(buf, sizeof buf, "_%02u", i);
          segname = clkname + buf;
        }
        const std::string cid = new_id();
        _clocks.push_back({segname, cid});
        _regs.push_back({segname, cid});

        _c.seek(body_open);
        std::vector<Effect> leaf_effects;
        std::vector<Node> nested;
        for (;;) {
          // peek_char first: it skips whitespace/comments, so a body whose last
          // statement ends flush against trailing whitespace + '}' doesn't re-enter
          // parse_statement on the gap and die on an empty keyword.
          _c.peek_char();
          if (_c.pos() >= body_close) {
            break;
          }
          parse_statement(len, leaf_effects, nested);
        }

        _clocks.pop_back();
        _regs.pop_back();

        Node leaf = action(len, std::move(leaf_effects));
        apply_image_hint(leaf, leaf.effects);
        leaf.id = cid;
        if (nested.empty()) {
          segs.push_back(std::move(leaf));
        } else {
          std::vector<Node> par;
          par.push_back(std::move(leaf));
          for (auto& n : nested) par.push_back(std::move(n));
          segs.push_back(group(Node::Type::Par, std::move(par)));
        }
      }
      _c.seek(body_close);
      _c.expect('}');

      return group(Node::Type::Seq, std::move(segs));
    }

    // Skip from just after the body's opening '{' (already consumed by the caller) to just
    // after its matching '}', leaving `_c` positioned right after the close. The v3 grammar has
    // no string literals that could hide a brace, so a plain depth count over raw characters is
    // safe (comments are still skipped normally via Cursor::skip()).
    void skip_balanced_braces()
    {
      uint32_t depth = 1;
      for (;;) {
        const char ch = _c.peek_char();
        if (ch == '\0') throw ParseError{"unterminated ramp body (missing '}')", _c.pos()};
        if (ch == '{') { depth++; _c.advance_one(); }
        else if (ch == '}') {
          if (--depth == 0) { _c.advance_one(); return; }
          _c.advance_one();
        } else {
          _c.advance_one();
        }
      }
    }

    // `every LEN [-> NAME] { body }` -> Rep(span/LEN, Action(LEN, body-effects)). Opens a CLOCK
    // scope (so modulators inside ride the per-beat clock) but NOT a register scope.
    // Also dispatches `every ramp A -> B steps N ...` (13.2) to parse_ramp_cadence.
    Node parse_cadence(uint32_t span)
    {
      expect_word("every");
      if (_c.peek_word() == "ramp") return parse_ramp_cadence(span);
      const std::size_t lat = _c.pos();
      const uint32_t len = parse_len();
      if (len == 0) throw ParseError{"cadence length must be > 0", lat};
      std::string clkname;
      const std::string cid = new_id();
      if (_c.accept('-')) {
        _c.expect('>');
        clkname = _c.word();
      }
      // `offset Nf`: delay this lane's start by N frames (OffsetCycler) -- the staggered
      // parallel-lane idiom (e.g. super_parallel's three image layers 32f apart).
      uint32_t offset = 0;
      if (_c.peek_word() == "offset") {
        _c.word();
        offset = parse_len();
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
      apply_image_hint(leaf, leaf.effects);
      leaf.id = cid;
      Node node = (span != 0 && len != 0) ? repeat(span / len, std::move(leaf)) : std::move(leaf);
      if (offset) {
        Node off;
        off.type = Node::Type::Off;
        off.count = offset;
        off.children.push_back(std::move(node));
        node = std::move(off);
      }
      // Fold any nested-pattern subtrees beside the cadence leaf (run in parallel).
      if (!nested.empty()) {
        nested.insert(nested.begin(), std::move(node));
        return group(Node::Type::Par, std::move(nested));
      }
      return node;
    }

    // `burst [-> NAME] period Nf chance 1/K cooldown Nf duration Amin..Amax { base {..} burst {..} }`
    // -- surfaces the existing BurstCycler (spec-grammar-v3.md 13.1): a base loop
    // randomly interrupted by a bounded burst, then a cooldown. Lowers to exactly one
    // Node::Burst; `length` is the enclosing pattern's span (like `every`, no separate `length`
    // keyword -- the runtime field is filled from `span`, not restated by the author). `base` and
    // `burst` are each a tiny statement list (draws/state only, same statement grammar as a
    // cadence body); their effects go straight to Node::burst_effects / Node::effects, with no
    // separate clock/register scope of their own -- both sides fire on the SAME enclosing
    // pattern's clock, so a bare modulator inside either block still rides `this` untouched. The
    // optional `-> NAME` mints a stable node id so `NAME.index` (1 during a burst, else 0) is
    // readable from render exprs via the existing NodeMap/resolve_ident path -- no new plumbing.
    Node parse_burst(uint32_t span)
    {
      expect_word("burst");
      std::string clkname;
      const std::string cid = new_id();
      if (_c.accept('-')) {
        _c.expect('>');
        clkname = _c.word();
      }

      uint32_t period = 0, chance_den = 0, cooldown = 0, dur_min = 0, dur_max = 0;
      bool have_period = false;
      for (;;) {
        const std::string w = _c.peek_word();
        if (w == "period") {
          _c.word();
          const std::size_t pat = _c.pos();
          period = _c.uint_lit();
          _c.expect('f');
          if (period == 0) throw ParseError{"burst `period` must be > 0", pat};
          have_period = true;
        } else if (w == "chance") {
          _c.word();
          const std::size_t dat = _c.pos();
          _c.expect('1');
          _c.expect('/');
          chance_den = _c.uint_lit();
          if (chance_den == 0) throw ParseError{"burst `chance` denominator must be > 0", dat};
        } else if (w == "cooldown") {
          _c.word();
          cooldown = _c.uint_lit();
          _c.expect('f');
        } else if (w == "duration") {
          _c.word();
          const std::size_t dat = _c.pos();
          dur_min = _c.uint_lit();
          _c.expect('f');
          dur_max = dur_min;
          if (_c.accept('.')) {
            _c.expect('.');
            dur_max = _c.uint_lit();
            _c.expect('f');
          }
          if (dur_max < dur_min)
            throw ParseError{"burst `duration` max is below min", dat};
        } else {
          break;
        }
      }
      if (!have_period) throw ParseError{"burst needs a `period`", _c.pos()};

      // The surface authors cooldown/duration in FRAMES (`cooldown 64f`), but
      // BurstCycler counts PERIOD TICKS (it only steps its FSM on period boundaries).
      // Convert here -- round up, minimum one tick for a non-zero value -- so the
      // runtime honors the authored units: `cooldown 64f` at `period 8f` is 8 ticks,
      // not 64 ticks (512 frames), which is how the first shipped version behaved.
      auto to_ticks = [&](uint32_t frames) -> uint32_t {
        if (frames == 0) return 0;
        const uint32_t ticks = (frames + period - 1) / period;
        return ticks ? ticks : 1;
      };
      cooldown = to_ticks(cooldown);
      dur_min = to_ticks(dur_min);
      dur_max = to_ticks(dur_max);

      _clocks.push_back({clkname, cid});

      std::vector<Effect> base_effects, burst_effects, enter_effects;
      std::vector<Node> nested;  // `pattern`/`every` inside base/burst blocks, run alongside
      _c.expect('{');
      bool saw_base = false, saw_burst = false;
      while (_c.peek_char() != '}') {
        const std::size_t bat = _c.pos();
        const std::string bw = _c.word();
        std::vector<Effect>& target = bw == "base"    ? (saw_base = true, base_effects)
                                      : bw == "burst" ? (saw_burst = true, burst_effects)
                                      : bw == "enter"
                                          ? enter_effects
                                          : throw ParseError{
                                                "expected 'base', 'burst' or 'enter' block", bat};
        const std::size_t render_before = _render.size();
        _c.expect('{');
        while (_c.peek_char() != '}') {
          parse_statement(span, target, nested);
        }
        _c.expect('}');
        // Gate this block's draws on the burst FSM's state (NAME.index: 1 during a
        // burst, else 0), so base draws stop painting during a burst and vice versa.
        // Without this, a burst-block `image ... anim` paints its animation over the
        // base cuts EVERY frame of the whole pattern (push_render's pattern-active gate
        // alone can't tell the two blocks apart). `enter` draws count as burst-side.
        const std::string gate = cid + (bw == "base" ? ".index == 0" : ".index >= 1");
        for (std::size_t ri = render_before; ri < _render.size(); ++ri) {
          auto& rs = _render[ri];
          rs.when = rs.when.empty() ? gate : ("(" + rs.when + ") and " + gate);
        }
      }
      _c.expect('}');
      if (!saw_base && !saw_burst)
        _warnings.push_back(loc(_c.pos()) + ": burst has neither a `base` nor a `burst` block");

      _clocks.pop_back();

      Node n;
      n.type = Node::Type::Burst;
      n.id = cid;
      n.length = span;
      n.burst_period = period;
      n.burst_chance_den = chance_den;
      n.burst_cooldown = cooldown;
      n.burst_dur_min = dur_min;
      n.burst_dur_max = dur_max;
      n.effects = std::move(base_effects);
      n.burst_effects = std::move(burst_effects);
      n.burst_enter_effects = std::move(enter_effects);
      // One combined hint across base+burst (apply_image_hint recomputes from scratch each
      // call, so two separate calls would let the second silently clobber the first instead of
      // merging -- pass both lists' effects together like any other single-effects-list node).
      std::vector<Effect> both = n.effects;
      both.insert(both.end(), n.burst_effects.begin(), n.burst_effects.end());
      apply_image_hint(n, both);

      // Wrap in a no-op Rep(1, ...), same convention `parse_cadence` uses: an enclosing pattern
      // with exactly one child collapses onto it and stamps ITS OWN id over the child's, which
      // would silently clobber the burst's minted id (and strand any `over NAME`/`this` expr
      // already baked into this body's render strings, pointing at an id nothing registers).
      // Keeping the id'd Burst node one level below an unid'd wrapper is what lets the
      // collapse's overwrite land somewhere harmless instead.
      Node wrapped = repeat(1, std::move(n));
      if (nested.empty()) {
        return wrapped;
      }
      std::vector<Node> par;
      par.push_back(std::move(wrapped));
      for (auto& nn : nested) par.push_back(std::move(nn));
      return group(Node::Type::Par, std::move(par));
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
        apply_image_hint(a, a.effects);
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
