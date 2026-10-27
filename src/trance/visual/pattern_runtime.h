#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_RUNTIME_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_RUNTIME_H
#include <trance/visual/pattern_ast.h>
#include <common/media/image.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace pattern
{
  // Private image captures and scalar selection/accent state for one compiled visual.
  struct Registers
  {
    std::unordered_map<std::string, Image> images;
    std::unordered_map<std::string, Slot> image_slots;
    // Which theme GENERATION each register's image was pulled from (ThemeBank bumps a
    // lane's generation whenever the theme occupying it changes). A register whose
    // generation has fallen behind is still holding an image of a theme that is no
    // longer on that lane -- valid (Image is ref-counted, so an unloaded theme's frame
    // stays perfectly drawable) but stale, and since a register is only replaced when
    // its effect fires again, it would otherwise sit on screen indefinitely.
    //
    std::unordered_map<std::string, uint32_t> image_gens;
    std::unordered_map<std::string, int32_t> scalars;
  };
}

#endif
