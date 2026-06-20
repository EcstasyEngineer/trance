#ifndef TRANCE_SRC_TRANCE_VISUAL_RENDER_PRESET_H
#define TRANCE_SRC_TRANCE_VISUAL_RENDER_PRESET_H
#include <common/media/image.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_runtime.h>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

class Cycler;
class VisualRender;

// Named render strategies for compiled patterns. A preset reads the pattern's image
// registers and named cycler nodes (by id) and emits the draw calls -- the analogue
// of a hardcoded visual's render lambda, but addressing nodes via the id map instead
// of captured C++ locals. Render is hand-written C++ per pattern, selected by name;
// making render itself data is the open v2 work (see docs/roadmap-grammar-v2.md).
namespace pattern
{
  // Registers (the pattern's mutable runtime state) is defined in pattern_runtime.h.
  using RenderFn = std::function<void(VisualRender&, const Registers&, const NodeMap&,
                                      const Cycler* root)>;

  // The named preset, or a simple single-image default if the name is empty/unknown
  // (so an authored pattern always renders something rather than failing playback).
  RenderFn render_preset(const std::string& name);
}

#endif
