#include <trance/platform/display_info.h>

#if defined(_WIN32)
#pragma warning(push, 0)
#include <windows.h>
#pragma warning(pop)
#endif

uint32_t display_refresh_hz()
{
#if defined(_WIN32)
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  // nullptr device name = the primary display; ENUM_CURRENT_SETTINGS = the mode it is
  // actually running in right now (as opposed to the registry-stored one, which can
  // differ after a runtime mode change).
  if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
    return 0;
  }
  // Documented sentinels: dmDisplayFrequency of 0 or 1 both mean "the hardware's
  // default rate", i.e. the mode table is declining to name a number. Reporting either
  // verbatim would put "display refresh: 1 Hz" in front of the user, so both fold into
  // the unknown case.
  if (mode.dmDisplayFrequency <= 1) {
    return 0;
  }
  return static_cast<uint32_t>(mode.dmDisplayFrequency);
#else
  // X11 exposes this through XRandR (XRRConfigCurrentRate), which needs libXrandr --
  // a link dependency this build does not take (see CMakeLists: X11 + Xext only, for
  // overlay mode). Unknown is honest here; the callers all degrade gracefully.
  return 0;
#endif
}
