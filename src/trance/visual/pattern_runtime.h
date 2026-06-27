#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_RUNTIME_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_RUNTIME_H
#include <trance/visual/pattern_ast.h>
#include <common/media/image.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace pattern
{
  // A compiled pattern's mutable runtime state, written by effects and read by the
  // render evaluator. `images` are the named image slots an Image/Copy effect writes
  // (e.g. "current"); `image_slots` stores the concrete source theme for the same
  // register so render-time debug can say which theme is actually on screen; `scalars`
  // are the named bool/int registers the state effects (set/inc/toggle/roll/pulse)
  // maintain.
  struct Registers
  {
    std::unordered_map<std::string, Image> images;
    std::unordered_map<std::string, Slot> image_slots;
    std::unordered_map<std::string, int32_t> scalars;
  };
}

#endif
