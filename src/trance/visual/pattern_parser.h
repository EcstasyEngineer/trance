#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_PARSER_H
#include <trance/visual/pattern_ast.h>
#include <string>

// Parser for the authorable pattern DSL (Framing B surface language). It produces a
// normalized pattern::Node tree plus the pattern's name / weight / render preset.
// The grammar is deliberately explicit and bounded -- no conditionals, loops bounded
// by a literal count, no mutable variables -- so a session can carry a pattern as
// readable text and the engine can validate it before playback.
//
// Grammar (EBNF-ish):
//   pattern   = "pattern" ident "{" { header } node "}"
//   header    = "weight" int | "render" ident
//   node      = { prefix } primary
//   prefix    = "id" string | "phase" string | "image" slot [ "as" string ]
//   primary   = "every" int [ "@" int ] [ "divide" int ] [ ":" effects ]
//             | "timer" int [ "@" int ]
//             | "par" block | "seq" block | "one" block
//             | "repeat" int node | "offset" int node
//             | "burst" "{" burst-fields "}"
//             | "generate" ident "from" int "to" int block
//   block     = "{" { node } "}"
//   effects   = effect { "," effect }
//   effect    = ( draw-effect | state-effect ) [ guard ]
//   draw-effect = "image" slot [ "->" ident ] | "text" split slot | "anim" slot
//             | "subtext" slot | "small_text" slot [ "force" ]
//             | "themes" | "font" [ "force" ] | "spiral_new" | "spiral" number
//             | "upload"
//   // State effects read/write named scalar registers -- the only mutable state the
//   // language has. There are no general variables; these plus `when` are the whole
//   // imperative surface, just enough to express the stateful built-in visuals.
//   state-effect = "set" reg int | "inc" reg [ "by" int ] | "toggle" reg
//             | "roll" reg ":" int { int }            // = choices[random(n)]
//             | "pulse" reg "every" ( int | reg ) "->" reg   // counter -> one-frame flag
//             | "copy" reg "->" reg                    // image register copy
//             | "super_fast_tick"                      // the isolated SUPER_FAST FSM
//   guard     = "when" reg [ ( "==" | ">=" ) int ]     // run the effect only if true
//   slot      = "primary" | "alternate" | "runtime" | "random" | "reg" ident
//   split     = "word" | "line" | "word_gaps" | "line_gaps" | "once"
//   // [expr] (an arithmetic expression over + - * / ^ and the active generate var) may
//   // appear anywhere an int is expected; it is floored in integer contexts.
namespace pattern
{
  struct Parsed
  {
    std::string name;
    uint32_t weight = 1;
    std::string render;  // named render preset; empty = default
    Node root;
  };

  struct ParseResult
  {
    bool ok = false;
    Parsed pattern;
    std::string error;  // "line:col: message" on failure
  };

  ParseResult parse(const std::string& source);
}

#endif
