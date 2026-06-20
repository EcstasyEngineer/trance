#include <trance/visual/pattern_parser.h>

#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
  struct Token {
    // Expr is the raw text inside a `[ ... ]` expression (evaluated where a number is
    // expected, with the current `generate` variable bindings).
    enum Kind { Ident, Number, String, Punct, Expr, End };
    Kind kind;
    std::string text;
    int line;
    int col;
  };

  struct ParseError {
    std::string msg;
    int line;
    int col;
  };

  // ---- lexer ----
  std::vector<Token> lex(const std::string& src)
  {
    std::vector<Token> toks;
    int line = 1;
    int col = 1;
    auto adv = [&](std::size_t& i) {
      if (src[i] == '\n') {
        ++line;
        col = 1;
      } else {
        ++col;
      }
      ++i;
    };
    for (std::size_t i = 0; i < src.size();) {
      char c = src[i];
      if (c == '#') {  // comment to end of line
        while (i < src.size() && src[i] != '\n') {
          adv(i);
        }
        continue;
      }
      if (std::isspace(static_cast<unsigned char>(c))) {
        adv(i);
        continue;
      }
      int tline = line;
      int tcol = col;
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string t;
        while (i < src.size()
               && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
          t += src[i];
          adv(i);
        }
        toks.push_back({Token::Ident, t, tline, tcol});
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < src.size()
                                                          && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
        std::string t;
        while (i < src.size()
               && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.')) {
          t += src[i];
          adv(i);
        }
        toks.push_back({Token::Number, t, tline, tcol});
        continue;
      }
      if (c == '"') {
        adv(i);
        std::string t;
        while (i < src.size() && src[i] != '"') {
          t += src[i];
          adv(i);
        }
        if (i >= src.size()) {
          throw ParseError{"unterminated string", tline, tcol};
        }
        adv(i);  // closing quote
        toks.push_back({Token::String, t, tline, tcol});
        continue;
      }
      if (c == '-' && i + 1 < src.size() && src[i + 1] == '>') {
        adv(i);
        adv(i);
        toks.push_back({Token::Punct, "->", tline, tcol});
        continue;
      }
      if (c == '=' && i + 1 < src.size() && src[i + 1] == '=') {
        adv(i);
        adv(i);
        toks.push_back({Token::Punct, "==", tline, tcol});
        continue;
      }
      if (c == '>' && i + 1 < src.size() && src[i + 1] == '=') {
        adv(i);
        adv(i);
        toks.push_back({Token::Punct, ">=", tline, tcol});
        continue;
      }
      if (c == '[') {  // [ expression ] -- captured raw, evaluated at use
        adv(i);
        std::string t;
        while (i < src.size() && src[i] != ']') {
          t += src[i];
          adv(i);
        }
        if (i >= src.size()) {
          throw ParseError{"unterminated [expression]", tline, tcol};
        }
        adv(i);  // closing ]
        toks.push_back({Token::Expr, t, tline, tcol});
        continue;
      }
      if (std::string("{}:,@").find(c) != std::string::npos) {
        adv(i);
        toks.push_back({Token::Punct, std::string(1, c), tline, tcol});
        continue;
      }
      throw ParseError{std::string("unexpected character '") + c + "'", tline, tcol};
    }
    toks.push_back({Token::End, "", line, col});
    return toks;
  }

  pattern::Slot slot_of(const Token& t)
  {
    if (t.text == "primary") return pattern::Slot::Primary;
    if (t.text == "alternate") return pattern::Slot::Alternate;
    if (t.text == "runtime") return pattern::Slot::Runtime;
    if (t.text == "random") return pattern::Slot::Runtime;  // resolved at fire time
    throw ParseError{"expected a slot (primary/alternate/runtime/random), got '" + t.text + "'",
                     t.line, t.col};
  }

  uint32_t split_of(const Token& t)
  {
    if (t.text == "word") return 0;
    if (t.text == "line") return 1;
    if (t.text == "word_gaps") return 2;
    if (t.text == "line_gaps") return 3;
    if (t.text == "once") return 4;
    throw ParseError{"expected a split type, got '" + t.text + "'", t.line, t.col};
  }

  // ---- expression evaluator for [ ... ] (used inside `generate`) ----
  // Grammar: expr = term {('+'|'-') term}; term = power {('*'|'/') power};
  //          power = factor ['^' power]; factor = number | ident | '(' expr ')' | '-' factor.
  // Bounded: no functions, no assignment. idents resolve to generate variables.
  struct ExprEval {
    const std::string& s;
    const std::map<std::string, double>& vars;
    std::size_t i = 0;
    int line, col;
    ExprEval(const std::string& s_, const std::map<std::string, double>& v_, int l, int c)
    : s(s_), vars(v_), line(l), col(c)
    {
    }
    [[noreturn]] void fail(const std::string& m)
    {
      throw ParseError{"in [expression]: " + m, line, col};
    }
    void ws()
    {
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    double run()
    {
      double v = expr();
      ws();
      if (i != s.size()) fail("trailing characters");
      return v;
    }
    double expr()
    {
      double v = term();
      for (ws(); i < s.size() && (s[i] == '+' || s[i] == '-'); ws()) {
        char op = s[i++];
        double r = term();
        v = op == '+' ? v + r : v - r;
      }
      return v;
    }
    double term()
    {
      double v = power();
      for (ws(); i < s.size() && (s[i] == '*' || s[i] == '/'); ws()) {
        char op = s[i++];
        double r = power();
        if (op == '/' && r == 0.0) fail("divide by zero");
        v = op == '*' ? v * r : v / r;
      }
      return v;
    }
    double power()
    {
      double b = factor();
      ws();
      if (i < s.size() && s[i] == '^') {
        ++i;
        return std::pow(b, power());
      }
      return b;
    }
    double factor()
    {
      ws();
      if (i >= s.size()) fail("unexpected end of expression");
      if (s[i] == '(') {
        ++i;
        double v = expr();
        ws();
        if (i >= s.size() || s[i] != ')') fail("expected ')'");
        ++i;
        return v;
      }
      if (s[i] == '-') {
        ++i;
        return -factor();
      }
      if (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.') {
        std::size_t st = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
        return std::stod(s.substr(st, i - st));
      }
      if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') {
        std::size_t st = i;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
        std::string id = s.substr(st, i - st);
        auto it = vars.find(id);
        if (it == vars.end()) fail("unknown variable '" + id + "'");
        return it->second;
      }
      fail("unexpected character in expression");
    }
  };

  // ---- parser ----
  class Parser
  {
  public:
    explicit Parser(std::vector<Token> toks) : _toks(std::move(toks))
    {
    }

    pattern::Parsed parse_pattern()
    {
      pattern::Parsed p;
      expect_ident("pattern");
      p.name = next_ident("a pattern name");
      expect_punct("{");
      for (;;) {
        const Token& t = peek();
        if (t.kind == Token::Ident && t.text == "weight") {
          ++_i;
          p.weight = next_uint("a weight");
        } else if (t.kind == Token::Ident && t.text == "render") {
          ++_i;
          if (peek().kind == Token::Punct && peek().text == "{") {
            p.render_block = parse_render_block();
          } else {
            p.render = next_ident("a render preset name");
          }
        } else {
          break;
        }
      }
      p.root = parse_node();
      expect_punct("}");
      if (peek().kind != Token::End) {
        err("trailing tokens after pattern body");
      }
      return p;
    }

  private:
    std::vector<Token> _toks;
    std::size_t _i = 0;
    std::map<std::string, double> _vars;  // active `generate` variable bindings

    const Token& peek() const
    {
      return _toks[_i];
    }
    [[noreturn]] void err(const std::string& m) const
    {
      throw ParseError{m, peek().line, peek().col};
    }
    void expect_punct(const std::string& p)
    {
      if (peek().kind != Token::Punct || peek().text != p) {
        err("expected '" + p + "'");
      }
      ++_i;
    }
    bool accept_punct(const std::string& p)
    {
      if (peek().kind == Token::Punct && peek().text == p) {
        ++_i;
        return true;
      }
      return false;
    }
    void expect_ident(const std::string& w)
    {
      if (peek().kind != Token::Ident || peek().text != w) {
        err("expected '" + w + "'");
      }
      ++_i;
    }
    std::string next_ident(const std::string& what)
    {
      if (peek().kind != Token::Ident) {
        err("expected " + what);
      }
      return _toks[_i++].text;
    }
    double next_number(const std::string& what)
    {
      const Token& t = peek();
      if (t.kind == Token::Expr) {
        ++_i;
        return ExprEval(t.text, _vars, t.line, t.col).run();
      }
      if (t.kind != Token::Number) {
        err("expected " + what);
      }
      try {
        return std::stod(_toks[_i++].text);
      } catch (...) {
        err("invalid number for " + what);
      }
    }
    uint32_t next_uint(const std::string& what)
    {
      const Token& t = peek();
      double v = next_number(what);
      if (v < 0) {
        throw ParseError{"expected a non-negative value for " + what, t.line, t.col};
      }
      // Integer contexts (lengths/counts) truncate, matching the integer arithmetic
      // the hardcoded patterns use (e.g. ACCELERATE's image_count division).
      return static_cast<uint32_t>(std::floor(v));
    }
    float next_float(const std::string& what)
    {
      return static_cast<float>(next_number(what));
    }

    // Raw text of a render param: a `[expr]` (kept unevaluated, run each frame by the
    // render evaluator) or a plain number literal. Unlike next_number, this does NOT
    // fold the value at parse time.
    std::string next_expr_text(const std::string& what)
    {
      const Token& t = peek();
      if (t.kind != Token::Expr && t.kind != Token::Number) {
        err("expected a number or [expr] for " + what);
      }
      return _toks[_i++].text;
    }

    pattern::Node parse_node()
    {
      // Prefixes: id "NAME", phase "NAME", and/or image <slot> [as <label>].
      std::string id;
      std::string phase;
      pattern::Slot image_slot = pattern::Slot::None;
      std::string image_label = "img";
      for (;;) {
        const Token& t = peek();
        if (t.kind == Token::Ident && t.text == "id") {
          ++_i;
          if (peek().kind != Token::String) {
            err("expected a quoted node id");
          }
          id = _toks[_i++].text;
        } else if (t.kind == Token::Ident && t.text == "phase") {
          ++_i;
          if (peek().kind != Token::String) {
            err("expected a quoted phase name");
          }
          phase = _toks[_i++].text;
        } else if (t.kind == Token::Ident && t.text == "image") {
          ++_i;
          image_slot = slot_of(_toks[_i]);
          ++_i;
          if (peek().kind == Token::Ident && peek().text == "as") {
            ++_i;
            if (peek().kind != Token::String) {
              err("expected a quoted image label after 'as'");
            }
            image_label = _toks[_i++].text;
          }
        } else {
          break;
        }
      }
      pattern::Node n = parse_primary();
      n.id = id;
      n.phase = phase;
      n.image_slot = image_slot;
      if (image_slot != pattern::Slot::None) {
        n.image_label = image_label;
      }
      return n;
    }

    // Index of the '}' matching the '{' at position `open` (which is the first token
    // INSIDE the braces, i.e. just past the opening '{').
    std::size_t matching_brace(std::size_t open)
    {
      int depth = 1;
      for (std::size_t k = open; k < _toks.size(); ++k) {
        if (_toks[k].kind == Token::Punct && _toks[k].text == "{") ++depth;
        else if (_toks[k].kind == Token::Punct && _toks[k].text == "}") {
          if (--depth == 0) return k;
        }
        if (_toks[k].kind == Token::End) break;
      }
      err("unterminated block");
    }

    // `generate VAR from A to B { template }` -- expand the template once per value of
    // VAR (A..B inclusive, step +/-1 inferred), re-parsing it with VAR bound so that
    // [expr]s resolve. The expanded nodes are appended to the enclosing block.
    void expand_generate(std::vector<pattern::Node>& kids)
    {
      ++_i;  // 'generate'
      std::string var = next_ident("a generate variable");
      expect_ident("from");
      long a = static_cast<long>(next_uint("the generate 'from' value"));
      expect_ident("to");
      long b = static_cast<long>(next_uint("the generate 'to' value"));
      expect_punct("{");
      std::size_t body = _i;
      std::size_t end = matching_brace(body);
      long step = a <= b ? 1 : -1;
      for (long v = a;; v += step) {
        _vars[var] = double(v);
        _i = body;
        while (_i < end) {
          if (peek().kind == Token::Ident && peek().text == "generate") {
            expand_generate(kids);
          } else {
            kids.push_back(parse_node());
          }
        }
        if (v == b) break;
      }
      _vars.erase(var);
      _i = end + 1;  // past '}'
    }

    std::vector<pattern::Node> parse_block()
    {
      expect_punct("{");
      std::vector<pattern::Node> kids;
      while (!(peek().kind == Token::Punct && peek().text == "}")) {
        if (peek().kind == Token::End) {
          err("unexpected end of input inside block");
        }
        if (peek().kind == Token::Ident && peek().text == "generate") {
          expand_generate(kids);
        } else {
          kids.push_back(parse_node());
        }
      }
      expect_punct("}");
      return kids;
    }

    // `render { <stmt>... }` -- the data-driven render block. Each statement is a draw
    // op + optional `when [cond]` + `:`-separated `key [expr]` params. Stored unevaluated
    // (RenderStmt) and run each frame by render_eval.cpp.
    std::vector<pattern::RenderStmt> parse_render_block()
    {
      expect_punct("{");
      std::vector<pattern::RenderStmt> stmts;
      while (!(peek().kind == Token::Punct && peek().text == "}")) {
        if (peek().kind == Token::End) {
          err("unexpected end of input inside render block");
        }
        stmts.push_back(parse_render_stmt());
      }
      expect_punct("}");
      if (stmts.empty()) {
        err("empty render block");
      }
      return stmts;
    }

    pattern::RenderStmt parse_render_stmt()
    {
      using Op = pattern::RenderStmt::Op;
      pattern::RenderStmt st;
      std::string w = next_ident("a render op (image/text/subtext/small_text/spiral)");
      if (w == "spiral") {
        st.op = Op::Spiral;
      } else if (w == "image") {
        st.op = Op::Image;
        st.image_reg = next_ident("an image register");
        if (peek().kind == Token::Ident && peek().text == "anim") {
          ++_i;
          st.has_anim = true;
          if (peek().kind == Token::Ident && peek().text == "if") {
            ++_i;
            st.anim_gate = next_expr_text("an anim gate [condition]");
          }
          if (peek().kind == Token::Ident && peek().text == "alt") {
            ++_i;
            st.anim_alt = next_expr_text("an anim alternate [condition]");
          }
        }
      } else if (w == "text") {
        st.op = Op::Text;
      } else if (w == "subtext") {
        st.op = Op::Subtext;
      } else if (w == "small_text") {
        st.op = Op::SmallText;
      } else {
        err("unknown render op '" + w + "'");
      }
      if (peek().kind == Token::Ident && peek().text == "when") {
        ++_i;
        st.when = next_expr_text("a when condition");
      }
      if (accept_punct(":")) {
        for (;;) {
          int kl = peek().line;
          int kc = peek().col;
          std::string key = next_ident("a render param name");
          std::string val = next_expr_text("a render param value");
          if (key == "alpha") {
            st.alpha = val;
          } else if (key == "origin") {
            st.origin = val;
          } else if (key == "zoom") {
            st.zoom = val;
          } else if (key == "shadow_origin") {
            st.shadow_origin = val;
          } else if (key == "shadow_zoom") {
            st.shadow_zoom = val;
          } else {
            throw ParseError{"unknown render param '" + key + "'", kl, kc};
          }
          if (!accept_punct(",")) {
            break;
          }
        }
      }
      return st;
    }

    pattern::Node parse_primary()
    {
      const Token& t = peek();
      if (t.kind != Token::Ident) {
        err("expected a node (every/timer/par/seq/one/repeat/offset)");
      }
      pattern::Node n;
      if (t.text == "every" || t.text == "timer") {
        bool has_effects = (t.text == "every");
        ++_i;
        n.type = pattern::Node::Type::Action;
        n.length = next_uint("a frame length");
        if (accept_punct("@")) {
          n.action_frame = next_uint("an action frame");
        }
        if (peek().kind == Token::Ident && peek().text == "divide") {
          ++_i;
          n.divide = next_uint("a divider");
          if (n.divide == 0) {
            err("divide must be >= 1");
          }
        }
        if (has_effects && accept_punct(":")) {
          n.effects = parse_effects();
        }
      } else if (t.text == "par" || t.text == "seq" || t.text == "one") {
        n.type = t.text == "par" ? pattern::Node::Type::Par
                                 : t.text == "seq" ? pattern::Node::Type::Seq
                                                   : pattern::Node::Type::One;
        ++_i;
        n.children = parse_block();
        if (n.children.empty()) {
          err("empty " + t.text + " group");
        }
      } else if (t.text == "repeat" || t.text == "offset") {
        bool is_repeat = t.text == "repeat";
        ++_i;
        n.type = is_repeat ? pattern::Node::Type::Rep : pattern::Node::Type::Off;
        n.count = next_uint(is_repeat ? "a repeat count" : "an offset");
        n.children.push_back(parse_node());
      } else if (t.text == "burst") {
        ++_i;
        n.type = pattern::Node::Type::Burst;
        parse_burst(n);
      } else {
        err("unknown node keyword '" + t.text + "'");
      }
      return n;
    }

    // burst { length L period P chance D cooldown C duration A B base: <fx> burst: <fx> }
    void parse_burst(pattern::Node& n)
    {
      expect_punct("{");
      for (;;) {
        const Token& t = peek();
        if (t.kind == Token::Punct && t.text == "}") {
          break;
        }
        if (t.kind != Token::Ident) {
          err("expected a burst field");
        }
        if (t.text == "base") {
          ++_i;
          expect_punct(":");
          n.effects = parse_effects();
        } else if (t.text == "burst") {
          ++_i;
          expect_punct(":");
          n.burst_effects = parse_effects();
        } else if (t.text == "length") {
          ++_i;
          n.length = next_uint("a burst length");
        } else if (t.text == "period") {
          ++_i;
          n.burst_period = next_uint("a burst period");
        } else if (t.text == "chance") {
          ++_i;
          n.burst_chance_den = next_uint("a burst chance denominator");
        } else if (t.text == "cooldown") {
          ++_i;
          n.burst_cooldown = next_uint("a burst cooldown");
        } else if (t.text == "duration") {
          ++_i;
          n.burst_dur_min = next_uint("a minimum burst duration");
          n.burst_dur_max = next_uint("a maximum burst duration");
        } else {
          err("unknown burst field '" + t.text + "'");
        }
      }
      expect_punct("}");
      if (!n.length || !n.burst_period) {
        err("burst needs a length and a period");
      }
    }

    std::vector<pattern::Effect> parse_effects()
    {
      std::vector<pattern::Effect> effects;
      for (;;) {
        effects.push_back(parse_effect());
        if (!accept_punct(",")) {
          break;
        }
      }
      return effects;
    }

    // A slot is either a static keyword (primary/alternate/runtime/random) or
    // `reg NAME`, meaning the alternate bool is read from a scalar register at fire
    // time (a toggle/flag used as a selector).
    void parse_slot(pattern::Effect& e)
    {
      if (peek().kind == Token::Ident && peek().text == "reg") {
        ++_i;
        if (peek().kind != Token::Ident) {
          err("expected a register name after 'reg'");
        }
        e.slot_reg = _toks[_i++].text;
      } else {
        e.slot = slot_of(_toks[_i++]);
      }
    }

    // Optional `when` guard, the language's only conditional: `when REG`,
    // `when REG == N`, or `when REG >= N`.
    void parse_guard(pattern::Effect& e)
    {
      using G = pattern::Effect::Guard;
      if (!(peek().kind == Token::Ident && peek().text == "when")) {
        return;
      }
      ++_i;
      if (peek().kind != Token::Ident) {
        err("expected a register name after 'when'");
      }
      e.guard_reg = _toks[_i++].text;
      if (accept_punct("==")) {
        e.guard = G::Eq;
        e.guard_value = static_cast<int32_t>(next_uint("a guard value"));
      } else if (accept_punct(">=")) {
        e.guard = G::Ge;
        e.guard_value = static_cast<int32_t>(next_uint("a guard value"));
      } else {
        e.guard = G::Truthy;
      }
    }

    pattern::Effect parse_effect()
    {
      using K = pattern::Effect::Kind;
      pattern::Effect e;
      std::string w = next_ident("an effect");
      if (w == "image") {
        e.kind = K::Image;
        parse_slot(e);
        if (accept_punct("->")) {
          e.target = next_ident("a register name");
        }
      } else if (w == "text") {
        e.kind = K::Text;
        e.split = split_of(_toks[_i++]);
        parse_slot(e);
      } else if (w == "anim") {
        e.kind = K::Anim;
        parse_slot(e);
      } else if (w == "subtext") {
        e.kind = K::Subtext;
        parse_slot(e);
      } else if (w == "small_text") {
        e.kind = K::SmallSub;
        parse_slot(e);
        if (peek().kind == Token::Ident && peek().text == "force") {
          ++_i;
          e.force = true;
        }
      } else if (w == "themes") {
        e.kind = K::Themes;
      } else if (w == "font") {
        e.kind = K::Font;
        if (peek().kind == Token::Ident && peek().text == "force") {
          ++_i;
          e.force = true;
        }
      } else if (w == "spiral_new") {
        e.kind = K::SpiralNew;
      } else if (w == "spiral") {
        e.kind = K::SpiralRot;
        e.rate = next_float("a spiral rate");
      } else if (w == "upload") {
        e.kind = K::Upload;
      } else if (w == "set") {
        e.kind = K::Set;
        e.target = next_ident("a register name");
        e.ivalue = static_cast<int32_t>(next_uint("a value"));
      } else if (w == "inc") {
        e.kind = K::Inc;
        e.target = next_ident("a register name");
        e.ivalue = 1;
        if (peek().kind == Token::Ident && peek().text == "by") {
          ++_i;
          e.ivalue = static_cast<int32_t>(next_uint("an increment"));
        }
      } else if (w == "toggle") {
        e.kind = K::Toggle;
        e.target = next_ident("a register name");
      } else if (w == "roll") {
        e.kind = K::Roll;
        e.target = next_ident("a register name");
        expect_punct(":");
        while (peek().kind == Token::Number || peek().kind == Token::Expr) {
          e.choices.push_back(static_cast<int32_t>(next_uint("a roll choice")));
        }
        if (e.choices.empty()) {
          err("roll needs at least one choice");
        }
      } else if (w == "pulse") {
        e.kind = K::Pulse;
        e.target = next_ident("a counter register");
        expect_ident("every");
        if (peek().kind == Token::Number || peek().kind == Token::Expr) {
          e.mod_literal = static_cast<int32_t>(next_uint("a modulus"));
        } else {
          e.mod_reg = next_ident("a modulus register");
        }
        expect_punct("->");
        e.flag = next_ident("a flag register");
      } else if (w == "copy") {
        e.kind = K::Copy;
        e.src = next_ident("a source register");
        expect_punct("->");
        e.target = next_ident("a destination register");
      } else if (w == "super_fast_tick") {
        e.kind = K::SuperFastTick;
      } else {
        err("unknown effect '" + w + "'");
      }
      parse_guard(e);
      return e;
    }
  };
}

namespace pattern
{
  ParseResult parse(const std::string& source)
  {
    ParseResult r;
    try {
      Parser p(lex(source));
      r.pattern = p.parse_pattern();
      r.ok = true;
    } catch (const ParseError& e) {
      r.ok = false;
      r.error = std::to_string(e.line) + ":" + std::to_string(e.col) + ": " + e.msg;
    }
    return r;
  }
}
