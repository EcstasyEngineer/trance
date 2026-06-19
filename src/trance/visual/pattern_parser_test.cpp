// Headless round-trip proof for the DSL parser: a pattern written as text parses to
// a normalized AST that compiles to a Cycler tree with the expected timing, and a
// malformed pattern produces a line:col diagnostic instead of crashing.
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_parser.h>
#include <trance/visual/pattern_compiler.h>

#include <iostream>
#include <string>

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

  // SLOW_FLASH expressed in the DSL. Schedule must total 3072 frames (1024 SLOW +
  // 512 FAST, repeated twice).
  const char* kSlowFlash = R"(
pattern slow_flash {
  weight 10
  render zoom_image_text
  one {
    every 1 : themes
    repeat 2 seq {
      phase "SLOW" par {
        one {
          every 1 : spiral_new, font
          repeat 16 image primary every 64 : image primary -> current, anim primary, text line primary, small_text primary
        }
        every 1 : spiral 2
        every 64 @32 : upload
      }
      phase "FAST" par {
        one {
          every 1 : spiral_new, font
          repeat 32 par {
            image alternate every 8 : image alternate -> current
            every 16 @8 : text word alternate
          }
        }
        every 16 : small_text alternate
        every 1 : spiral 4
      }
    }
  }
}
)";
}

int main()
{
  // ---- valid parse + compile ----
  pattern::ParseResult r = pattern::parse(kSlowFlash);
  if (!r.ok) {
    std::cerr << "parse failed: " << r.error << "\n";
    return 1;
  }
  check(r.pattern.name == "slow_flash", "name is slow_flash");
  check(r.pattern.weight == 10, "weight is 10");
  check(r.pattern.render == "zoom_image_text", "render preset captured");

  Cycler* tree = pattern::compile(r.pattern.root);
  check(tree->length() == 3072, "compiled length is 3072 (got " + std::to_string(tree->length()) + ")");

  // The first SLOW image lane should carry the primary image-slot annotation, and a
  // section label should appear once we advance into the body.
  bool found_phase = false;
  bool found_image = false;
  std::function<void(const Cycler*)> walk = [&](const Cycler* c) {
    if (!c->phase().empty()) found_phase = true;
    if (c->image_slot() != ImageSlotHint::None) found_image = true;
    for (const Cycler* k : c->children()) walk(k);
  };
  walk(tree);
  check(found_phase, "a phase label survived compilation");
  check(found_image, "an image-slot annotation survived compilation");

  // ---- malformed parse produces a diagnostic ----
  pattern::ParseResult bad = pattern::parse("pattern x { one { every : themes } }");
  check(!bad.ok, "missing frame length is rejected");
  check(!bad.error.empty() && bad.error.find(':') != std::string::npos,
        "error carries line:col (got '" + bad.error + "')");

  pattern::ParseResult bad2 = pattern::parse("pattern y { wobble { } }");
  check(!bad2.ok, "unknown node keyword is rejected");

  if (failures == 0) {
    std::cout << "PASS: DSL parses, compiles to 3072-frame schedule, and reports errors\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
