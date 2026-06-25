// Sanity + behavioral test for the v3 intent grammar (docs/spec-grammar-v3.md).
// Asserts the load-bearing invariants of Phase 1: patterns nest and lower to a runnable tree;
// crossfade EMERGES from primitives (copy + cur/prev + complementary alpha, no keyword); and
// registers are lexically pattern-scoped so two sibling crossfades never collide.
//
// Headless: parser + compiler + cyclers, no SFML/protobuf. Run via ctest.
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser_v3.h>

#include <iostream>
#include <string>
#include <vector>

namespace
{
  int g_fail = 0;
  void check(bool ok, const std::string& what)
  {
    std::cout << (ok ? "  ok  " : "FAIL  ") << what << "\n";
    if (!ok) ++g_fail;
  }

  patternv3::ParseResult parse(const std::string& src, uint32_t locked = 0)
  {
    return patternv3::parse(src, locked);
  }

  std::vector<const pattern::RenderStmt*> images(const patternv3::ParseResult& pr)
  {
    std::vector<const pattern::RenderStmt*> v;
    for (const auto& s : pr.render_block)
      if (s.op == pattern::RenderStmt::Op::Image) v.push_back(&s);
    return v;
  }
  // The qualified-register prefix, e.g. "_n1" from "_n1$cur".
  std::string prefix(const std::string& reg)
  {
    const auto d = reg.find('$');
    return d == std::string::npos ? reg : reg.substr(0, d);
  }
}

int main()
{
  // 1. Simplest draw lowers to a runnable tree with a curve-driven zoom.
  {
    auto pr = parse("pattern hello for 240f { image concept zoom (curve 0 -> 0.5) }");
    check(pr.ok, std::string("simplest: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 240, "simplest: tree length 240");
      auto im = images(pr);
      check(im.size() == 1 && !im[0]->zoom.empty(), "simplest: one image draw with a zoom expr");
      delete root;
    }
  }

  // 2. Crossfade emerges from primitives: two image draws (cur + prev) with COMPLEMENTARY alpha,
  //    both in the same pattern's register scope, fed by a copy handoff -- no crossfade keyword.
  {
    auto pr = parse(R"(
pattern flash_text for 1024f {
  pattern life for 128f loop 8 {
    every 64f -> beat {
      copy cur -> prev
      image concept -> cur fade in  zoom (curve 0 -> 0.5 over life)
      draw prev          fade out zoom (curve 0.5 -> 1.0 over life)
    }
  }
})");
    check(pr.ok, std::string("crossfade: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto im = images(pr);
      check(im.size() == 2, "crossfade: exactly two image layers (cur + prev)");
      if (im.size() == 2) {
        const bool comp = (im[0]->alpha.find("1 - ") != std::string::npos) !=
                          (im[1]->alpha.find("1 - ") != std::string::npos);
        check(comp, "crossfade: layers have complementary alpha (one fades in, one out)");
        check(prefix(im[0]->image_reg) == prefix(im[1]->image_reg),
              "crossfade: cur and prev share one register scope (the life pattern)");
      }
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 1024, "crossfade: tree length 1024");
      delete root;
    }
  }

  // 3. Register scoping: two sibling crossfades each minting cur/prev must NOT collide.
  {
    auto pr = parse(R"(
pattern twin for 512f {
  pattern a for 128f loop 4 { every 64f -> b { copy cur -> prev  image reward  -> cur fade in } draw prev fade out }
  pattern c for 128f loop 4 { every 64f -> d { copy cur -> prev  image concept -> cur fade in } draw prev fade out }
})");
    check(pr.ok, std::string("siblings: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto im = images(pr);
      check(im.size() == 4, "siblings: four image layers");
      if (im.size() == 4) {
        // a's two layers share a prefix; c's two share a DIFFERENT prefix.
        const std::string pa = prefix(im[0]->image_reg), pc = prefix(im[2]->image_reg);
        check(prefix(im[1]->image_reg) == pa && prefix(im[3]->image_reg) == pc && pa != pc,
              "siblings: the two crossfades occupy DISTINCT register scopes (no collision)");
      }
    }
  }

  // 4. `over` resolution check fails loud on an unknown clock (no silent-zero).
  {
    auto pr = parse("pattern x for 100f { image concept zoom (curve 0 -> 1 over nope) }");
    check(!pr.ok && pr.error.find("nope") != std::string::npos,
          "resolution: `over nope` is a hard parse error");
  }

  std::cout << "\n" << (g_fail ? "FAILED: " : "all v3 grammar checks passed (")
            << g_fail << (g_fail ? " failed\n" : " failures)\n");
  return g_fail ? 1 : 0;
}
