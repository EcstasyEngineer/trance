# Fallback finder for x264 when pkg-config metadata isn't available (e.g. MSVC).
# Produces the imported target x264::x264.
find_path(X264_INCLUDE_DIR NAMES x264.h)
find_library(X264_LIBRARY NAMES x264 libx264)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(x264
  REQUIRED_VARS X264_LIBRARY X264_INCLUDE_DIR)

if(x264_FOUND AND NOT TARGET x264::x264)
  add_library(x264::x264 UNKNOWN IMPORTED)
  set_target_properties(x264::x264 PROPERTIES
    IMPORTED_LOCATION "${X264_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${X264_INCLUDE_DIR}")
endif()

mark_as_advanced(X264_INCLUDE_DIR X264_LIBRARY)
