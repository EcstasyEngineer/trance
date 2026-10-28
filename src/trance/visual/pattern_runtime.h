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
    struct Capture {
      Image image;
      Slot requested_slot = Slot::None;
      uint32_t generation = 0;
      // A retained fallback is drawable but must retry when the requested lane
      // becomes ready, even if that lane's generation has not changed.
      bool current = false;
    };
    std::unordered_map<std::string, Capture> captures;
    std::unordered_map<std::string, int32_t> scalars;
  };
}

#endif
