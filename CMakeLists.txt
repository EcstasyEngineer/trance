cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0091 NEW)  # MSVC runtime selected via CMAKE_MSVC_RUNTIME_LIBRARY
project(trance CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Relocatable binaries: embed the library search path relative to the executable
# ($ORIGIN) rather than the absolute build tree, so the output doesn't depend on
# where it was built and can be run/moved from anywhere.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

# Multi-config MSVC (VS generator) otherwise falls back to a dependency's Debug
# import when building Release, producing LNK2038 _ITERATOR_DEBUG_LEVEL /
# RuntimeLibrary mismatches (notably from openal-soft via sfml-audio). Force
# non-Debug configs to map to the Release-imported libraries.
if(MSVC)
  set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release "")
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release "")
  set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release "")
endif()

# ---------------------------------------------------------------------------
# Dependencies (resolved by vcpkg manifest mode via the toolchain file; see
# CMakePresets.json). Nothing is vendored — `vcpkg install` restores everything.
# ---------------------------------------------------------------------------
find_package(Protobuf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SFML 3 COMPONENTS System Window Graphics Audio CONFIG REQUIRED)
if(WIN32)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_trance_vcpkg_lib_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    set(_trance_vcpkg_debug_lib_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")

    # SFML's audio config uses module-style imported targets for Vorbis/Ogg/FLAC.
    # The module search can cache debug .lib paths under the Visual Studio
    # multi-config generator, so give the actual targets per-config locations.
    function(trance_pin_imported_audio_dep target debug_lib release_lib)
      if(TARGET ${target})
        set_target_properties(${target} PROPERTIES
          IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
          IMPORTED_LOCATION_DEBUG "${_trance_vcpkg_debug_lib_dir}/${debug_lib}"
          IMPORTED_LOCATION_RELEASE "${_trance_vcpkg_lib_dir}/${release_lib}"
          IMPORTED_LOCATION_RELWITHDEBINFO "${_trance_vcpkg_lib_dir}/${release_lib}"
          IMPORTED_LOCATION_MINSIZEREL "${_trance_vcpkg_lib_dir}/${release_lib}")
      endif()
    endfunction()
    trance_pin_imported_audio_dep(Ogg::ogg ogg.lib ogg.lib)
    trance_pin_imported_audio_dep(Vorbis::vorbis vorbis.lib vorbis.lib)
    trance_pin_imported_audio_dep(Vorbis::vorbisfile vorbisfile.lib vorbisfile.lib)
    trance_pin_imported_audio_dep(Vorbis::vorbisenc vorbisenc.lib vorbisenc.lib)
    trance_pin_imported_audio_dep(FLAC::FLAC FLAC.lib FLAC.lib)
  endif()
endif()
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(gflags CONFIG REQUIRED)
find_package(ImGui-SFML CONFIG REQUIRED)
find_package(unofficial-libvpx CONFIG REQUIRED)
find_package(unofficial-libwebm CONFIG REQUIRED)
find_package(GIF REQUIRED)
find_package(miniz CONFIG REQUIRED)

# vcpkg's libwebm headers cross-include each other as "mkvparser/..." and
# "mkvmuxer/..." (paths relative to include/webm), but the imported target only
# advertises include/. Add include/webm so those internal includes resolve; our
# own sources still include the fully-qualified <webm/mkvparser/...> form.
if(TARGET unofficial::libwebm::libwebm)
  get_target_property(_webm_inc unofficial::libwebm::libwebm INTERFACE_INCLUDE_DIRECTORIES)
  set_property(TARGET unofficial::libwebm::libwebm APPEND PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES "${_webm_inc}/webm")
endif()
find_package(OpenVR REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Shared build options (per-target, not global) — INTERFACE library that every
# target links against.
# ---------------------------------------------------------------------------
add_library(trance_options INTERFACE)
target_compile_features(trance_options INTERFACE cxx_std_17)
target_compile_definitions(trance_options INTERFACE
  $<$<CONFIG:Debug>:DEBUG>)

if(MSVC)
  # WORKAROUND: CMAKE_MSVC_RUNTIME_LIBRARY was broken with the VS 18 2026
  # generator in CMake 4.1.2 (it rejected "MultiThreadedDLLDebug"). Set the
  # /MD[d] flags directly. The x64-windows-static-md triplet matches this:
  # static deps, dynamic CRT.
  target_compile_options(trance_options INTERFACE
    $<$<CONFIG:Debug>:/MDd> $<$<NOT:$<CONFIG:Debug>>:/MD>
    /W3 /WX /MP /wd4800 /wd4005)
  target_compile_definitions(trance_options INTERFACE
    _SCL_SECURE_NO_WARNINGS
    _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
else()
  # 2014-era code hasn't been cleaned for gcc/clang's stricter -Wall yet, so we
  # do NOT enable -Werror here. -Wno-unknown-pragmas swallows the MSVC
  # `#pragma warning(push, 0)` lines that pepper the source.
  target_compile_options(trance_options INTERFACE
    -Wall -Wextra -Wno-unknown-pragmas
    # Reproducible builds: make __FILE__ and debug paths relative (./src/...) so
    # the output doesn't depend on the build directory.
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
endif()

# SFML/GLEW are linked statically on Windows (x64-windows-static-md) and
# dynamically on Linux; the *_STATIC defines must match the Windows case only.
if(WIN32)
  target_compile_definitions(trance_options INTERFACE SFML_STATIC GLEW_STATIC)
endif()

# ---------------------------------------------------------------------------
# Protobuf codegen. protoc preserves the proto_path-relative path, so with
# --proto_path=src and input src/common/trance.proto it lands in build/common/.
# Uses the protoc from the vcpkg protobuf port.
# ---------------------------------------------------------------------------
add_custom_command(
  OUTPUT
    ${CMAKE_BINARY_DIR}/common/trance.pb.h
    ${CMAKE_BINARY_DIR}/common/trance.pb.cc
  COMMAND protobuf::protoc
    --cpp_out=${CMAKE_BINARY_DIR}
    --proto_path=${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common/trance.proto
  DEPENDS
    ${CMAKE_SOURCE_DIR}/src/common/trance.proto
    protobuf::protoc
  VERBATIM)

# The frozen upstream-era schema the legacy .session/.cfg importer parses against
# (src/common/legacy.proto, package trance_legacy_pb). Deliberately a SECOND descriptor:
# parsing legacy textproto against the live trance.proto would make every field this
# fork adds part of the import surface. See legacy.proto's header and issue #47.
add_custom_command(
  OUTPUT
    ${CMAKE_BINARY_DIR}/common/legacy.pb.h
    ${CMAKE_BINARY_DIR}/common/legacy.pb.cc
  COMMAND protobuf::protoc
    --cpp_out=${CMAKE_BINARY_DIR}
    --proto_path=${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common/legacy.proto
  DEPENDS
    ${CMAKE_SOURCE_DIR}/src/common/legacy.proto
    protobuf::protoc
  VERBATIM)

# The generated .cc is now a normal compiled TU (session.cpp includes the .pb.h).
# Don't subject protobuf's generated code to our warning flags.
set_source_files_properties(${CMAKE_BINARY_DIR}/common/trance.pb.cc PROPERTIES
  GENERATED TRUE
  COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")
set_source_files_properties(${CMAKE_BINARY_DIR}/common/trance.pb.h PROPERTIES
  GENERATED TRUE)
set_source_files_properties(${CMAKE_BINARY_DIR}/common/legacy.pb.cc PROPERTIES
  GENERATED TRUE
  COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")
set_source_files_properties(${CMAKE_BINARY_DIR}/common/legacy.pb.h PROPERTIES
  GENERATED TRUE)

# ---------------------------------------------------------------------------
# Common static library
# ---------------------------------------------------------------------------
add_library(common_lib STATIC
  src/common/media/image.cpp
  src/common/media/streamer.cpp
  src/common/session.cpp
  src/common/session_archive.cpp
  src/common/session_json.cpp
  src/common/session_legacy.cpp
  ${CMAKE_BINARY_DIR}/common/trance.pb.cc
  ${CMAKE_BINARY_DIR}/common/legacy.pb.cc)

target_include_directories(common_lib PUBLIC
  src/
  ${CMAKE_BINARY_DIR})

# streamer.{h,cpp} (a public header of this lib) pulls in the vpx/webm/gif
# headers, so these must propagate to every consumer — hence PUBLIC.
target_link_libraries(common_lib PUBLIC
  trance_options
  protobuf::libprotobuf
  nlohmann_json::nlohmann_json
  unofficial::libvpx::libvpx
  unofficial::libwebm::libwebm
  GIF::GIF
  miniz::miniz)

# ---------------------------------------------------------------------------
# trance executable (realtime renderer)
# ---------------------------------------------------------------------------
add_executable(trance
  src/trance/director.cpp
  src/trance/main.cpp
  src/trance/media/async_streamer.cpp
  src/trance/media/audio.cpp
  src/trance/media/entrainment.cpp
  src/trance/media/font.cpp
  src/trance/net/command_channel.cpp
  src/trance/net/command_protocol.cpp
  src/trance/platform/display_info.cpp
  src/trance/platform/overlay_hints.cpp
  src/trance/playlist_runner.cpp
  src/trance/platform/system_control.cpp
  src/trance/render/openvr.cpp
  src/trance/render/openxr.cpp
  src/trance/render/render.cpp
  src/trance/theme_bank.cpp
  src/trance/ui/app_ui.cpp
  src/trance/visual/api.cpp
  src/trance/visual/builtin_patterns_v3.cpp
  src/trance/visual/compiled_visual.cpp
  src/trance/visual/cyclers.cpp
  src/trance/visual/pattern_compiler.cpp
  src/trance/visual/pattern_parser_v3.cpp
  src/trance/visual/render_eval.cpp
  src/trance/visual/visual.cpp)

target_link_libraries(trance PRIVATE
  common_lib
  trance_options
  SFML::System SFML::Window SFML::Graphics SFML::Audio
  GLEW::GLEW
  OpenGL::GL
  gflags::gflags
  ImGui-SFML::ImGui-SFML
  unofficial::libvpx::libvpx
  unofficial::libwebm::libwebm
  GIF::GIF
  OpenVR::OpenVR
  OpenXR::openxr_loader)

if(WIN32)
  # avrt: used by WASAPI-backed audio paths on Windows.
  target_link_libraries(trance PRIVATE ws2_32 winmm shlwapi avrt)
endif()

if(UNIX AND NOT APPLE)
  # X11 + Xext (Shape extension): v0 click-through overlay mode --
  # _NET_WM_WINDOW_OPACITY + XShapeCombineRectangles in src/trance/platform/overlay_hints.cpp.
  # System dev packages (libx11-dev/libxext-dev), not vcpkg -- SFML already links X11
  # transitively for window creation, but the Shape extension calls need it explicit.
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(X11_XEXT QUIET IMPORTED_TARGET x11 xext)
  endif()
  if(TARGET PkgConfig::X11_XEXT)
    target_link_libraries(trance PRIVATE PkgConfig::X11_XEXT)
  else()
    target_link_libraries(trance PRIVATE X11 Xext)
  endif()
endif()

# Keep the runtime output dir to the shippables: static libs and the linker's
# import-lib byproducts (.lib/.exp) go to lib/ instead of sitting next to trance.exe.
set_target_properties(common_lib trance PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Install / bundle. `cmake --install build/<preset> --config Release --prefix <dir>`
# produces the complete distributable: trance.exe and, on Windows, the DLLs
# vcpkg's applocal step deployed beside it (openvr_api.dll — Valve ships OpenVR
# only as a DLL, so it cannot be linked static; everything else is already
# static via the x64-windows-static-md triplet).
# ---------------------------------------------------------------------------
install(TARGETS trance RUNTIME DESTINATION .)
if(WIN32)
  install(DIRECTORY $<TARGET_FILE_DIR:trance>/ DESTINATION .
    FILES_MATCHING PATTERN "*.dll")
endif()

# ---------------------------------------------------------------------------
# Headless tests (tests/CMakeLists.txt). Default OFF; enable with
# -DTRANCE_BUILD_TESTS=ON and run via `ctest`. The subdirectory keeps every test
# artifact (exes, MSBuild intermediates, project files) under build/<preset>/tests/
# so the Release/ output dir holds only the shippable binaries.
# ---------------------------------------------------------------------------
option(TRANCE_BUILD_TESTS "Build headless unit tests" OFF)
if(TRANCE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
