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
# On Windows, SFML's audio module pulls a bare `OpenAL` target whose config
# selection is ambiguous under the multi-config generator; pin it to the
# namespaced `OpenAL::OpenAL` (which carries proper per-config locations) so the
# Release build links the Release openal-soft. Windows-only — Linux is unaffected.
if(WIN32)
  find_package(OpenAL CONFIG REQUIRED)
  if(TARGET OpenAL AND TARGET OpenAL::OpenAL)
    set_property(TARGET OpenAL PROPERTY
      INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:OpenAL::OpenAL>")
  endif()
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_trance_vcpkg_lib_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    set(_trance_vcpkg_debug_lib_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")

    # SFML's audio config exposes bare VORBIS/FLAC targets whose library paths
    # can resolve to debug libs under the Visual Studio multi-config generator.
    # Pin them per config so Release does not pull MSVCRTD through audio deps.
    if(TARGET VORBIS)
      set_property(TARGET VORBIS PROPERTY INTERFACE_LINK_LIBRARIES
        "$<$<CONFIG:Debug>:${_trance_vcpkg_debug_lib_dir}/vorbisfile.lib>"
        "$<$<CONFIG:Debug>:${_trance_vcpkg_debug_lib_dir}/vorbisenc.lib>"
        "$<$<CONFIG:Debug>:${_trance_vcpkg_debug_lib_dir}/vorbis.lib>"
        "$<$<CONFIG:Debug>:${_trance_vcpkg_debug_lib_dir}/ogg.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${_trance_vcpkg_lib_dir}/vorbisfile.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${_trance_vcpkg_lib_dir}/vorbisenc.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${_trance_vcpkg_lib_dir}/vorbis.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${_trance_vcpkg_lib_dir}/ogg.lib>")
    endif()
    if(TARGET FLAC)
      set_property(TARGET FLAC PROPERTY INTERFACE_LINK_LIBRARIES
        "$<$<CONFIG:Debug>:${_trance_vcpkg_debug_lib_dir}/FLAC.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${_trance_vcpkg_lib_dir}/FLAC.lib>")
    endif()
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

# x264 ships pkg-config metadata, not a first-class CMake config. Prefer
# pkg-config (Linux); fall back to cmake/Findx264.cmake (MSVC) — never hardcode.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(X264 QUIET IMPORTED_TARGET x264)
endif()
if(TARGET PkgConfig::X264)
  set(X264_TARGET PkgConfig::X264)
else()
  find_package(x264 REQUIRED)
  set(X264_TARGET x264::x264)
endif()

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

# The generated .cc is now a normal compiled TU (session.cpp includes the .pb.h).
# Don't subject protobuf's generated code to our warning flags.
set_source_files_properties(${CMAKE_BINARY_DIR}/common/trance.pb.cc PROPERTIES
  GENERATED TRUE
  COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")
set_source_files_properties(${CMAKE_BINARY_DIR}/common/trance.pb.h PROPERTIES
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
  src/jpgd/jpgd.cpp
  ${CMAKE_BINARY_DIR}/common/trance.pb.cc)

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
# trance executable (realtime renderer + video export)
# ---------------------------------------------------------------------------
add_executable(trance
  src/trance/director.cpp
  src/trance/main.cpp
  src/trance/media/async_streamer.cpp
  src/trance/media/audio.cpp
  src/trance/media/entrainment.cpp
  src/trance/media/export.cpp
  src/trance/media/font.cpp
  src/trance/net/command_channel.cpp
  src/trance/net/command_protocol.cpp
  src/trance/render/openvr.cpp
  src/trance/render/render.cpp
  src/trance/render/video_export.cpp
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
  ${X264_TARGET})

if(WIN32)
  # avrt: required by openal-soft's WASAPI backend (AvSetMmThreadCharacteristics).
  target_link_libraries(trance PRIVATE ws2_32 winmm shlwapi avrt)
endif()

if(UNIX AND NOT APPLE)
  # X11 + Xext (Shape extension): v0 click-through overlay mode (#27) --
  # _NET_WM_WINDOW_OPACITY + XShapeCombineRectangles in src/trance/render/render.cpp.
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

# ---------------------------------------------------------------------------
# trance_convert (one-shot legacy .session/.cfg textproto -> JSON converter,
# docs/session-json-format.md sec 7). No SFML/window/GL/audio deps -- links
# common_lib (protobuf + nlohmann_json) only.
# ---------------------------------------------------------------------------
add_executable(trance_convert
  src/tools/convert_main.cpp)

target_link_libraries(trance_convert PRIVATE
  common_lib
  trance_options)

# ---------------------------------------------------------------------------
# creator executable (wxWidgets session editor)
#
# Defaults ON for Windows (the primary platform for the editor) and OFF
# elsewhere: wxWidgets pulls the whole GTK3 chain (~80 vcpkg ports, the long
# pole of a Linux build) and the editor has near-zero non-Windows use. Linux
# devs who want it can configure with -DTRANCE_BUILD_CREATOR=ON, but must also
# add `wxwidgets` to vcpkg.json (it is platform-gated to windows there).
# ---------------------------------------------------------------------------
if(WIN32)
  set(_creator_default ON)
else()
  set(_creator_default OFF)
endif()
option(TRANCE_BUILD_CREATOR "Build the wxWidgets session editor (creator)" ${_creator_default})

if(TRANCE_BUILD_CREATOR)
  find_package(wxWidgets CONFIG REQUIRED)

  add_executable(creator WIN32
    src/creator/export.cpp
    src/creator/launch.cpp
    src/creator/main.cpp
    src/creator/playlist.cpp
    src/creator/program.cpp
    src/creator/settings.cpp
    src/creator/theme.cpp
    src/creator/variables.cpp)

  target_compile_definitions(creator PRIVATE _UNICODE)
  if(MSVC)
    target_compile_definitions(creator PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  target_link_libraries(creator PRIVATE
    common_lib
    trance_options
    SFML::System SFML::Window SFML::Graphics
    GLEW::GLEW
    OpenGL::GL
    unofficial::libvpx::libvpx
    unofficial::libwebm::libwebm
    GIF::GIF
    ${X264_TARGET}
    wx::core wx::base)

  if(WIN32)
    target_link_libraries(creator PRIVATE ws2_32 winmm shlwapi)
  endif()
endif()

# ---------------------------------------------------------------------------
# Headless tests (no SFML/protobuf). Default OFF; enable with -DTRANCE_BUILD_TESTS=ON
# and run via `ctest`. These link only the parser/compiler/cyclers, so they build
# with a bare C++17 compiler and need none of the vcpkg media dependencies.
# ---------------------------------------------------------------------------
option(TRANCE_BUILD_TESTS "Build headless unit tests" OFF)
if(TRANCE_BUILD_TESTS)
  enable_testing()

  # v3 grammar test: the primitive grammar (nesting, crossfade-from-primitives, lexical
  # register scoping, all 8 built-ins), PLUS evaluating (not just string-matching) every
  # lowered render [expr] against live cycler state -- closes the gap where parse-time name
  # resolution can't catch a malformed expr the lowering code itself emits. The evaluator
  # (pattern::eval_expr / eval_cond_expr) lives inline in render_eval.h precisely so this stays
  # headless: render_eval.cpp's eval_render() needs the full VisualRender type from api.h, which
  # includes trance/media/font.h -> SFML/Graphics.hpp and would drag the vcpkg media deps into
  # this "bare C++17 compiler" target. render_eval.cpp is deliberately NOT a source here.
  add_executable(v3_grammar_test
    src/trance/visual/builtin_patterns_v3.cpp
    src/trance/visual/cyclers.cpp
    src/trance/visual/pattern_compiler.cpp
    src/trance/visual/pattern_parser_v3.cpp
    tests/v3_grammar_test.cpp)
  target_include_directories(v3_grammar_test PRIVATE src/)
  target_link_libraries(v3_grammar_test PRIVATE trance_options)
  add_test(NAME v3_grammar_test COMMAND v3_grammar_test)

  # session.json / system.json mapping test (docs/session-json-format.md). Headless (no
  # SFML/window/GL/audio) but not "bare C++17" like v3_grammar_test above: it links
  # common_lib to exercise the real session_json.cpp against protobuf + nlohmann_json,
  # the same dependency shape session.cpp itself has.
  add_executable(session_json_test tests/session_json_test.cpp)
  target_link_libraries(session_json_test PRIVATE common_lib trance_options)
  add_test(NAME session_json_test COMMAND session_json_test)

  # #21 command channel protocol test (docs/spec-mcp-ambient-daemon.md sec 3/4). Bare
  # C++17 like v3_grammar_test above: command_protocol.{h,cpp} is the pure line->Verb
  # parser with zero socket/Director/Audio dependency by design (see the header comment),
  # so this links neither common_lib nor any socket TU -- just the parser itself.
  add_executable(command_protocol_test
    src/trance/net/command_protocol.cpp
    tests/command_protocol_test.cpp)
  target_include_directories(command_protocol_test PRIVATE src/)
  target_link_libraries(command_protocol_test PRIVATE trance_options)
  add_test(NAME command_protocol_test COMMAND command_protocol_test)
endif()
