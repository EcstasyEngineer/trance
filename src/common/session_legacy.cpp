#include <common/session_legacy.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

#pragma warning(push, 0)
#include <google/protobuf/text_format.h>
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  // Legacy .session/.cfg files are hand/tool-authored on Windows and are riddled with
  // literal backslash path separators, e.g. `image_path: ".\\hypno\\103827022_p4.png"`.
  // protobuf TextFormat's string-literal grammar treats a backslash followed by 1-3
  // octal digits (or \x../\u..../\U........) as an escape sequence -- so `\103` silently
  // decodes to the single byte 0x43 ('C'), and `\227` decodes to the single byte 0x97,
  // which is invalid as standalone UTF-8 and makes JSON emission throw. This corpus has
  // no intentionally-authored octal/hex/unicode string escapes (paths and text_line
  // strings are the only string content; colours are float fields, not string escapes --
  // docs/session-json-format.md sec 2.3), so inside quoted string literals we treat any
  // backslash immediately followed by a digit or x/X/u/U as a Windows path separator, not
  // an escape, and double it so protobuf reads it as a literal backslash. True escapes
  // actually used in this corpus (`\n` line breaks in text_line, `\"` quoted phrases,
  // `\\` itself) are left untouched. `#` line comments are also tracked so a `#` inside a
  // string doesn't get misread as a comment starting outside one (and vice versa).
  std::string protect_legacy_windows_backslashes(const std::string& in, std::size_t& fixed_count)
  {
    enum class State { normal, line_comment, string };
    State state = State::normal;
    char quote = 0;

    std::string out;
    out.reserve(in.size());
    fixed_count = 0;

    for (std::size_t i = 0; i < in.size(); ++i) {
      char c = in[i];

      if (state == State::line_comment) {
        out.push_back(c);
        if (c == '\n') {
          state = State::normal;
        }
        continue;
      }

      if (state == State::normal) {
        out.push_back(c);
        if (c == '#') {
          state = State::line_comment;
        } else if (c == '"' || c == '\'') {
          state = State::string;
          quote = c;
        }
        continue;
      }

      // state == State::string
      if (c == quote) {
        out.push_back(c);
        state = State::normal;
        continue;
      }

      if (c != '\\' || i + 1 == in.size()) {
        out.push_back(c);
        continue;
      }

      unsigned char next = static_cast<unsigned char>(in[i + 1]);
      bool is_digit_or_hex_unicode_lead =
          (next >= '0' && next <= '9') || next == 'x' || next == 'X' || next == 'u' || next == 'U';
      if (is_digit_or_hex_unicode_lead) {
        // Not a real escape in this corpus -- a Windows path separator followed by a
        // filename/directory that starts with a digit (or looks hex-ish). Double the
        // backslash so TextFormat parses it as one literal backslash; do not consume
        // `next` here, it's copied verbatim on the following iteration.
        out.push_back('\\');
        out.push_back('\\');
        ++fixed_count;
        continue;
      }

      // A genuine escape used in this corpus (\n, \", \\, ...) -- copy both chars as-is.
      out.push_back(c);
      out.push_back(in[i + 1]);
      ++i;
    }

    return out;
  }

  // Parses a legacy text-format protobuf file into T (trance_pb::Session or
  // trance_pb::System). Throws std::runtime_error on missing file or parse failure.
  template <typename T>
  T load_legacy_proto(const std::string& path)
  {
    std::ifstream f{path};
    if (!f) {
      throw std::runtime_error("couldn't open " + path);
    }
    std::string str{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};

    std::size_t fixed_count = 0;
    str = protect_legacy_windows_backslashes(str, fixed_count);
    if (fixed_count) {
      std::cerr << "warning: " << path << ": normalized " << fixed_count
                << " ambiguous backslash-digit/hex/unicode sequences as literal Windows"
                   " path separators before parsing (see session_legacy.cpp comment)\n";
    }

    T proto;
    if (!google::protobuf::TextFormat::ParseFromString(str, &proto)) {
      throw std::runtime_error(path + ": failed to parse as legacy trance textproto");
    }
    return proto;
  }
}

trance_pb::Session load_legacy_session(const std::string& path)
{
  return load_legacy_proto<trance_pb::Session>(path);
}

trance_pb::System load_legacy_system(const std::string& path)
{
  return load_legacy_proto<trance_pb::System>(path);
}
