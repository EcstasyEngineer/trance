#ifndef TRANCE_SRC_TRANCE_VISUAL_COMPILED_VISUAL_H
#define TRANCE_SRC_TRANCE_VISUAL_COMPILED_VISUAL_H
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_runtime.h>
#include <trance/visual/visual.h>
#include <vector>

class VisualControl;

// A Visual built from a parsed pattern. Scheduled effects update runtime state;
// render statements read that state and the live clocks. Built-ins and session
// patterns share this path, including the tree annotations used by the F1 overlay.
class CompiledVisual : public Visual
{
public:
  // `render_block` is the data-driven render (run by render_eval.cpp). An empty block
  // falls back to pattern::default_render_block().
  CompiledVisual(VisualControl& api, const pattern::Node& root,
                 const std::vector<pattern::RenderStmt>& render_block);

private:
  // Re-pull image registers whose lane has changed theme since they were captured, so a
  // captured frame of an unloaded theme doesn't sit on screen until its effect happens to
  // fire again. Skips `copy` snapshots, and only accepts an image the CURRENT theme
  // actually produced. Run once per frame from the render lambda; see the definition.
  void refresh_stale_registers(VisualControl& api);

  // Named image registers an Image effect writes (e.g. "current"); read by the render
  // block. Stable for the lifetime of the visual so the cycler's action lambdas can
  // write into it.
  pattern::Registers _registers;
  // id -> compiled node, so render expressions can read named cycler state.
  pattern::NodeMap _node_map;
};

#endif
