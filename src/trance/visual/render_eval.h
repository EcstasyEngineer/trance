#ifndef TRANCE_SRC_TRANCE_VISUAL_RENDER_EVAL_H
#define TRANCE_SRC_TRANCE_VISUAL_RENDER_EVAL_H
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_runtime.h>
#include <vector>

class Cycler;
class VisualRender;

namespace pattern
{
  // Run a pattern's data-driven render block for one frame: evaluate each statement's
  // [expr] params against live cycler state (via the node-id map / root) and the
  // registers, and emit the matching VisualRender draw call. This is the generic
  // replacement for the named C++ render presets -- the render is data, not code.
  void eval_render(const std::vector<RenderStmt>& stmts, VisualRender& api,
                   const Registers& regs, const NodeMap& nodes, const Cycler* root);

  // The render block used when a pattern declares none (draws the "current" image +
  // spiral + text), so playback never shows a blank frame.
  std::vector<RenderStmt> default_render_block();
}

#endif
