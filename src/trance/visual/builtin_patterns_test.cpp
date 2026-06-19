// Guards the built-in pattern DSL sources: every non-empty one must parse and
// compile. Once a hardcoded visual is deleted its source has no other reference, so
// this is what catches a typo in builtin_patterns.cpp (and keeps Director's
// fail-fast from ever firing in a tested build).
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser.h>

#include <iostream>
#include <string>

int main()
{
  int failures = 0;
  int found = 0;
  for (uint32_t t = 1; t <= 8; ++t) {
    std::string source = builtin::pattern_source(t);
    if (source.empty()) {
      continue;
    }
    ++found;
    pattern::ParseResult r = pattern::parse(source);
    if (!r.ok) {
      std::cerr << "built-in " << t << " parse failed: " << r.error << "\n";
      ++failures;
      continue;
    }
    Cycler* tree = pattern::compile(r.pattern.root);
    if (!tree || tree->length() == 0) {
      std::cerr << "built-in " << t << " compiled to an empty tree\n";
      ++failures;
    }
  }

  if (failures == 0) {
    std::cout << "PASS: " << found << " built-in pattern source(s) parse and compile\n";
    return 0;
  }
  return 1;
}
