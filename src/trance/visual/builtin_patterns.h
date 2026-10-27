#ifndef TRANCE_SRC_TRANCE_VISUAL_BUILTIN_PATTERNS_H
#define TRANCE_SRC_TRANCE_VISUAL_BUILTIN_PATTERNS_H
#include <cstdint>
#include <string>

// The eight builtins use native V4 AST sources. Program::VisualType identities remain unchanged.
namespace builtin
{
  // `visual_type` is the Program::VisualType enum value (1=ACCELERATE .. 8=SUPER_FAST).
  std::string pattern_source_v4(uint32_t visual_type);
}

#endif
