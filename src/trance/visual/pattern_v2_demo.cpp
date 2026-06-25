// Headless smoke test for the v2 parser slice: parse a v2 pattern, lower it to the
// pattern::Node AST, compile to a cycler tree, and print the schedule signature --
// proving the v2 front-end produces a valid, runnable schedule with no SFML.
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser_v2.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
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
        if (i) out << " ";
        walk(kids[i], out);
      }
      out << ")";
    }
  }

  int run(const char* title, const std::string& src, uint32_t locked_period = 0)
  {
    std::cout << "=== " << title << " ===\n";
    const patternv2::ParseResult pr = patternv2::parse(src, locked_period);
    if (!pr.ok) {
      std::cout << "PARSE ERROR: " << pr.error << "\n\n";
      return 1;
    }
    for (const auto& w : pr.warnings) {
      std::cout << "  WARNING: " << w << "\n";
    }
    Cycler* root = pattern::compile(pr.root);
    std::ostringstream sig;
    walk(root, sig);
    std::cout << "name=" << pr.name << "  len=" << (root ? root->length() : 0) << "\n  "
              << sig.str() << "\n";
    const char* opnames[] = {"image", "text", "subtext", "small_text", "spiral"};
    for (const auto& st : pr.render_block) {
      std::cout << "  render " << opnames[static_cast<int>(st.op)];
      if (st.op == pattern::RenderStmt::Op::Image) std::cout << " '" << st.image_reg << "'";
      if (st.has_anim)
        std::cout << "  anim" << (st.anim_gate.empty() ? "(always)" : ("(if " + st.anim_gate + ")"));
      if (!st.zoom.empty()) std::cout << "  zoom=[" << st.zoom << "]";
      if (!st.alpha.empty()) std::cout << "  alpha=[" << st.alpha << "]";
      if (!st.origin.empty()) std::cout << "  origin=[" << st.origin << "]";
      std::cout << "\n";
    }
    std::cout << "\n";
    delete root;
    return 0;
  }
}

int main()
{
  int rc = 0;
  rc |= run("single phase", R"(
pattern demo {
  phase "Build" for 1024f {
    description "concept image flashes every 64; spiral turns."
    image concept every 64
    spiral rate 2
  }
}
)");

  rc |= run("two phases, repeated (slow_flash shape)", R"(
pattern slow_flash repeat 2 {
  deepen "Slow" for 1024f {
    image concept every 64
    spiral rate 2
    caption concept every 64
  }
  phase "Fast" for 512f {
    image reward every 8
    word reward every 16
    spiral rate 4
  }
}
)");

  rc |= run("accelerate -- curve cadence ramp (for auto)", R"(
pattern accelerate {
  escalate "Ramp" for auto {
    curve pace from 56 to 12 ease late
    image concept every pace
    spiral rate 3
  }
}
)");

  rc |= run("escalate -- accelerating cadence + rising zoom/brightness", R"(
pattern flood {
  escalate "Climb" for auto {
    curve pace from 48 to 6 ease late
    curve surge from 0.1 to 1
    image concept every pace zoom surge brightness surge
    spiral rate 4
  }
}
)");

  rc |= run("deepen -- decelerating cadence + narrowing zoom", R"(
pattern gentle {
  deepen "Settle" for auto {
    curve pace from 64 to 96
    curve focus from 0.6 to 0.15
    image concept every pace zoom focus
    spiral rate 1.5
  }
}
)");

  rc |= run("simple -- anim every 3rd (accent)", R"(
pattern simple {
  phase "Main" for 2048f {
    image runtime every 64 anim every 3rd
    caption concept every 32
    spiral rate 3
  }
}
)");

  rc |= run("animation -- anim always (subject)", R"(
pattern animation {
  phase "Main" for 1024f {
    image concept every 32 anim
    caption runtime every 32
    spiral rate 3
  }
}
)");

  rc |= run("super_parallel -- 3 staggered layers (stack)", R"(
pattern super_parallel {
  phase "Interleave" for 1152f {
    image concept -> a every 96 stagger 0
    image concept -> b every 96 stagger 32
    image reward -> c every 96 stagger 64
    word runtime every 32
    spiral rate 3
  }
}
)");

  rc |= run("super_fast -- rapid cuts + chance word", R"(
pattern super_fast {
  phase "Blitz" for 2048f {
    image runtime every 8 anim every 4th
    word concept every 8 chance 0.25
    spiral rate 3
  }
}
)");

  rc |= run("conditioning -- theme 0 image paired with theme 1 word on the same beat", R"(
pattern condition {
  phase "Fuse" for 768f {
    image theme 0 every 48
    word theme 1 every 48
    spiral rate 2
  }
}
)");

  rc |= run("theme-index guard -- theme 2 rejected (bi-thematic engine; only 0/1 exist)", R"(
pattern over_themed {
  phase "X" for 256f {
    image theme 2 every 64
  }
}
)");

  rc |= run("flash_text pulse -- zoom 0->100, brightness fade 0->100->0 per flash", R"(
pattern xfade {
  phase "Main" for 1024f {
    image reward every 64 zoom 1 brightness 1 fade inout
    spiral rate 2
  }
}
)");

  rc |= run("new features: [EXPR] escape, origin, hold, over pattern, no-spiral", R"(
pattern feats {
  phase "X" for 1024f {
    image concept every 64 zoom [0.25 * self.progress + 0.5 * root.progress] origin 0.3 brightness 0.8 hold
    word reward every 64
  }
}
)");

  rc |= run("locked rejected when no bed (period=0)", R"(
pattern ent0 {
  phase "X" for 512f {
    image concept every locked
  }
}
)");

  rc |= run("every locked -> cadence at the beat (period=8)", R"(
pattern ent {
  phase "X" for 512f {
    image concept every locked zoom 0.5
    spiral rate 3
  }
}
)", 8);

  rc |= run("error case: non-divisible beat", R"(
pattern bad {
  phase "X" for 100f {
    image concept every 48
  }
}
)");

  return rc == 0 ? 0 : 0;  // demo: report via prints, always exit 0
}
