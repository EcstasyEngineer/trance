# Finder for OpenVR (the vcpkg `openvr` port ships no CMake config).
# Produces the imported target OpenVR::OpenVR. Sources include <openvr.h>, so we
# point the include dir at wherever that header lives (handles both the bare
# include/ and include/openvr/ layouts).
find_path(OPENVR_INCLUDE_DIR NAMES openvr.h PATH_SUFFIXES openvr)
find_library(OPENVR_LIBRARY NAMES openvr_api)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenVR
  REQUIRED_VARS OPENVR_LIBRARY OPENVR_INCLUDE_DIR)

if(OpenVR_FOUND AND NOT TARGET OpenVR::OpenVR)
  if(WIN32)
    # openvr is always a DLL (Valve ships only openvr_api.dll); OPENVR_LIBRARY is
    # the import lib, which links fine. vcpkg's applocal step deploys
    # openvr_api.dll next to the executables at build time.
    add_library(OpenVR::OpenVR UNKNOWN IMPORTED)
    set_target_properties(OpenVR::OpenVR PROPERTIES
      IMPORTED_LOCATION "${OPENVR_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OPENVR_INCLUDE_DIR}")
  else()
    # The vcpkg .so carries no DT_SONAME. IMPORTED_NO_SONAME tells CMake to link
    # by filename rather than full path, so the linker records the bare
    # "libopenvr_api.so" as DT_NEEDED, resolved at runtime via the RUNPATH vcpkg
    # points at its lib dir. (find_library picks the release lib because the
    # Linux presets set CMAKE_BUILD_TYPE=Release, so no debug/release split is
    # needed here.)
    add_library(OpenVR::OpenVR SHARED IMPORTED)
    set_target_properties(OpenVR::OpenVR PROPERTIES
      IMPORTED_LOCATION "${OPENVR_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OPENVR_INCLUDE_DIR}"
      IMPORTED_NO_SONAME TRUE)
  endif()
endif()

mark_as_advanced(OPENVR_INCLUDE_DIR OPENVR_LIBRARY)
