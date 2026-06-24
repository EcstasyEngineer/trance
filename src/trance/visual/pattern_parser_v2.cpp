#include <trance/visual/pattern_parser_v2.h>

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
  using pattern::Effect;
  using pattern::Node;
  using pattern::Slot;

  // A parse error carrying a source position, mapped to "line:col: message" by parse().
  struct ParseError
  {
    std::string message;
    std::size_t pos;
  };

  // ---- tokenizer ------------------------------------------------------------
  // Minimal hand cursor over the source: skips whitespace and `# ... EOL` comments,
  // and exposes ident / string / number / punctuation lexemes on demand.
  class Cursor
  {
  public:
    explicit Cursor(const std::string& src) : _src(src) {}

    std::size_t pos() const { return _i; }
    bool eof() { skip(); return _i >= _src.size(); }

    char peek_char()
    {
      skip();
      return _i < _src.size() ? _src[_i] : '\0';
    }

    // A bare word: identifier (a-z A-Z 0-9 _) -- keywords and theme names are words.
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

    // A double-quoted string literal (no escapes needed for this grammar).
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
      ++_i;  // closing quote
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

    // Peek the next word without consuming (for keyword dispatch); "" at EOF/non-word.
    std::string peek_word()
    {
      skip();
      std::size_t j = _i;
      while (j < _src.size() && (std::isalnum(static_cast<unsigned char>(_src[j])) || _src[j] == '_')) {
        ++j;
      }
      return _src.substr(_i, j - _i);
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

  // ---- node construction helpers -------------------------------------------
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

  Slot theme_to_slot(const std::string& w, std::size_t at)
  {
    if (w == "concept") return Slot::Primary;
    if (w == "reward") return Slot::Alternate;
    if (w == "runtime") return Slot::Runtime;
    throw ParseError{"unknown theme '" + w + "' (want concept|reward|runtime)", at};
  }

  // ---- the parser -----------------------------------------------------------
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

      // pattern { phases } -> One{ init , [repeat] Sequence{ phases } }
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
    }

  private:
    static Effect effect(Effect::Kind kind)
    {
      Effect e;
      e.kind = kind;
      return e;
    }

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
      const uint32_t length = _c.uint_lit();
      _c.expect('f');  // frames unit
      _c.expect('{');

      if (_c.peek_word() == "description") {
        _c.word();
        _c.string_lit();  // metadata only; not lowered
      }

      std::vector<Node> streams;
      while (_c.peek_char() != '}') {
        streams.push_back(parse_statement(length));
      }
      _c.expect('}');
      if (streams.empty()) {
        throw ParseError{"phase '" + label + "' has no content", _c.pos()};
      }

      // Streams are parallel-by-default; the phase node carries the section label.
      Node phase = group(Node::Type::Par, std::move(streams));
      phase.id = label;
      phase.phase = label;
      return phase;
    }

    // A content stream `T theme every M` -> Repeat(length/M, Action(M, effect)); the
    // image case also annotates the leaf's image_slot so it surfaces in the overlay.
    // `spiral rate K` -> Action(1, SpiralRot).
    Node parse_statement(uint32_t phase_length)
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
      } else if (kw == "caption") {
        kind = Effect::Kind::SmallSub;
      } else if (kw == "subtext") {
        kind = Effect::Kind::Subtext;
      } else {
        throw ParseError{"unknown statement '" + kw + "'", at};
      }

      const std::size_t theme_at = _c.pos();
      const Slot slot = theme_to_slot(_c.word(), theme_at);
      expect_word("every");
      const std::size_t every_at = _c.pos();
      const uint32_t every = _c.uint_lit();
      if (every == 0) {
        throw ParseError{"'every 0' is not a valid beat", every_at};
      }
      if (phase_length % every != 0) {
        throw ParseError{"beat " + std::to_string(every) + " does not divide phase length " +
                             std::to_string(phase_length),
                         every_at};
      }

      Effect e = effect(kind);
      e.slot = slot;
      if (kind == Effect::Kind::SmallSub) {
        e.force = true;
      }
      Node leaf = action(every, {e});
      if (is_image) {
        leaf.image_slot = slot;
      }
      return repeat(phase_length / every, std::move(leaf));
    }

    Cursor _c;
  };

  // Map a byte offset to "line:col" for error reporting.
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
