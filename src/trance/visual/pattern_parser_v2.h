#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_V2_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_V2_H
#include <trance/visual/pattern_ast.h>
#include <string>

// The v2 "intent" grammar front-end (docs/spec-grammar-v2.md). It is a friendlier
// authoring surface -- phases, streams, curves -- that LOWERS to the same
// pattern::Node AST the v1 grammar produces, so the existing compiler, cyclers and
// render path are reused unchanged. This is the front-end-only contract from the
// spec: nothing here is a new runtime, every construct becomes a Node subtree.
//
// Scope of this first slice (grown toward the full spec incrementally):
//   pattern   = "pattern" IDENT ["repeat" INT] "{" phase+ "}"
//   phase     = ("phase"|"escalate"|"deepen") STRING "for" INT "f" "{"
//                 ["description" STRING] statement+ "}"
//   statement = ("image"|"word"|"caption"|"subtext") theme "every" INT
//             | "spiral" "rate" NUMBER
//   theme     = "concept" | "reward" | "runtime"
namespace patternv2
{
  struct ParseResult
  {
    bool ok = false;
    std::string name;
    pattern::Node root;                            // lowered v1 AST, ready for pattern::compile
    std::vector<pattern::RenderStmt> render_block;  // the generated "what is drawn" (zoom etc.)
    std::vector<std::string> warnings;             // non-fatal "line:col: message" diagnostics
    std::string error;                             // "line:col: message" on failure
  };

  // `locked_period_frames` is the program's entrainment beat period in frames (0 = no pulsed
  // bed); `every locked` / `spiral locked` lower against it, or hard-error when 0.
  ParseResult parse(const std::string& source, uint32_t locked_period_frames = 0);
}

#endif
