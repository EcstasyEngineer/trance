#include <trance/visual/builtin_patterns.h>

namespace
{
  // SLOW_FLASH (enum 2). Node ids match what render_slow_flash reads; proven
  // render-equivalent to the hardcoded SlowFlashVisual by pattern_render_test.
  const char* kSlowFlash = R"(
pattern slow_flash {
  render slow_flash
  one {
    every 1 : themes
    repeat 2 seq {
      phase "SLOW" par {
        id "slow_main" one {
          every 1 : spiral_new, font
          id "slow_repeat" repeat 16 id "slow_loop" image primary every 64 : image primary -> current, anim primary, text line primary, small_text primary
        }
        every 1 : spiral 2
        every 64 @32 : upload
      }
      id "fast_cycler" phase "FAST" par {
        id "fast_main" one {
          every 1 : spiral_new, font
          id "fast_repeat" repeat 32 id "fast_loop" par {
            id "fast_image" image alternate every 8 : image alternate -> current
            id "fast_text" every 16 @8 : text word alternate
          }
        }
        every 16 : small_text alternate
        every 1 : spiral 4
      }
    }
  }
}
)";

  // ACCELERATE (enum 1). A ramp of image segments whose length shortens 56->12; per
  // segment the image_count, spiral speed, slot and "fastest" flag are derived from the
  // length, so the DSL emits them with `generate` + [expr], split into blocks where the
  // slot / fastest / upload-vs-timer constants hold. The anim modulus is captured random
  // ({2,4,8}); render's anim type depends on it, but every render call's float args are
  // schedule-driven, so pattern_render_test validates it ignoring only the anim type.
  // alt blocks: L/12 even => alternate. upload only when L>24. fastest when L<16.
  const char* kAccelerate = R"(
pattern accelerate {
  render accelerate
  one {
    every 1 : set animation_counter 0, roll animation_mod : 2 4 8, font, spiral_new, themes
    id "ramp" phase "RAMP" seq {
      generate L from 56 to 48 {
        repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
          par {
            image alternate every [L] : image alternate -> current, pulse animation_counter every animation_mod -> animation_on, anim alternate when animation_on, set animation_alt 1 when animation_on
            every 1 : spiral [1 + (56-L)/16]
            every [L] @[L/2] : upload
          }
          every 8 : toggle text_on, text line alternate when text_on
        }
      }
      generate L from 47 to 36 {
        repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
          par {
            image primary every [L] : image primary -> current, pulse animation_counter every animation_mod -> animation_on, anim primary when animation_on, set animation_alt 0 when animation_on
            every 1 : spiral [1 + (56-L)/16]
            every [L] @[L/2] : upload
          }
          every 8 : toggle text_on, text line primary when text_on
        }
      }
      generate L from 35 to 25 {
        repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
          par {
            image alternate every [L] : image alternate -> current, pulse animation_counter every animation_mod -> animation_on, anim alternate when animation_on, set animation_alt 1 when animation_on
            every 1 : spiral [1 + (56-L)/16]
            every [L] @[L/2] : upload
          }
          every 8 : toggle text_on, text line alternate when text_on
        }
      }
      generate L from 24 to 24 {
        repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
          par {
            image alternate every [L] : image alternate -> current, pulse animation_counter every animation_mod -> animation_on, anim alternate when animation_on, set animation_alt 1 when animation_on
            every 1 : spiral [1 + (56-L)/16]
            timer [L]
          }
          every 8 : toggle text_on, text line alternate when text_on
        }
      }
      generate L from 23 to 16 {
        repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
          par {
            image primary every [L] : image primary -> current, pulse animation_counter every animation_mod -> animation_on, anim primary when animation_on, set animation_alt 0 when animation_on
            every 1 : spiral [1 + (56-L)/16]
            timer [L]
          }
          every 8 : toggle text_on, text line primary when text_on
        }
      }
      generate L from 15 to 12 {
        repeat [1 + (56-L)*(56-L)*(56-L)*(56-L)*(56-L)*(56-L)/(56*56*56*56*56)] one {
          par {
            image primary every [L] : image primary -> current, pulse animation_counter every animation_mod -> animation_on, anim primary when animation_on, set animation_alt 0 when animation_on
            every 1 : spiral [1 + (56-L)/16]
            timer [L]
          }
          every [L] : set text_on 1, text word primary
        }
      }
    }
  }
}
)";

  // SUB_TEXT (enum 3). Toggling alternate slot, a captured-random animation modulus
  // ({3,5,7}) driving an every-Nth animation pulse, and a sub_speed ramp gating which
  // subtext cadence fires. Render reads `animation_on` (which depends on the random
  // modulus), so this is validated by review + builtin_patterns_test rather than the
  // frame-for-frame render harness (the RNG diverges between two instances).
  const char* kSubText = R"(
pattern sub_text {
  render sub_text
  par {
    every 1 : spiral 4
    one {
      every 1 : set animation_counter 0, set alt 1, roll animation_mod : 3 5 7, themes, font, spiral_new, inc sub_speed
      repeat 16 par {
        id "image" image runtime every 48 : toggle alt, image reg alt -> current, pulse animation_counter every animation_mod -> animation_on, anim reg alt when animation_on, anim reg alt
        every 48 @24 : upload
        seq {
          every 4 : text word reg alt
          repeat 23 every 4 : text once primary
        }
        every 12 : subtext reg alt when sub_speed == 1
        every 24 : subtext reg alt when sub_speed == 2
        every 48 : subtext reg alt when sub_speed >= 3
      }
    }
  }
}
)";

  // FLASH_TEXT (enum 4). A captured-random `animated` flag (re-rolled each pattern loop)
  // gates the animation; `alt` toggles per oneshot iteration; the previous end image is
  // handed to start via `copy`. Render depends on the random `animated`, so it is
  // review-validated, not frame-harness-validated.
  const char* kFlashText = R"(
