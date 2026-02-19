cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(trance CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DEPS ${CMAKE_SOURCE_DIR}/dependencies)

# WORKAROUND: CMAKE_MSVC_RUNTIME_LIBRARY is broken with the VS 18 2026 generator
# in CMake 4.1.2 — it rejects "MultiThreadedDLLDebug" as an unknown value.
# Set /MDd and /MD directly instead. Once CMake adds proper VS 18 support, replace
# these two lines with:
#   set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
add_compile_options(
  $<$<CONFIG:Debug>:/MDd>
  $<$<NOT:$<CONFIG:Debug>>:/MD>
)

# ---------------------------------------------------------------------------
# Shared compiler options / definitions (all targets in this project share them)
# ---------------------------------------------------------------------------
add_compile_options(/W3 /WX /MP /wd4800 /wd4005)
add_compile_definitions(
  _MBCS
  SFML_STATIC
  GLEW_STATIC
  _SCL_SECURE_NO_WARNINGS
  _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
  $<$<CONFIG:Debug>:DEBUG>
)

# ---------------------------------------------------------------------------
# Protobuf codegen
# protoc preserves the proto_path-relative path, so with --proto_path=src
# and input src/common/trance.proto it outputs to ${CMAKE_BINARY_DIR}/common/
# ---------------------------------------------------------------------------
add_custom_command(
  OUTPUT
    ${CMAKE_BINARY_DIR}/common/trance.pb.h
    ${CMAKE_BINARY_DIR}/common/trance.pb.cc
  COMMAND ${DEPS}/x64/protoc.exe
    --cpp_out=${CMAKE_BINARY_DIR}
    --proto_path=${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common/trance.proto
  DEPENDS
    ${CMAKE_SOURCE_DIR}/src/common/trance.proto
    ${DEPS}/x64/protoc.exe
  VERBATIM
)

# WORKAROUND: session.cpp does `#include <common/trance.pb.cc>` directly instead
# of including the header and linking against the compiled TU, so trance.pb.cc
# must NOT be listed as a compiled source (it would be compiled twice — once
# by the compiler and once textually by the #include). We use a custom target
# + add_dependencies to ensure the files are generated before session.cpp compiles.
# If session.cpp is ever refactored to #include <common/trance.pb.h>, replace this
# block and the add_dependencies() below with a single source entry in common_lib:
#   ${CMAKE_BINARY_DIR}/common/trance.pb.cc
add_custom_target(proto_gen
  DEPENDS
    ${CMAKE_BINARY_DIR}/common/trance.pb.h
    ${CMAKE_BINARY_DIR}/common/trance.pb.cc
)

# ---------------------------------------------------------------------------
# Helper: full path to a vendored lib (same filename in Debug and Release)
# ---------------------------------------------------------------------------
set(D ${DEPS}/x64/Debug)
set(R ${DEPS}/x64/Release)
# Usage: "$<IF:$<CONFIG:Debug>,${D}/foo.lib,${R}/foo.lib>"

# ---------------------------------------------------------------------------
# Common static library
# ---------------------------------------------------------------------------
add_library(common_lib STATIC
  src/common/media/image.cpp
  src/common/media/streamer.cpp
  src/common/session.cpp   # #includes common/trance.pb.cc directly (see WORKAROUND above)
  src/jpgd/jpgd.cpp
)

add_dependencies(common_lib proto_gen)

target_include_directories(common_lib PUBLIC
  ${DEPS}/include
  src/
  ${CMAKE_BINARY_DIR}
)

# ---------------------------------------------------------------------------
# trance executable
# ---------------------------------------------------------------------------
add_executable(trance
  src/trance/director.cpp
  src/trance/main.cpp
  src/trance/media/async_streamer.cpp
  src/trance/media/audio.cpp
  src/trance/media/export.cpp
  src/trance/media/font.cpp
  src/trance/render/oculus.cpp
  src/trance/render/openvr.cpp
  src/trance/render/render.cpp
  src/trance/render/video_export.cpp
  src/trance/theme_bank.cpp
  src/trance/visual/api.cpp
  src/trance/visual/cyclers.cpp
  src/trance/visual/visual.cpp
)

# Libs with the same filename in both Debug and Release
foreach(LIB IN ITEMS
    LibOVR flac freetype gflags glew jpeg
    libprotobuf libwebm libx264 ogg openal32
    openvr_api vorbis vorbisfile)
  target_link_libraries(trance PRIVATE
    "$<IF:$<CONFIG:Debug>,${D}/${LIB}.lib,${R}/${LIB}.lib>")
endforeach()

# Libs with different filenames between Debug and Release
target_link_libraries(trance PRIVATE
  common_lib   # also propagates include dirs
  # Windows SDK — no path needed
  ws2_32 winmm shlwapi opengl32
  # SFML static libs (debug suffix differs)
  "$<IF:$<CONFIG:Debug>,${D}/sfml-system-s-d.lib,${R}/sfml-system-s.lib>"
  "$<IF:$<CONFIG:Debug>,${D}/sfml-window-s-d.lib,${R}/sfml-window-s.lib>"
  "$<IF:$<CONFIG:Debug>,${D}/sfml-graphics-s-d.lib,${R}/sfml-graphics-s.lib>"
  "$<IF:$<CONFIG:Debug>,${D}/sfml-audio-s-d.lib,${R}/sfml-audio-s.lib>"
  # VPX (debug/release have different names)
  "$<IF:$<CONFIG:Debug>,${D}/vpxmdd.lib,${R}/vpxmd.lib>"
  # libgif ships as a .a (GNU archive), must use full path so MSVC doesn't mangle to .lib
  "$<IF:$<CONFIG:Debug>,${D}/libgif.a,${R}/libgif.a>"
)

target_link_options(trance PRIVATE /NODEFAULTLIB:libcmt)

# ---------------------------------------------------------------------------
# DLL copy for trance (post-build)
# x264.dll differs by config folder; the rest only ship from Release/
# ---------------------------------------------------------------------------
add_custom_command(TARGET trance POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<IF:$<CONFIG:Debug>,${D}/x264.dll,${R}/x264.dll>"
    "$<TARGET_FILE_DIR:trance>"
  VERBATIM
)
foreach(DLL IN ITEMS openal32.dll openvr_api.dll libwinpthread-1.dll)
  add_custom_command(TARGET trance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${R}/${DLL}" "$<TARGET_FILE_DIR:trance>"
    VERBATIM
  )
endforeach()

# ---------------------------------------------------------------------------
# creator executable
# ---------------------------------------------------------------------------
add_executable(creator WIN32
  src/creator/export.cpp
  src/creator/launch.cpp
  src/creator/main.cpp
  src/creator/playlist.cpp
  src/creator/program.cpp
  src/creator/settings.cpp
  src/creator/theme.cpp
  src/creator/variables.cpp
)

target_compile_definitions(creator PRIVATE
  _CRT_SECURE_NO_WARNINGS
  _UNICODE
)

# wxWidgets header setup: the wx 3.2.x MSVC redirect (include/msvc/wx/setup.h)
# must be found BEFORE the generic include/ so that #include "wx/setup.h"
# resolves to the MSVC-specific version, which then pulls in the real
# build-config setup.h from ${DEPS}/lib/vc_x64_lib/msw{u,ud}/wx/setup.h.
target_include_directories(creator PRIVATE ${DEPS}/include/msvc)

# wxWidgets auto-links its own libs via #pragma comment(lib, ...) in its headers,
# so we only need to put the lib directory on the search path — we don't list wx
# libs explicitly here.
target_link_directories(creator PRIVATE
  "$<IF:$<CONFIG:Debug>,${D},${R}>")

# Libs with the same filename in both Debug and Release
foreach(LIB IN ITEMS
    freetype jpeg libprotobuf libwebm libx264)
  target_link_libraries(creator PRIVATE
    "$<IF:$<CONFIG:Debug>,${D}/${LIB}.lib,${R}/${LIB}.lib>")
endforeach()

target_link_libraries(creator PRIVATE
  common_lib   # also propagates include dirs
  # Windows SDK
  ws2_32 winmm shlwapi opengl32
  # SFML
  "$<IF:$<CONFIG:Debug>,${D}/sfml-system-s-d.lib,${R}/sfml-system-s.lib>"
  "$<IF:$<CONFIG:Debug>,${D}/sfml-graphics-s-d.lib,${R}/sfml-graphics-s.lib>"
  "$<IF:$<CONFIG:Debug>,${D}/sfml-window-s-d.lib,${R}/sfml-window-s.lib>"
  # VPX
  "$<IF:$<CONFIG:Debug>,${D}/vpxmdd.lib,${R}/vpxmd.lib>"
  # libgif
  "$<IF:$<CONFIG:Debug>,${D}/libgif.a,${R}/libgif.a>"
)

target_link_options(creator PRIVATE
  /NODEFAULTLIB:libcmt
  $<$<NOT:$<CONFIG:Debug>>:/SAFESEH:NO>
)
