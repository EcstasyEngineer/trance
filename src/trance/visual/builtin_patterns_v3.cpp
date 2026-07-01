#include <trance/visual/builtin_patterns.h>

// The 8 built-ins authored in the v3 intent grammar (docs/spec-grammar-v3.md): two nouns
// (pattern, effect) and one rule (every numeric is a modulator riding the enclosing pattern's
// clock). Patterns nest; crossfade EMERGES from copy + cur/prev + source-over fade-in (no baked
// keyword); spiral speed / zoom / fade are one curve-drivable class; super_fast uses randomness
// primitives instead of a hand-rolled FSM. These are faithful-in-feel, not frame-identical, to
// the originals -- the project's "supersede, not parity" stance.
namespace
{
  // 1 ACCELERATE -- a true accelerating cadence: one continuous ramp cutting from a 56f flash
  // down to a 12f flash over 46 sampled steps (13.2/Extension #3), eased `late` so the cuts stay
  // slow for a while before rushing to the fast end. Spiral speed rides the WHOLE pattern's
  // parent clock (`over accelerate`), not any one segment, so it accelerates smoothly across the
  // entire 2772f span independent of the ramp's per-segment/un-id'd-elsewhere honest limit.
  // 46 steps (not the spec's illustrative ~45) is the exact step count that makes the sampled
  // segment durations sum to 2772f with zero rounding remainder to fold -- see sample_ramp.
  const char* kAccelerate = R"(
pattern accelerate for 2772f {
  every ramp 56f -> 12f steps 46 ease late -> cut {
    image concept zoom 0.5
  }
  spiral speed (curve 2 -> 6 over accelerate)
})";

  // 2 SLOW_FLASH -- a slow concept phase then a fast reward phase (sequenced).
  const char* kSlowFlash = R"(
pattern slow_flash for 1536f seq {
  pattern slow for 1024f {
    every 64f { image concept zoom 0.5 }
    every 64f { caption concept }
    spiral speed 2
  }
  pattern fast for 512f {
    every 8f  { image reward }
    every 16f { word reward }
    spiral speed 4
  }
})";

  // 3 SUB_TEXT -- a steady runtime image (every third animates) under a reward subtext stream.
  const char* kSubText = R"(
pattern sub_text for 1024f {
  every 64f { image runtime zoom 0.5 anim every 3rd }
  every 32f { subtext reward }
  spiral speed 4
})";

  // 4 FLASH_TEXT -- a continuous IMAGE crossfade: reward images dissolve one into the next via
  // the copy handoff (cur -> prev). Each image zooms across two 64f halves: cur does 0->half,
  // then after the copy prev does half->full. The old layer is drawn first and the new layer
  // fades in above it, matching the original source-over blend without a baked crossfade keyword.
  // Word + caption accents. (A true text-on-text dissolve needs the deferred text-register
  // extension, Ext#4.)
  const char* kFlashText = R"(
pattern flash_text for 1024f {
  pattern life for 128f loop 8 {
    every 64f -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image reward -> cur fade in zoom (curve 0 -> 0.5)
    }
    every 64f { word reward }
  }
  every 32f { caption concept }
  spiral speed 2
})";

  // 5 SIMPLE -- one steady runtime image; every third showing animates (the accent).
  const char* kSimple = R"(
pattern simple for 2048f {
  every 64f { image runtime zoom 0.5 anim every 3rd }
  every 32f { caption concept }
  spiral speed 3
})";

  // 6 SUPER_PARALLEL -- three image layers compositing at once (the stack) + a runtime word.
  const char* kSuperParallel = R"(
pattern super_parallel for 1152f {
  every 96f { image concept -> a zoom 0.5 }
  every 96f { image concept -> b zoom 0.5 }
  every 96f { image reward  -> c zoom 0.5 }
  every 32f { word runtime }
  spiral speed 3
})";

  // 7 ANIMATION -- anim-as-subject: the concept image plays as animation throughout.
  const char* kAnimation = R"(
pattern animation for 1024f {
  every 32f { image concept zoom 0.5 anim }
  every 32f { caption runtime }
  spiral speed 3
})";

  // 8 SUPER_FAST -- rapid runtime cuts, occasionally interrupted by a short random ANIMATED
  // burst with a cooldown (13.1, issue #26): the felt "rapid-cut base, then it suddenly plays
  // an animation for a bit, then settles back down" shape the old hand-rolled FSM gave, now
  // built from the burst/random primitive instead of a baked C++ state machine (same effect,
  // not the same frames). The accent word rides its own independent chance cadence, same as
  // before the burst reauthor.
  const char* kSuperFast = R"(
pattern super_fast for 2048f {
  burst -> rapid period 8f chance 1/24 cooldown 32f duration 32f..96f {
    base  { image runtime zoom 0.5 anim every 4th }
    burst { image runtime zoom 0.5 anim }
  }
  every 8f { word concept chance 0.25 }
  spiral speed 3
})";
}

namespace builtin
{
  std::string pattern_source_v3(uint32_t visual_type)
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
