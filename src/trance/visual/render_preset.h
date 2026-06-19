#ifndef TRANCE_SRC_TRANCE_VISUAL_RENDER_PRESET_H
#define TRANCE_SRC_TRANCE_VISUAL_RENDER_PRESET_H
#include <common/media/image.h>
#include <trance/visual/pattern_compiler.h>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

class Cycler;
class VisualRender;

// Named render strategies for compiled patterns (Framing B). A preset reads the
// pattern's image registers and named cycler nodes (by id) and emits the draw calls
// -- the analogue of a hardcoded visual's render lambda, but addressing nodes via
// the id map instead of captured C++ locals. Keeping render as named C++ presets
// (rather than a render expression language) is the B1 plan; it is also what a
// built-in must have before it can be retired in favour of its compiled twin.
namespace pattern
{
  // The pattern's mutable state, written by effects and read by the render preset.
  // `images` are the named image slots an Image/Copy effect writes; `scalars` are the
  // named bool/int registers the state effects (Set/Inc/Toggle/Roll/Pulse) maintain.
  struct Registers
  {
    std::unordered_map<std::string, Image> images;
    std::unordered_map<std::string, int32_t> scalars;
  };
  using RenderFn = std::function<void(VisualRender&, const Registers&, const NodeMap&,
                                      const Cycler* root)>;

  // The named preset, or a simple single-image default if the name is empty/unknown
  // (so an authored pattern always renders something rather than failing playback).
  RenderFn render_preset(const std::string& name);
}

#endif
