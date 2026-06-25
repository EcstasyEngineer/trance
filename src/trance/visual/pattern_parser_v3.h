#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_V3_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_V3_H
#include <trance/visual/pattern_ast.h>
#include <string>
#include <vector>

// The v3 intent grammar front-end (docs/spec-grammar-v3.md). Two nouns and one rule:
// a PATTERN is a named span with a 0..1 clock; an EFFECT is a draw/driver/state op placed
// inside a pattern; every numeric an effect takes is a MODULATOR (curve / beat / literal /
// [expr]) that rides the enclosing pattern's clock unless redirected with `over NAME`.
//
// Like v2 it LOWERS to the same pattern::Node AST + RenderStmt render block the existing
// compiler/cyclers/render path already runs -- no new runtime in Phase 1. Patterns nest 1:1
// onto the Cycler tree; crossfade EMERGES from `copy` + a `prev` register + two
// complementary-alpha draws (no baked keyword). Registers are lexically pattern-scoped: a
// bare name is qualified with its enclosing pattern's compiled id, so nested patterns never
// collide; a qualified `Other.reg` reaches another pattern's register.
namespace patternv3
{
  struct ParseResult
  {
    bool ok = false;
    std::string name;
    pattern::Node root;                             // lowered AST, ready for pattern::compile
    std::vector<pattern::RenderStmt> render_block;  // the generated "what is drawn"
    std::vector<std::string> warnings;              // non-fatal "line:col: message" diagnostics
    std::string error;                              // "line:col: message" on failure
  };

  // `locked_period_frames` is the program's entrainment beat period in frames (0 = no pulsed
  // bed); `beats N` / `locked` lengths lower against it, or hard-error when 0.
  ParseResult parse(const std::string& source, uint32_t locked_period_frames = 0);
}

#endif
