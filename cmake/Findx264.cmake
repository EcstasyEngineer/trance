# Fallback finder for x264 when pkg-config metadata isn't available (e.g. MSVC).
# Produces the imported target x264::x264.
find_path(X264_INCLUDE_DIR NAMES x264.h)

if(WIN32)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_x264_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    find_library(X264_LIBRARY_RELEASE NAMES x264 libx264
      PATHS "${_x264_vcpkg_root}/lib"
      NO_DEFAULT_PATH)
    find_library(X264_LIBRARY_DEBUG NAMES x264 libx264
      PATHS "${_x264_vcpkg_root}/debug/lib"
      NO_DEFAULT_PATH)
  endif()
  find_library(X264_LIBRARY_RELEASE NAMES x264 libx264)
  find_library(X264_LIBRARY_DEBUG NAMES x264 libx264)
  set(X264_LIBRARY "${X264_LIBRARY_RELEASE}")
else()
  find_library(X264_LIBRARY NAMES x264 libx264)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(x264
  REQUIRED_VARS X264_LIBRARY X264_INCLUDE_DIR)

if(x264_FOUND AND NOT TARGET x264::x264)
  add_library(x264::x264 UNKNOWN IMPORTED)
  set_target_properties(x264::x264 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${X264_INCLUDE_DIR}")
  if(WIN32)
    set_target_properties(x264::x264 PROPERTIES
      IMPORTED_LOCATION "${X264_LIBRARY_RELEASE}"
      IMPORTED_LOCATION_RELEASE "${X264_LIBRARY_RELEASE}"
      IMPORTED_LOCATION_RELWITHDEBINFO "${X264_LIBRARY_RELEASE}"
      IMPORTED_LOCATION_MINSIZEREL "${X264_LIBRARY_RELEASE}")
    if(X264_LIBRARY_DEBUG)
      set_property(TARGET x264::x264 PROPERTY
        IMPORTED_LOCATION_DEBUG "${X264_LIBRARY_DEBUG}")
    endif()
  else()
    set_target_properties(x264::x264 PROPERTIES
      IMPORTED_LOCATION "${X264_LIBRARY}")
  endif()
endif()

mark_as_advanced(
  X264_INCLUDE_DIR
  X264_LIBRARY
  X264_LIBRARY_RELEASE
  X264_LIBRARY_DEBUG)
