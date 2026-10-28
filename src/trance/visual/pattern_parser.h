#pragma once
#include <trance/visual/pattern_parse_result.h>
namespace pattern {
// V4 text lowers to the same canonical AST and native compiler as V4 JSON.
ParseResult parse_source(const std::string& source, uint32_t locked_period_frames = 0);
}
