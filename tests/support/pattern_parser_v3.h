#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_V3_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_V3_H
#include <trance/visual/pattern_parse_result.h>
#include <string>
#include <vector>

// Historical V3 frontend used only by regression oracles. Excluded from the product.
namespace patternv3
{
  using ParseResult = pattern::ParseResult;

  // `locked_period_frames` is the program's entrainment beat period in frames (0 = no pulsed
  // bed); `beats N` / `locked` lengths lower against it, or hard-error when 0.
  ParseResult parse(const std::string& source, uint32_t locked_period_frames = 0);
}

#endif