pattern flash_text {
  render flash_text
  one {
    every 1 : roll animated : 1 0, set alt 1, image alternate -> end
    par {
      every 1 : spiral 2.5
      every 64 : font force
      id "subtext_counter" repeat 2 timer 32
      every 32 : small_text primary force
      every 64 @32 : upload
      repeat 8 one {
        every 1 : toggle alt, text line reg alt
        id "image_repeat" repeat 2 id "image" image runtime every 64 : copy end -> start, anim reg alt when animated, image reg alt -> end
      }
    }
    every 1 : themes
  }
}
)";

  // SIMPLE (enum 5, the enum name is PARALLEL). The former SimpleVisual cycled an
  // `_anim_cycle` counter and showed the animation every third image. The `++ twice`
  // was an implementation detail; the observable behaviour is "every third image fire:
  // pull the alternate slot, fire the (primary) animation, render ANIM" -- a pulse to a
  // one-frame flag. Render-equivalent via pattern_render_test.
  const char* kSimple = R"(
pattern simple {
  render {
    image current anim_if anim_on : zoom [0.5 * image.progress]
    spiral
    small_text : alpha [1 / 5], origin 0.25
    text when [counter.index == 1 or counter.index == 2] : origin 0.75, zoom 0.75, shadow_origin [0.5 * image.progress], shadow_zoom [0.5 * image.progress]
  }
  one {
    every 1 : spiral_new, font, themes
    par {
      every 1 : spiral 3
      every 32 : small_text primary force
      every 32 @16 : upload
      repeat 16 par {
        id "counter" repeat 4 timer 32
        id "image" image runtime every 64 : pulse simple_counter every 3 -> anim_on, image reg anim_on -> current, anim primary when anim_on
        every 128 : text line runtime
      }
    }
  }
}
)";

  // SUPER_PARALLEL (enum 6). Three offset image lanes + an alternate-animation toggle;
  // render-equivalent to the former ParallelVisual via pattern_render_test. The toggle
  // `alt_anim` starts at 1 (set in the init leaf, then flipped once per repeat, exactly
  // mirroring _alternate_animation{true} + the per-iteration toggle).
  const char* kSuperParallel = R"(
pattern super_parallel {
  render super_parallel
  one {
    every 1 : spiral_new, font, themes, set alt_anim 1
    par {
      every 1 : spiral 3.5
      every 32 @16 : upload
      id "text" every 32 : text word runtime
      repeat 12 one {
        every 1 : toggle alt_anim
        id "interleave" phase "INTERLEAVE" par {
          offset 0 id "prog0" image primary as "img[0]" seq {
            every 16 : image primary -> img0, anim reg alt_anim
            id "single0" timer 16
            timer 64
          }
          offset 32 id "prog1" image primary as "img[1]" seq {
            every 16 : image primary -> img1
            id "single1" timer 16
            timer 64
          }
          offset 64 id "prog2" image alternate as "img[2]" seq {
            every 16 : image alternate -> img2
            id "single2" timer 16
            timer 64
          }
        }
      }
    }
  }
}
)";

  // SUPER_FAST (enum 8). The one genuine state machine: a 4-state FSM ticking every 8
  // frames, isolated in the native `super_fast_tick` effect (compiled_visual.cpp) which
  // writes the sf_* scalar registers and the current/next image registers. The schedule
  // is data and the render is the super_fast preset; only the FSM itself is C++. Its
  // render structure depends on the (random) state, so it is review-validated.
  const char* kSuperFast = R"(
pattern super_fast {
  render super_fast
  one {
    every 1 : spiral_new, font, themes
    par {
      timer 2048
      id "rapid" image runtime every 8 : super_fast_tick
      every 1 : spiral 3
    }
  }
}
)";

  // ANIMATION (enum 7). Render-equivalent to AnimationVisual via pattern_render_test.
  const char* kAnimation = R"(
pattern animation {
  render animation
  one {
    every 1 : spiral_new, font, themes
    repeat 8 par {
      every 1 : spiral 3.5
      every 32 : small_text random force
      every 32 @24 : upload
      image primary every 32 : image primary -> backup
      image alternate as "img_alt" every 64 @32 : image alternate -> current
      every 16
      seq {
        every 64 @0 : text line primary, anim primary
        id "change_alt" every 64 @0 : text line alternate, anim alternate
      }
      id "change_counter" repeat 2 every 32
    }
    id "start_end_timer" seq { every 32  every 960  every 32 }
  }
}
)";
}

namespace builtin
{
  std::string pattern_source(uint32_t visual_type)
  {
    switch (visual_type) {
    case 1:  // ACCELERATE
      return kAccelerate;
    case 2:  // SLOW_FLASH
      return kSlowFlash;
    case 3:  // SUB_TEXT
      return kSubText;
    case 4:  // FLASH_TEXT
      return kFlashText;
    case 5:  // SIMPLE (enum name PARALLEL)
      return kSimple;
    case 6:  // SUPER_PARALLEL
      return kSuperParallel;
    case 7:  // ANIMATION
      return kAnimation;
    case 8:  // SUPER_FAST
      return kSuperFast;
    default:
      return {};
    }
  }
}
