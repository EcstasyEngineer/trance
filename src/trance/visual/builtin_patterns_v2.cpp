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
  // 1 ACCELERATE -- the cadence ramp: cuts shorten 56 -> 12 with a pow6 dwell, image
  // zoom sweeping continuously over the ramp. Lowers to the same 2770-frame ramp the v1
  // built-in does (per-band slot/upload aside).
  const char* kAccelerate = R"(
pattern accelerate {
  escalate "Ramp" for auto {
    description "Cuts accelerate from every 56 to every 12 frames; the image zoom intensifies across the whole ramp; quick spiral."
    curve pace from 56 to 12 ease late
    image concept every pace zoom 0.5
    spiral rate 3
  }
}
)";

  // 2 SLOW_FLASH -- slow concept phase, then fast alternate phase, twice.
  const char* kSlowFlash = R"(
pattern slow_flash repeat 2 {
  deepen "Slow" for 1024f {
    description "Slow concept flashes every 64; gentle spiral; caption underneath."
    image concept every 64 zoom 0.5
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

  // 3 SUB_TEXT -- the subtext cadence slows from every 12 to every 48 across the section
  // (the sub_speed ramp, expressed as a curve); runtime image with an every-third anim.
  const char* kSubText = R"(
pattern sub_text {
  phase "Main" for auto {
    description "A runtime image (every third animates); a reward subtext whose cadence slows from every 12 to every 48 across the section; quick spiral."
    curve subrate from 12 to 48
    image runtime every 48 zoom 0.5 anim every 3rd
    subtext reward every subrate
    spiral rate 4
  }
}
)";

  // 4 FLASH_TEXT -- a per-flash pulse: each beat the reward image zooms 0->100% while its
  // brightness fades 0->100->0 (a triangle). Composed from explicit modifiers (zoom +
  // `fade inout`), no baked construct. (A continuous A->B dissolve would need a copy/prev
  // handoff; this is the per-flash pulse spec instead.)
  const char* kFlashText = R"(
pattern flash_text {
  phase "Main" for 1024f {
    description "Each beat a reward image zooms in while its brightness fades up then down (a pulse); reward word; caption; spiral."
    image reward every 64 zoom 1 brightness 1 fade inout
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
    description "One steady runtime image every 64; every third showing animates (the accent); caption; spiral."
    image runtime every 64 zoom 0.5 anim every 3rd
    caption concept every 32
    spiral rate 3
  }
}
)";

  // 6 SUPER_PARALLEL -- three staggered image layers compositing at once (the stack),
  // brightest in front, plus a runtime word.
  const char* kSuperParallel = R"(
pattern super_parallel {
  phase "Interleave" for 1152f {
    description "Three image layers each cutting every 96, staggered by 32 frames, composited front-to-back; runtime word."
    image concept -> a every 96 stagger 0 zoom 0.5
    image concept -> b every 96 stagger 32 zoom 0.5
    image reward -> c every 96 stagger 64 zoom 0.5
    word runtime every 32
    spiral rate 3
  }
}
)";

  // 7 ANIMATION -- anim-as-subject: the image is ALWAYS drawn as its animated form, so
  // the animation is the main event rather than an accent.
  const char* kAnimation = R"(
pattern animation {
  phase "Main" for 1024f {
    description "The concept image plays as animation throughout (anim is the subject); runtime caption; spiral."
    image concept every 32 zoom 0.5 anim
    caption runtime every 32
    spiral rate 3
  }
}
)";

  // 8 SUPER_FAST -- rapid cuts with random variety (the FSM replaced by chance/anim, per
  // the spec review: same effect, not the same exact frames).
  const char* kSuperFast = R"(
pattern super_fast {
  phase "Blitz" for 2048f {
    description "Rapid runtime cuts every 8; every fourth animates; words flash about a quarter of the time; quick spiral."
    image runtime every 8 zoom 0.5 anim every 4th
    word concept every 8 chance 0.25
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
