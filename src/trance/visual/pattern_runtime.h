#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_RUNTIME_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_RUNTIME_H
#include <trance/visual/pattern_ast.h>
#include <common/media/image.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace pattern
{
  // A compiled pattern's mutable runtime state, written by effects and read by the
  // render evaluator. `images` are the named image slots an Image/Copy effect writes
  // (e.g. "current"); `image_slots` stores the concrete source theme for the same
  // register so render-time debug can say which theme is actually on screen; `scalars`
  // are the named bool/int registers the state effects (set/inc/toggle/roll/pulse)
  // maintain.
  struct Registers
  {
    std::unordered_map<std::string, Image> images;
    std::unordered_map<std::string, Slot> image_slots;
    // Which theme GENERATION each register's image was pulled from (ThemeBank bumps a
    // lane's generation whenever the theme occupying it changes). A register whose
    // generation has fallen behind is still holding an image of a theme that is no
    // longer on that lane -- valid (Image is ref-counted, so an unloaded theme's frame
    // stays perfectly drawable) but stale, and since a register is only replaced when
    // its effect fires again, it would otherwise sit on screen indefinitely.
    //
    // ABSENCE MEANS "SNAPSHOT": a register filled by `copy` is deliberately a picture of
    // a past state -- that is the whole of how crossfade works -- so Copy erases the
    // entry and the refresh leaves it alone. Image sets it.
    std::unordered_map<std::string, uint32_t> image_gens;
    std::unordered_map<std::string, int32_t> scalars;
    // Which theme the last Anim effect loaded from. The bank keeps a primary AND an
    // alternate animation streamer alive at once, so "which one was loaded" and "which
    // one is drawn" are separate questions -- and a draw that answers the second one
    // differently from the first renders an animation nobody asked for. This is the
    // single-value analogue of image_slots (there is one live animation per side, not a
    // register file of them): the Anim effect records the slot it resolved at FIRE time,
    // and the render evaluator draws from it. Resolving once at fire time is what makes
    // `anim runtime` / `anim alternate` stable -- re-deciding per frame would re-roll the
    // random slot and flip the ping-pong mid-draw. Primary by default so a pattern that
    // draws `anim` without ever loading one behaves exactly as it did before.
    Slot anim_slot = Slot::Primary;
  };
}

#endif
