#ifndef TRANCE_SRC_TRANCE_VISUAL_BUILTIN_VISUALS_H
#define TRANCE_SRC_TRANCE_VISUAL_BUILTIN_VISUALS_H
// The single catalog of built-in visuals: proto VisualType enum value <-> v3 pattern
// name <-> human blurb. Consumed by the --visual CLI flag (main.cpp), the F2 Visuals
// section (app_ui.cpp), and the F1 debug overlay's label (director.cpp) -- one table
// so a rename or addition can't drift across those surfaces.
//
// `name` is the v3 built-in pattern name (builtin_patterns_v3.cpp's `pattern NAME
// for ...` declarations); `type` is the trance_pb::Program::VisualType enum value.
// Note the proto enum names don't all match (enum 5 is PARALLEL; its v3 name is
// `simple`) -- the v3 name is the user-facing one everywhere.
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
      {5, "simple", "single image"},
      {6, "super_parallel", "3-image overlay / triple fade"},
      {7, "animation", "animation + crossfade image"},
      {8, "super_fast", "rapid current/next cuts"},
  };
  return table;
}

#endif
