#include <trance/visual/builtin_patterns.h>

// The 8 built-ins re-authored in the v2 intent grammar (docs/spec-grammar-v2.md),
// parsed by patternv2::parse and lowered to the same pattern::Node the v1 sources
// produce. Every pattern's compiler-inserted init fires `themes` (+ font + spiral_new),
// so each cycles the session's themes exactly as the originals do.
//
// SCOPE: these are SIMPLIFIED smoke versions using the grammar slice that exists today
// (image/word/caption/subtext streams + spiral, beat must divide the phase). They keep
// the felt skeleton (sectioning, cut rate, layering intent) but drop the constructs the
// parser does not yet support -- the accelerate ramp, every-third anim, sub_speed,
// crossfade, the super_fast bursts. Restoring those is the continuing grammar work; this
// set exists to prove v2 drives the real engine end-to-end without crashing.
namespace
{
  // 1 ACCELERATE -- fast alternate cuts (ramp not yet in the grammar).
  const char* kAccelerate = R"(
pattern accelerate {
  phase "Ramp" for 2048f {
    description "Fast reward-theme cuts with a quick spiral."
    image reward every 16
    spiral rate 3
  }
}
)";

  // 2 SLOW_FLASH -- slow concept phase, then fast alternate phase, twice.
  const char* kSlowFlash = R"(
pattern slow_flash repeat 2 {
  deepen "Slow" for 1024f {
    description "Slow concept flashes every 64; gentle spiral; caption underneath."
    image concept every 64
    caption concept every 64
    spiral rate 2
  }
  phase "Fast" for 512f {
    description "Fast reward flashes every 8; reward words; quicker spiral. The image zoom sweeps across the WHOLE fast section (section clock) instead of resetting per flash."
    image reward every 8 zoom 0.5 over section
    word reward every 16
    spiral rate 4
  }
}
)";

  // 3 SUB_TEXT -- image with a subtext line.
  const char* kSubText = R"(
pattern sub_text {
  phase "Main" for 1536f {
    description "Runtime image every 48 with a reward subtext line."
    image runtime every 48
    subtext reward every 48
    spiral rate 4
  }
}
)";

  // 4 FLASH_TEXT -- image + foreground word.
  const char* kFlashText = R"(
pattern flash_text {
  phase "Main" for 1024f {
    description "Reward image every 64 with a co-timed reward word."
    image reward every 64
    word reward every 64
    caption concept every 32
    spiral rate 2
  }
}
)";

  // 5 PARALLEL / SIMPLE -- one steady image.
  const char* kSimple = R"(
pattern simple {
  phase "Main" for 2048f {
    description "One steady runtime image every 64; caption; spiral."
    image runtime every 64
    caption concept every 32
    spiral rate 3
  }
}
)";

  // 6 SUPER_PARALLEL -- denser image cuts + word (true 3-lane stagger not yet in grammar).
  const char* kSuperParallel = R"(
pattern super_parallel {
  phase "Interleave" for 1152f {
    description "Dense concept cuts every 16 with a runtime word."
    image concept every 16
    word runtime every 32
    spiral rate 3
  }
}
)";

  // 7 ANIMATION -- image + caption (anim-as-subject not yet in grammar).
  const char* kAnimation = R"(
pattern animation {
  phase "Main" for 1024f {
    description "Concept image every 32 with a runtime caption."
    image concept every 32
    caption runtime every 32
    spiral rate 3
  }
}
)";

  // 8 SUPER_FAST -- rapid runtime cuts (bursts/FSM dropped per the spec review).
  const char* kSuperFast = R"(
pattern super_fast {
  phase "Blitz" for 2048f {
    description "Rapid runtime cuts every 8 frames."
    image runtime every 8
    spiral rate 3
  }
}
)";
}

namespace builtin
{
  std::string pattern_source_v2(uint32_t visual_type)
  {
    switch (visual_type) {
    case 1:  return kAccelerate;
    case 2:  return kSlowFlash;
    case 3:  return kSubText;
    case 4:  return kFlashText;
    case 5:  return kSimple;
    case 6:  return kSuperParallel;
    case 7:  return kAnimation;
    case 8:  return kSuperFast;
    default: return {};
    }
  }
}
