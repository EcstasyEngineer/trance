#pragma once
#include <string>

namespace patternv4 {
// Syntax conversion only. Native parse() remains the semantic validator/compiler.
// Both functions throw on invalid or unsupported input; the caller retains its document.
std::string parse_text_json(const std::string& source);
std::string format_text_json(const std::string& json_source);
}
