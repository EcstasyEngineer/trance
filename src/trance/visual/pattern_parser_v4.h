#ifndef TRANCE_PATTERN_PARSER_V4_H
#define TRANCE_PATTERN_PARSER_V4_H
#include <trance/visual/pattern_parse_result.h>
// Canonical V4 JSON is compiled directly to native occurrence nodes and render data.
namespace patternv4
{
  using ParseResult = pattern::ParseResult;
  ParseResult parse(const std::string& source, uint32_t locked_period_frames = 0);
} // namespace patternv4
#endif
