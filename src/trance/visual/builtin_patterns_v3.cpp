#include <trance/visual/builtin_patterns.h>

// The 8 built-ins authored in the v3 intent grammar (docs/spec-grammar-v3.md): two nouns
// (pattern, effect) and one rule (every numeric is a modulator riding the enclosing pattern's
// clock). Patterns nest; crossfade EMERGES from copy + cur/prev + source-over fade-in (no baked
// keyword); spiral speed / zoom / fade are one curve-drivable class; super_fast uses randomness
// primitives instead of a hand-rolled FSM. These are faithful-in-feel, not frame-identical, to
// the originals -- the project's "supersede, not parity" stance. Reference for "feel": the
// pre-grammar hand-written visuals (visual.cpp at upstream ae7d94c), whose key texture is that
// EVERY image zooms over its own on-screen life -- so zoom modulators here are `curve` rides on
// the pulling cadence's clock, never constants (a constant zoom is a static magnification).
namespace
{
  // 1 ACCELERATE -- a true accelerating cadence: one continuous ramp cutting from a 56f flash
  // down to a 12f flash, eased `early` so the cut length rushes off the slow end and dwells at
  // the fast end. This matches the original's repeat curve (count = 1 + d^6/56^5, i.e. the
  // fastest cuts repeated ~14x, putting ~25% of the runtime at <=16f cuts and ~38% at <=20f):
  // with cubic ease-early and 120 once-each sampled segments the sampled distribution matches
  // that within a point or two, so the pattern both GETS fast quickly and STAYS fast. (The
  // first authoring used `ease late` -- dwell at the SLOW end -- which took ~3x too long to
  // accelerate and only touched 12f for the last ~3%: the exact regression reported against it.)
  const char* kAccelerate = R"(
pattern accelerate for 2772f {
  every ramp 56f -> 12f steps 120 ease early -> cut {
    image concept zoom (curve 0 -> 0.5)
    word concept chance 0.5
  }
  spiral speed (curve 1 -> 4 over accelerate)
})";

  // 2 SLOW_FLASH -- a slow concept phase then a fast reward phase, the pair looped twice per
  // visual like the original's RepeatCycler{2, slow->fast}. Slow images zoom over their own 64f
  // life and every 2nd one animates; the fast phase's zoom climbs across the WHOLE phase (the
  // original's index-driven creep), not per image -- at 8f per cut a per-image zoom would strobe.
  const char* kSlowFlash = R"(
pattern slow_flash for 1536f loop 2 seq {
  pattern slow for 1024f {
    every 64f { image concept zoom (curve 0 -> 0.5) anim every 2nd }
    every 64f { caption concept }
    spiral speed 2
  }
  pattern fast for 512f {
    every 8f  { image reward zoom (curve 0 -> 0.8 over fast) }
    every 16f { word reward }
    spiral speed 4
  }
})";

  // 3 SUB_TEXT -- a steady runtime image (every third animates) under a reward subtext stream.
  const char* kSubText = R"(
pattern sub_text for 1024f {
  every 64f { image runtime zoom (curve 0 -> 0.375) anim every 3rd }
  every 32f { subtext reward }
  spiral speed 4
})";

  // 4 FLASH_TEXT -- a continuous IMAGE crossfade: reward images dissolve one into the next via
  // the copy handoff (cur -> prev). Each image zooms across two 64f halves: cur does 0->0.4,
  // then after the copy prev does 0.4->0.8 -- the original's exact range. (Do NOT run image zoom
  // near 1.0: zoom projects toward the near plane, and by ~0.9+ the mirror-tiled quad grid
  // degenerates into a garbled magnified mosaic -- the "jigsaw" regression this range fixes.)
  // The old layer is drawn first and the new layer fades in above it, matching the original
  // source-over blend without a baked crossfade keyword. Word + caption accents. (A true
  // text-on-text dissolve needs the deferred text-register extension, Ext#4.)
  const char* kFlashText = R"(
pattern flash_text for 1024f {
  pattern life for 128f loop 8 {
    every 64f -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.4 -> 0.8)
      image reward -> cur fade in zoom (curve 0 -> 0.4)
    }
    every 64f { word reward }
  }
  every 32f { caption concept }
  spiral speed 2
})";

  // 5 SIMPLE -- one steady runtime image zooming over its life; every third showing animates.
  const char* kSimple = R"(
pattern simple for 2048f {
  every 64f { image runtime zoom (curve 0 -> 0.5) anim every 3rd }
  every 32f { caption concept }
  spiral speed 3
})";

  // 6 SUPER_PARALLEL -- three image layers compositing at once, staggered a third of a cycle
  // apart (the original's OffsetCycler{i * 32} over a 96f lane) with source-over alphas
  // 1 / 0.5 / 0.33 (the original's 1/(1+i)) so all three actually READ as a stack -- three
  // full-alpha layers just show whichever drew last. Lane a carries the animation accent.
  const char* kSuperParallel = R"(
pattern super_parallel for 1152f {
  every 96f            { image concept -> a zoom (curve 0 -> 0.875) anim every 2nd }
  every 96f offset 32f { image concept -> b alpha 0.5 zoom (curve 0 -> 0.875) }
  every 96f offset 64f { image reward  -> c alpha 0.33 zoom (curve 0 -> 0.875) }
  every 32f { word runtime }
  spiral speed 3
})";

  // 7 ANIMATION -- animation-as-subject (a rolling always-animated layer), with a still image
  // crossfading in and out above it on the offbeat -- the original drew BOTH (animation base +
  // periodic render_image reveal), which is what separates this visual from a bare video player.
  const char* kAnimation = R"(
pattern animation for 1024f {
  every 64f { image runtime zoom (curve 0 -> 0.625) anim }
  every 64f offset 32f { image reward -> still fade inout zoom (curve 0.5 -> 0.625) }
  every 32f { caption runtime }
  spiral speed 3
})";

  // 8 SUPER_FAST -- rapid 8f current/next cuts, occasionally interrupted by a short random
  // ANIMATED burst with a cooldown (13.1, issue #26). The burst picks its animation ONCE on
  // entry (`enter { anim runtime }`) and then just renders it (`draw cur ... anim` -- a pure
  // render, no per-period re-roll); base cuts stay still images. Base and burst draws are
  // FSM-gated (rapid.index) so the burst's animation only paints during a burst -- an ungated
  // always-anim draw here painted one animation over the entire pattern, which is the "no cuts
  // at all, just one animation playing" regression. Word accent on its own chance cadence.
  const char* kSuperFast = R"(
pattern super_fast for 2048f {
  burst -> rapid period 8f chance 1/12 cooldown 64f duration 64f..128f {
    base  { image runtime zoom 0.15 }
    enter { anim runtime }
    burst { draw cur zoom 0.4 anim }
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
