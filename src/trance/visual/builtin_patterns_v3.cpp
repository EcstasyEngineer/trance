#include <trance/visual/builtin_patterns.h>

// The 8 built-ins authored in the v3 intent grammar (docs/spec-grammar-v3.md): two nouns
// (pattern, effect) and one rule (every numeric is a modulator riding the enclosing pattern's
// clock). Patterns nest; crossfade EMERGES from copy + cur/prev + source-over fade-in (no baked
// keyword); spiral speed / zoom / fade are one curve-drivable class; super_fast uses randomness
// primitives instead of a hand-rolled FSM. These are faithful-in-feel, not frame-identical, to
// the pre-grammar hand-written visuals, whose key texture is that EVERY image zooms over its
// own on-screen life -- so zoom modulators here are `curve` rides on the pulling cadence's
// clock, never constants (a constant zoom is a static magnification).
namespace
{
  // 1 ACCELERATE -- the up-ramp: one continuous accelerating cadence that titrates HARD into a
  // sustained strobe, churning theme every other image once it is up to speed. This is an
  // OWNER-SPEC re-author (issue #42), not a port -- it supersedes original-parity deliberately.
  //
  // Pacing. The ramp samples `steps` segments off the ease curve and then SCALES them to fill
  // the span, so A -> B sets the ramp's SHAPE and (span / steps) sets its absolute tempo --
  // more steps in fewer frames is a harder up-ramp, independent of the 56 -> 12 endpoints.
  // Both knobs moved: 2772f -> 2048f and 120 -> 140 steps. Against the previous authoring the
  // sampled ramp now spends ~46% of its runtime at <=16f cuts (was 27%) and first reaches a
  // <=16f cut at frame 1115 of 2048 -- 54% in, where it used to be 2034 of 2772, i.e. 73% in
  // and nearly over. 88 of the 140 cuts are <=14f, so the fast end is a sustained strobe
  // rather than a fly-by. 2048f was chosen over a tighter total because the arrival, not the
  // outro, is what "feels" long: landing the strobe at ~1100f makes the last ~900f read as
  // the payoff. `ease early` stays -- it is already the steepest ease the grammar has, and
  // the tightening is carried by the step count instead.
  //
  // Theme churn. `image alternate chance 0.5` (4.18) -- the hidden toggle flips at p=0.5 per
  // pull, so every new display is a coin flip between concept and reward. Unlike `runtime`
  // (an independent re-roll each fire) this is a stateful walk, so the world holds and pivots
  // rather than shimmering, and a lower chance would hold it longer.
  //
  // Lean-in. The original's macro-arc was a whole-run 0 -> 0.4 origin creep with only a small
  // +0.1 zoom pop per cut; the first v3 authoring collapsed that into a violent 0 -> 0.5 zoom
  // on EVERY cut, which reads as per-image punch with no sense of approach. Split back apart:
  // `origin` rides the whole `accelerate` clock, and `zoom` is the raw expr
  // origin + 0.1*cut -- the pop rides ON TOP of the creep, exactly the original's
  // `zoom = zoom_origin + .1f * progress`. The base term is load-bearing: the projection
  // shrinks the image grid whenever origin > zoom, and an absolute 0 -> 0.1 zoom spends
  // three quarters of the run in that regime (the black-bar bug).
  //
  // Animation bursts. `anim every 4th` restores the original's every-Nth live-motion cut
  // (N was rolled 2/4/8/16 per pass; 4 is the middle of that range and the one that keeps
  // motion present at both ends of the ramp without dominating the fast cuts).
  //
  // Word accents. `show 0..0.25` restores the original's stab duty -- text punches on for the
  // first quarter of each cut and is gone for the rest, instead of painting continuously
  // (27% of frames are inside the window; with `chance 0.5` on top the text actually paints
  // ~13% of the run). Fractional (not `0f..8f`) on purpose: the ramp re-parses this body per
  // sampled segment and the fast segments are as short as 7f, so a literal 8f window would be
  // a hard parse error on those -- and one failed segment fails the whole pattern. The
  // fraction also scales the stab with the tempo -- ~9 frames at the slow end tightening to
  // ~2 at the strobe, which is the accent getting sharper as the ride tightens.
  const char* kAccelerate = R"(
pattern accelerate for 2048f {
  every ramp 56f -> 12f steps 140 ease early -> cut {
    image alternate chance 0.5 zoom [0.4 * accelerate.progress + 0.1 * this.progress] origin (curve 0 -> 0.4 over accelerate) anim every 4th
    word concept show 0..0.25 chance 0.5
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
  // that VISITS above it on the offbeat -- the original drew BOTH (animation base + periodic
  // render_image reveal), which is what separates this visual from a bare video player.
  //
  // The still's envelope is the whole point and `fade inout` got it wrong. `fade inout` is a
  // whole-clock triangle (1 - abs(2p-1)): nonzero at nearly every frame, peaking for an
  // instant. Under it the animation is NEVER alone on screen, so "the animation is the
  // subject" stops being true -- the visual reads as a permanent double-exposure. The original
  // (ae7d94c) drew the still only on frames 48-63 (ramping up) and 0-15 (ramping down) of its
  // 64f counter and drew NOTHING on frames 16-47: a genuine 32-frame hole where the animation
  // holds the stage by itself. That absence is the visual's breathing.
  //
  // `env in 8f hold 16f out 8f` on the still lane restores it, with the lane's `offset 48f`
  // setting the phase so the visit lands where the original's did. Per 64f base cycle
  // (lane frame = base - 48, mod 64):
  //   base 49-55  fade in      (alpha 0.125 .. 0.875)
  //   base 56-63  HOLD at full -- and on across the wrap into
  //   base  0- 8  HOLD at full  (17 full-alpha frames spanning the cycle boundary)
  //   base  9-15  fade out     (alpha 0.875 .. 0.125)
  //   base 16-48  ABSENT       (33f of animation alone -- the original's 32f hole)
  // Those are measured off the compiled tree, not estimated; the animation case in
  // tests/v3_grammar_test.cpp asserts the counts (33 absent / 17 full / 14 mid-ramp) and
  // fails against `fade inout`, which scores 1 and 1.
  //
  // The legs are 8/16/8 rather than a literal 16/16/16 because the four legs of the intent
  // (16 in + 16 hold + 16 out + 32 absent) sum to 80f and cannot fit one 64f turn. Trading the
  // in/out ramps down to 8f is what buys BOTH a real hold and a real hole on a 64f clock --
  // and the hold plus the absence, not the ramp duration, are what the shape is for.
  const char* kAnimation = R"(
pattern animation for 1024f {
  every 64f { image runtime zoom (curve 0 -> 0.625) anim }
  every 64f offset 48f { image reward -> still env in 8f hold 16f out 8f zoom (curve 0.5 -> 0.625) }
  every 32f { caption runtime }
  spiral speed 3
})";

  // 8 SUPER_FAST -- rapid 8f current/next cuts, occasionally interrupted by a short random
  // ANIMATED burst with a cooldown (13.1). The burst picks its animation ONCE on
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
