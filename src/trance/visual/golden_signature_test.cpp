// Golden schedule-signature harness for the 8 built-in patterns.
//
// Purpose (see docs/spec-grammar-v2.md §7 "blocking predecessor"): before the v2
// intent grammar is built, we freeze a stable signature of what each current
// built-in compiles to -- the SHAPE of its schedule (cycler node types + lengths
// + section/image annotations). When a built-in is later re-authored in the v2
// grammar, its lowered schedule must reproduce the same signature (or a
// deliberately-recorded new one). This is the "same effect, validated by
// structure" net that replaces the deleted byte-identity render harness.
//
// Headless on purpose: parser + compiler + cyclers pull in no SFML / protobuf, so
// this builds and runs with just a C++17 compiler. The schedule-only compile()
// overload makes every leaf a pure timer, so the signature is timing structure,
// which is exactly what distinguishes one pattern from another.
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
  // Pre-order serialization of the compiled cycler tree: "Type:length" per node,
  // with optional "#PHASE" (section label) and "@slot" (image-source hint) and
  // children in parentheses. This string IS the schedule signature.
  void walk(const Cycler* c, std::ostream& out)
  {
    if (!c) {
      out << "<null>";
      return;
    }
    out << c->type_name() << ":" << c->length();
    if (!c->phase().empty()) {
      out << "#" << c->phase();
    }
    if (c->image_slot() != ImageSlotHint::None) {
      out << "@" << static_cast<int>(c->image_slot());
    }
    const auto kids = c->children();
    if (!kids.empty()) {
      out << "(";
      for (std::size_t i = 0; i < kids.size(); ++i) {
        if (i) {
          out << " ";
        }
        walk(kids[i], out);
      }
      out << ")";
    }
  }

  // FNV-1a 64-bit, so a whole signature collapses to one comparable golden value.
  uint64_t fnv1a(const std::string& s)
  {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : s) {
      h ^= ch;
      h *= 1099511628211ull;
    }
    return h;
  }

  struct Builtin {
    uint32_t type;
    const char* name;
    uint64_t golden;  // frozen schedule-signature; a mismatch is a behavior change
  };

  // trance.proto VisualType values (see builtin_patterns.cpp). Goldens captured from
  // the schedule-only compile of each built-in at f50b9f8 -- the baseline a v2 re-port
  // (or any accidental compiler/parser change) is checked against.
  const Builtin kBuiltins[] = {
      {1, "accelerate",     0x8730be2abc472189ull},
      {2, "slow_flash",     0x6298b646b1c2de4aull},
      {3, "sub_text",       0x64b60424b6e27c18ull},
      {4, "flash_text",     0x33babc5ffe2a1705ull},
      {5, "simple",         0xb12440da28f61df3ull},
      {6, "super_parallel", 0xaede9f0eaaa004f5ull},
      {7, "animation",      0x0c7ae94d5ce94d1eull},
      {8, "super_fast",     0x8d12dc0341a537f7ull},
  };
}

int main()
{
  int failures = 0;
  for (const auto& b : kBuiltins) {
    const std::string src = builtin::pattern_source(b.type);
    const pattern::ParseResult pr = pattern::parse(src);
    if (!pr.ok) {
      std::cout << b.type << " " << b.name << "  PARSE-ERROR: " << pr.error << "\n";
      ++failures;
      continue;
    }
    Cycler* root = pattern::compile(pr.pattern.root);
    std::ostringstream sig;
    walk(root, sig);
    const std::string s = sig.str();
    const uint64_t h = fnv1a(s);
    const bool ok = (h == b.golden);
    std::cout << (ok ? "  ok  " : "FAIL  ") << b.type << " " << b.name
              << "  len=" << (root ? root->length() : 0) << "  sig=0x" << std::hex << h
              << std::dec << "\n";
    if (!ok) {
      std::cout << "      expected 0x" << std::hex << b.golden << std::dec << "\n      " << s
                << "\n";
      ++failures;
    }
    delete root;
  }
  std::cout << (failures ? "\nGOLDEN MISMATCH: " : "\nall schedules match golden: ")
            << (failures ? failures : static_cast<int>(sizeof(kBuiltins) / sizeof(kBuiltins[0])))
            << (failures ? " pattern(s) changed\n" : " patterns\n");
  return failures ? 1 : 0;
}
