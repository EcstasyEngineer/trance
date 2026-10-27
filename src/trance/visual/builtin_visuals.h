#ifndef TRANCE_SRC_TRANCE_VISUAL_BUILTIN_VISUALS_H
#define TRANCE_SRC_TRANCE_VISUAL_BUILTIN_VISUALS_H
// Shared V4 builtin catalog for CLI, F2 and playback identities.
#include <cstdint>
#include <vector>

struct BuiltinVisual {
  uint32_t type;
  const char* name;
  const char* blurb;
};

inline const std::vector<BuiltinVisual>& builtin_visuals()
{
  static const std::vector<BuiltinVisual> table = {
      {1, "accelerate", "accelerating image + spiral"},
      {2, "slow_flash", "slow then fast flash phases"},
      {3, "sub_text", "image + scrolling subtext"},
      {4, "flash_text", "2-image crossfade + text"},
      {5, "parallel", "2-image parallel layers"},
      {6, "super_parallel", "3-image overlay / triple fade"},
      {7, "animation", "animation + crossfade image"},
      {8, "super_fast", "rapid current/next cuts"},
  };
  return table;
}

#endif
