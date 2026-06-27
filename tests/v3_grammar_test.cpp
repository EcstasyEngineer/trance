// Sanity + behavioral test for the v3 intent grammar (docs/spec-grammar-v3.md).
// Asserts the load-bearing invariants of Phase 1: patterns nest and lower to a runnable tree;
// crossfade EMERGES from primitives (copy + cur/prev + complementary alpha, no keyword); and
// registers are lexically pattern-scoped so two sibling crossfades never collide.
//
// Headless: parser + compiler + cyclers, no SFML/protobuf. Run via ctest.
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser_v3.h>

#include <cctype>
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
  bool suffix(const std::string& s, const std::string& tail)
  {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
  }
  std::string progress_clock(const std::string& expr)
  {
    const auto p = expr.find(".progress");
    if (p == std::string::npos) return "";
    std::size_t start = p;
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(expr[start - 1])) || expr[start - 1] == '_')) {
      --start;
    }
    return expr.substr(start, p - start);
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

  // 2. Crossfade emerges from primitives: a copied previous image drawn under a new image whose
  //    top layer fades in. Both zoom halves ride the same beat clock so the copied image keeps
  //    zooming continuously after the handoff -- no crossfade keyword or hidden +0.5 branch.
  {
    auto pr = parse(R"(
pattern flash_text for 1024f {
  pattern life for 128f loop 8 {
    every 64f -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image concept -> cur fade in zoom (curve 0 -> 0.5)
    }
  }
})");
    check(pr.ok, std::string("crossfade: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto im = images(pr);
      check(im.size() == 2, "crossfade: exactly two image layers (cur + prev)");
      if (im.size() == 2) {
        const auto* prev = suffix(im[0]->image_reg, "$prev") ? im[0] : im[1];
        const auto* cur = suffix(im[0]->image_reg, "$cur") ? im[0] : im[1];
        check(suffix(im[0]->image_reg, "$prev") && suffix(im[1]->image_reg, "$cur"),
              "crossfade: previous layer draws before the new fade-in layer");
        check(!cur->alpha.empty() && progress_clock(cur->alpha) == progress_clock(cur->zoom) &&
                  progress_clock(cur->zoom) == progress_clock(prev->zoom),
              "crossfade: fade and both zoom halves ride the same beat clock");
        check(prefix(cur->image_reg) == prefix(prev->image_reg),
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

  // 3b. Spiral speed is a curve-drivable render param; look{} lowers to a SpiralSet effect.
  {
    auto pr = parse(R"(
pattern s for 240f {
  look { spiral type=3 width=6 }
  image concept zoom 0.5
  spiral speed (curve 1 -> 3)
})");
    check(pr.ok, std::string("spiral: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      bool spiral_speed = false;
      for (const auto& st : pr.render_block)
        if (st.op == pattern::RenderStmt::Op::Spiral && !st.speed.empty()) spiral_speed = true;
      check(spiral_speed, "spiral: speed lowers to a render-side [expr]");
    }
  }

  // 3c. The wave warp (and `drunk` sugar) lowers to a Warp render op with params.
  {
    auto pr = parse(R"(
pattern w for 240f {
  warp amplitude (curve 0 -> 0.3) wavelength 0.2 speed 2
  image concept zoom 0.5
})");
    check(pr.ok, std::string("warp: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    bool has_warp = false;
    if (pr.ok)
      for (const auto& st : pr.render_block)
        if (st.op == pattern::RenderStmt::Op::Warp && !st.zoom.empty()) has_warp = true;
    check(has_warp, "warp: lowers to a Warp op carrying an amplitude expr");

    auto pd = parse("pattern d for 240f { drunk (curve 0 -> 0.3) image concept zoom 0.5 }");
    bool drunk_warp = pd.ok;
    if (pd.ok) {
      drunk_warp = false;
      for (const auto& st : pd.render_block)
        if (st.op == pattern::RenderStmt::Op::Warp) drunk_warp = true;
    }
    check(drunk_warp, "drunk: is sugar for a warp op");
  }

  // 4. `over` resolution check fails loud on an unknown clock (no silent-zero).
  {
    auto pr = parse("pattern x for 100f { image concept zoom (curve 0 -> 1 over nope) }");
    check(!pr.ok && pr.error.find("nope") != std::string::npos,
          "resolution: `over nope` is a hard parse error");
  }

  // 5. Every shipped v3 built-in parses and lowers to a runnable tree.
  {
    const char* names[] = {"", "accelerate", "slow_flash", "sub_text",     "flash_text",
                           "simple",         "super_parallel", "animation", "super_fast"};
    for (uint32_t t = 1; t <= 8; ++t) {
      auto pr = parse(builtin::pattern_source_v3(t));
      check(pr.ok, std::string("builtin ") + names[t] + ": parses" +
                       (pr.ok ? "" : (" -- " + pr.error)));
      if (pr.ok) {
        Cycler* root = pattern::compile(pr.root);
        check(root && root->length() > 0,
              std::string("builtin ") + names[t] + ": lowers to a runnable tree (len=" +
                  std::to_string(root ? root->length() : 0) + ")");
        delete root;
      }
    }
  }

  std::cout << "\n" << (g_fail ? "FAILED: " : "all v3 grammar checks passed (")
            << g_fail << (g_fail ? " failed\n" : " failures)\n");
  return g_fail ? 1 : 0;
}
