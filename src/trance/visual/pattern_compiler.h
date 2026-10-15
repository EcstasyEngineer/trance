#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_COMPILER_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_COMPILER_H
#include <trance/visual/pattern_ast.h>
#include <functional>
#include <string>
#include <unordered_map>

class Cycler;

namespace pattern
{
  // Map from a Node's id to the Cycler it compiled to (non-owning; valid for the
  // lifetime of the compiled tree). Used by render presets to read named nodes.
  using NodeMap = std::unordered_map<std::string, const Cycler*>;

  // Given an Action node, return the behaviour its leaf should run (or an empty
  // function for a pure timer). This is the seam that keeps the compiler itself
  // free of any VisualControl / SFML dependency: the engine supplies a MakeAction
  // that maps node.effects onto real VisualControl calls + register writes, while
  // tests supply one that logs. Called once per Action leaf at compile time.
  using MakeAction = std::function<std::function<void()>(const Node&)>;

  // Lower a normalized pattern AST to a heap-allocated Cycler tree, reusing the
  // existing Cycler classes so their timing semantics stay authoritative. The
  // caller owns the returned root. Phase/image annotations are applied so the
  // compiled tree drives the same F1 overlay the hardcoded visuals do.
  Cycler* compile(const Node& node, const MakeAction& make_action);

  // As above, additionally recording id->Cycler for every node carrying an id.
  Cycler* compile(const Node& node, const MakeAction& make_action, NodeMap& node_map);

  // Schedule-only convenience: every leaf is a pure timer (no behaviour). Used by
  // the structural equivalence test and anywhere only the timing matters.
  Cycler* compile(const Node& node);
}

#endif
