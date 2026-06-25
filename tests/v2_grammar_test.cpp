// Sanity + behavioral harness for the v2 intent grammar and the shipped built-ins.
//
// Deliberately NOT a parity test. We do not freeze the compiled-tree shape of the
// original 8 patterns: the v2 grammar is meant to SUPERSEDE them, and byte-locking the
// schedule is what kept dragging us back to super_fast's FSM. Instead we assert the
// invariants that actually matter:
//   1. every shipped v2 built-in PARSES,
//   2. it lowers to a non-null cycler tree with a positive length,
//   3. it produces a render block,
//   4. targeted behavioral checks (e.g. flash_text really crossfades -- two image
//      layers, prev + current -- rather than showing one image at a time).
//
// Headless on purpose: parser + compiler + cyclers + the v2 built-in sources pull in no
// SFML / protobuf, so this builds and runs with a bare C++17 compiler via `ctest`.
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser_v2.h>

#include <iostream>
#include <string>
#include <vector>

namespace
{
  int g_failures = 0;

  void check(bool ok, const std::string& what)
  {
    std::cout << (ok ? "  ok  " : "FAIL  ") << what << "\n";
    if (!ok) {
      ++g_failures;
    }
  }

  struct Builtin {
    uint32_t type;
    const char* name;
  };
  const Builtin kBuiltins[] = {
      {1, "accelerate"}, {2, "slow_flash"}, {3, "sub_text"},     {4, "flash_text"},
      {5, "simple"},     {6, "super_parallel"}, {7, "animation"}, {8, "super_fast"},
  };

  // Parse a v2 built-in and return its result; reports parse errors as failures.
  patternv2::ParseResult parse_builtin(const Builtin& b)
  {
    const std::string src = builtin::pattern_source_v2(b.type);
    patternv2::ParseResult pr = patternv2::parse(src);
    if (!pr.ok) {
      std::cout << "FAIL  " << b.name << "  PARSE-ERROR: " << pr.error << "\n";
      ++g_failures;
    }
    return pr;
  }

  // Count image-drawing render statements and collect their registers.
  std::vector<std::string> image_regs(const patternv2::ParseResult& pr)
  {
    std::vector<std::string> regs;
    for (const auto& st : pr.render_block) {
      if (st.op == pattern::RenderStmt::Op::Image) {
        regs.push_back(st.image_reg);
      }
    }
    return regs;
  }
}

int main()
{
  // 1-3: every shipped built-in parses, lowers, and paints.
  for (const auto& b : kBuiltins) {
    const patternv2::ParseResult pr = parse_builtin(b);
    if (!pr.ok) {
      continue;
    }
    Cycler* root = pattern::compile(pr.root);
    const bool tree_ok = root && root->length() > 0;
    check(tree_ok, std::string(b.name) + ": lowers to a runnable tree (len=" +
                       std::to_string(root ? root->length() : 0) + ")");
    check(!pr.render_block.empty(), std::string(b.name) + ": produces a render block");
    delete root;
  }

  // 4: behavioral -- flash_text must genuinely crossfade: two image layers (prev +
  // current) so two images are on screen at once, not one-at-a-time.
  {
    const patternv2::ParseResult pr = parse_builtin({4, "flash_text"});
    if (pr.ok) {
      const std::vector<std::string> regs = image_regs(pr);
      bool has_prev = false, has_current = false;
      for (const auto& r : regs) {
        if (r == "prev") has_prev = true;
        if (r == "current") has_current = true;
      }
      check(regs.size() >= 2, "flash_text: draws >= 2 image layers (interleaved crossfade)");
      check(has_prev && has_current,
            "flash_text: layers are 'prev' + 'current' (copy-handoff dissolve)");
    }
  }

  std::cout << "\n" << (g_failures ? "FAILED: " : "all v2 grammar checks passed (")
            << g_failures << (g_failures ? " check(s) failed\n" : " failures)\n");
  return g_failures ? 1 : 0;
}
