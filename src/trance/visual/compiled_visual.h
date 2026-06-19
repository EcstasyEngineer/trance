#ifndef TRANCE_SRC_TRANCE_VISUAL_COMPILED_VISUAL_H
#define TRANCE_SRC_TRANCE_VISUAL_COMPILED_VISUAL_H
#include <trance/visual/pattern_ast.h>
#include <trance/visual/render_preset.h>
#include <trance/visual/visual.h>
#include <string>

class VisualControl;

// A Visual built at runtime from a compiled pattern AST (Framing B). The pattern's
// effects write named image registers; a named render preset reads them and the
// live cycler state. This is the bridge that lets an authored .session pattern play
// through the same path as the hardcoded visuals -- and, because the compiled tree
// carries the same phase/image annotations, it drives the F1 overlay for free.
class CompiledVisual : public Visual
{
public:
  CompiledVisual(VisualControl& api, const pattern::Node& root, const std::string& render_preset);

private:
  // Named image registers an Image effect writes (e.g. "current"); read by the render
  // preset. Stable for the lifetime of the visual so the cycler's action lambdas can
  // write into it.
  pattern::Registers _registers;
  // id -> compiled node, so the render preset can read named cycler state.
  pattern::NodeMap _node_map;
};

#endif
