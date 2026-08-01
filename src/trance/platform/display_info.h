#ifndef TRANCE_SRC_TRANCE_PLATFORM_DISPLAY_INFO_H
#define TRANCE_SRC_TRANCE_PLATFORM_DISPLAY_INFO_H
// What the display can actually show, asked of the OS rather than of SFML.
//
// SFML 3's sf::VideoMode carries only {size, bitsPerPixel} -- there is no refresh-rate
// field anywhere in the SFML window API -- so the one number that says how many frames
// per second are worth presenting has to come from the platform directly.
#include <cstdint>

// Refresh rate of the PRIMARY display in Hz, or 0 when it can't be determined.
//
// Primary rather than "the monitor the window is on" to match sf::VideoMode::
// getDesktopMode(), which is what the fullscreen window is sized from -- one query
// answering for a different monitor than the window covers would be worse than none.
//
// 0 means genuinely unknown and callers must degrade rather than substitute a guess:
// on Windows the mode table reports 0 or 1 for "whatever the hardware default is", and
// off Windows there is no implementation at all (X11 would need libXrandr, which this
// build does not link -- only X11 + Xext, for overlay mode).
//
// Uncached: it is a display-mode table read, cheap enough for the per-frame F2 readout,
// and caching would freeze a stale number across a resolution/refresh change.
uint32_t display_refresh_hz();

#endif
