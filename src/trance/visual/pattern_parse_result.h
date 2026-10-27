#pragma once
#include <trance/visual/pattern_ast.h>
#include <string>
#include <vector>

namespace pattern {
struct ParseResult {
  bool ok = false;
  std::string name;
  Node root;
  std::vector<RenderStmt> render_block;
  std::vector<std::string> warnings;
  std::string error;
};
}
