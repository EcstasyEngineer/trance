// Standalone exploration, intentionally outside CTest. These print existing
// permissive behaviors; they are findings to review, not acceptance contracts.
#include <trance/visual/pattern_parser_v3.h>
#include <trance/visual/render_eval.h>
#include <iostream>

int main()
{
  for (const auto* source : {
      "pattern scene for 8f {} trailing_words",
      "pattern scene for 8f {} pattern ignored for 8f { image primary }",
      "pattern scene for 8f { copy cur -> prev image primary }",
      "pattern scene for 8f { image primary zoom [this.progres] }",
      "pattern scene for 8f { image primary anim every 0th }"}) {
    const auto parsed = patternv3::parse(source);
    std::cout << source << "\n  ok=" << parsed.ok
              << " warnings=" << parsed.warnings.size()
              << " draws=" << parsed.render_block.size()
              << " error=" << parsed.error << '\n';
  }
  const pattern::Registers regs;
  const pattern::NodeMap nodes;
  for (const auto* expr : {"missing.progress", "mystery(0.75)",
                           "1 trailing", "(-1)^0.5", "1 / 0"})
    std::cout << expr << " => "
              << pattern::eval_expr(expr, 0, regs, nodes, nullptr) << '\n';
}
