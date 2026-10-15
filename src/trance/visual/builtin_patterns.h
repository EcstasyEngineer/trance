#ifndef TRANCE_SRC_TRANCE_VISUAL_BUILTIN_PATTERNS_H
#define TRANCE_SRC_TRANCE_VISUAL_BUILTIN_PATTERNS_H
#include <cstdint>
#include <string>

// The 8 built-in visuals authored in the v3 intent grammar (docs/spec-grammar-v3.md,
// builtin_patterns_v3.cpp), parsed by patternv3::parse and compiled instead of a hardcoded
// C++ *Visual class -- letting the class stay deleted while the `Program::VisualType` enum
// (and therefore every existing .session) is unchanged.
namespace builtin
{
  // `visual_type` is the Program::VisualType enum value (1=ACCELERATE .. 8=SUPER_FAST).
  std::string pattern_source_v3(uint32_t visual_type);
}

#endif
