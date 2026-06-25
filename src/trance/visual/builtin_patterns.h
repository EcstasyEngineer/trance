#ifndef TRANCE_SRC_TRANCE_VISUAL_BUILTIN_PATTERNS_H
#define TRANCE_SRC_TRANCE_VISUAL_BUILTIN_PATTERNS_H
#include <cstdint>
#include <string>

// The built-in visuals expressed as Framing-B pattern DSL. As each hardcoded
// *Visual class is ported (and proven render-equivalent), its source is added here
// and `Director::change_visual` compiles it instead of constructing the C++ class --
// letting the class be deleted while the `Program::VisualType` enum (and therefore
// every existing .session) stays unchanged. An empty string means "not yet ported;
// use the hardcoded class".
namespace builtin
{
  // `visual_type` is the Program::VisualType enum value (1=ACCELERATE .. 8=SUPER_FAST).
  std::string pattern_source(uint32_t visual_type);

  // The same built-ins authored in the v3 intent grammar (docs/spec-grammar-v3.md), parsed by
  // patternv3::parse. The director prefers this when non-empty (else falls back to v1).
  std::string pattern_source_v3(uint32_t visual_type);
}

#endif
