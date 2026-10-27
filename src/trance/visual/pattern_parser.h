#pragma once
#include <trance/visual/pattern_parse_result.h>
namespace pattern {
// The product accepts the canonical V4 JSON AST exclusively.
ParseResult parse_source(const std::string& source, uint32_t locked_period_frames = 0);
}
