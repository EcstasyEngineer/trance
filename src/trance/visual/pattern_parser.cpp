#include <trance/visual/pattern_parser.h>
#include <trance/visual/pattern_parser_v4.h>
namespace pattern {
ParseResult parse_source(const std::string& source, uint32_t locked_period_frames)
{
  std::string text = source;
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first != std::string::npos && text[first] == '{')
    return patternv4::parse(text, locked_period_frames);
  ParseResult result;
  result.error = "V3 text grammars are unsupported; provide a V4 JSON AST";
  return result;
}
}
