// Headless tests for the Framing-B authoring primitives (divide, generate, ...).
// Each proves the primitive lowers to the expected schedule / firing pattern,
// isolated (cyclers + compiler + parser only).
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
  int failures = 0;
  void check(bool cond, const std::string& what)
  {
    if (!cond) {
      std::cerr << "CHECK FAILED: " << what << "\n";
      ++failures;
    }
  }

  // Count how many times the (single) effect fires over `frames`, by logging.
  uint32_t count_fires(const pattern::Node& root, uint32_t frames)
  {
    uint32_t fires = 0;
    Cycler* tree =
        pattern::compile(root, [&](const pattern::Node& n) -> std::function<void()> {
          if (n.effects.empty()) {
            return {};
          }
          return [&fires] { ++fires; };
        });
    for (uint32_t f = 0; f < frames; ++f) {
      tree->advance(true);
    }
    return fires;
  }

  pattern::Node leaf_every(uint32_t length, uint32_t divide)
  {
    pattern::Node n;
    n.type = pattern::Node::Type::Action;
    n.length = length;
    n.divide = divide;
    n.effects.push_back(pattern::Effect{});  // one effect so the leaf has behaviour
    return n;
  }

  pattern::Effect kind(pattern::Effect::Kind k)
  {
    pattern::Effect e;
    e.kind = k;
    return e;
  }

  // Run a burst node and count base vs burst firings (distinguished by effect kind)
  // plus how many frames the node reports being in a burst.
  void run_burst(const pattern::Node& root, uint32_t frames, uint32_t& base, uint32_t& burst,
                 uint32_t& in_burst_frames)
  {
    base = burst = in_burst_frames = 0;
    Cycler* tree =
        pattern::compile(root, [&](const pattern::Node& n) -> std::function<void()> {
          if (n.effects.empty()) {
            return {};
          }
          auto k = n.effects[0].kind;
          return [&base, &burst, k] {
            if (k == pattern::Effect::Kind::Themes) {
              ++base;
            } else {
              ++burst;
            }
          };
        });
    for (uint32_t f = 0; f < frames; ++f) {
      tree->advance(true);
      if (tree->index() == 1) {
        ++in_burst_frames;
      }
    }
  }
}

int main()
{
  // ---- divide: a length-1 leaf fires every frame; divide N runs effects every Nth.
  check(count_fires(leaf_every(1, 1), 300) == 300, "divide 1 fires every frame");
  check(count_fires(leaf_every(1, 3), 300) == 100, "divide 3 fires every 3rd frame");
  check(count_fires(leaf_every(1, 5), 300) == 60, "divide 5 fires every 5th frame");

  // ---- divide via the parser.
  pattern::ParseResult r = pattern::parse("pattern p { one { every 1 divide 4 : themes } }");
  check(r.ok, "divide parses");
  if (r.ok) {
    check(count_fires(r.pattern.root, 400) == 100, "parsed divide 4 fires every 4th frame");
  }
  pattern::ParseResult bad = pattern::parse("pattern p { one { every 1 divide 0 : themes } }");
  check(!bad.ok, "divide 0 is rejected");

  // ---- generate: ascending expansion with an [expression] in the length.
  {
    auto g = pattern::parse("pattern p { seq { generate L from 1 to 3 { every [L * 10] : themes } } }");
    check(g.ok, "generate parses");
    if (g.ok) {
      Cycler* t = pattern::compile(g.pattern.root);
      check(t->children().size() == 3, "generate produced 3 children");
      check(t->length() == 60, "generate lengths 10+20+30 = 60 (got "
                                   + std::to_string(t->length()) + ")");
    }
  }
  // Descending range works too.
  {
    auto g = pattern::parse("pattern p { seq { generate L from 3 to 1 { every [L * 10] : themes } } }");
    check(g.ok && pattern::compile(g.pattern.root)->children().size() == 3,
          "descending generate produces 3 children");
  }
  // The real ACCELERATE shape: 45 segments with an integer-power falloff in the count.
  {
    auto g = pattern::parse(
        "pattern p { seq { generate L from 56 to 12 { "
        "repeat [1 + (56 - L) ^ 6 / 56 ^ 5] every [L] : themes } } }");
    check(g.ok, "ACCELERATE-shaped generate parses");
    if (g.ok) {
      Cycler* t = pattern::compile(g.pattern.root);
      check(t->children().size() == 45, "ACCELERATE generate produces 45 segments (got "
                                            + std::to_string(t->children().size()) + ")");
      check(t->length() > 0, "ACCELERATE generate has positive length");
    }
  }
  // An [expr] referencing an unbound variable is a parse error.
  check(!pattern::parse("pattern p { one { every [X + 1] : themes } }").ok,
        "unbound expression variable is rejected");

  // ---- burst: base loop interrupted by bursts (base=themes, burst=font).
  {
    // chance 0 = never burst: base fires every period, never in a burst.
    pattern::Node n;
    n.type = pattern::Node::Type::Burst;
    n.length = 100;
    n.burst_period = 10;
    n.burst_chance_den = 0;
    n.effects.push_back(kind(pattern::Effect::Kind::Themes));
    n.burst_effects.push_back(kind(pattern::Effect::Kind::Font));
    uint32_t base, burst, in_burst;
    run_burst(n, 100, base, burst, in_burst);
    check(base == 10, "never-burst: base fires every period (got " + std::to_string(base) + ")");
    check(burst == 0, "never-burst: no burst firings");
    check(in_burst == 0, "never-burst: never reports in-burst");
  }
  {
    // chance 1 = always burst (deterministic), fixed duration 3, cooldown 2.
    pattern::Node n;
    n.type = pattern::Node::Type::Burst;
    n.length = 200;
    n.burst_period = 10;
    n.burst_chance_den = 1;
    n.burst_cooldown = 2;
    n.burst_dur_min = 3;
    n.burst_dur_max = 3;
    n.effects.push_back(kind(pattern::Effect::Kind::Themes));
    n.burst_effects.push_back(kind(pattern::Effect::Kind::Font));
    uint32_t base, burst, in_burst;
    run_burst(n, 200, base, burst, in_burst);
    check(burst > 0, "always-burst: bursts fire");
    check(base > 0, "always-burst: base fires during cooldown");
    check(burst > base, "always-burst: mostly bursting");
    check(in_burst > 0, "always-burst: reports in-burst frames");
  }
  // burst parses from the DSL.
  {
    auto b = pattern::parse(
        "pattern p { burst { length 2048 period 8 chance 12 cooldown 8 duration 8 16 "
        "base: image alternate -> current, text word alternate "
        "burst: anim alternate } }");
    check(b.ok, "burst parses (got error: " + b.error + ")");
    if (b.ok) {
      Cycler* t = pattern::compile(b.pattern.root);
      check(t->length() == 2048, "parsed burst length is 2048");
    }
  }
  check(!pattern::parse("pattern p { burst { period 8 base: themes } }").ok,
        "burst without length is rejected");

  if (failures == 0) {
    std::cout << "PASS: primitives (divide, generate, burst)\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
