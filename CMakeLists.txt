cmake_minimum_required(VERSION 3.20)
project(trance CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DEPS ${CMAKE_SOURCE_DIR}/dependencies)

# MSVC runtime: /MDd debug, /MD release
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Shared compiler options / definitions
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
# ---------------------------------------------------------------------------
set(PROTO_OUT ${CMAKE_BINARY_DIR}/common)
file(MAKE_DIRECTORY ${PROTO_OUT})

add_custom_command(
  OUTPUT
    ${PROTO_OUT}/trance.pb.h
    ${PROTO_OUT}/trance.pb.cc
  COMMAND ${DEPS}/x64/protoc.exe
    --cpp_out=${CMAKE_BINARY_DIR}/common
    --proto_path=${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common/trance.proto
  DEPENDS ${CMAKE_SOURCE_DIR}/src/common/trance.proto
  VERBATIM
)

# ---------------------------------------------------------------------------
# Lib dir (generator expression — picks Debug or Release folder)
# ---------------------------------------------------------------------------
set(LIB_DIR "$<IF:$<CONFIG:Debug>,${DEPS}/x64/Debug,${DEPS}/x64/Release>")

# Shared include dirs
set(SHARED_INCLUDES
  ${DEPS}/include
  src/
  ${CMAKE_BINARY_DIR}
)

# ---------------------------------------------------------------------------
# Common static library
# ---------------------------------------------------------------------------
add_library(common_lib STATIC
  src/common/media/image.cpp
  src/common/media/streamer.cpp
  src/common/session.cpp
  src/jpgd/jpgd.cpp
  ${PROTO_OUT}/trance.pb.cc
)

target_include_directories(common_lib PUBLIC ${SHARED_INCLUDES})
target_link_directories(common_lib PUBLIC ${LIB_DIR})

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

target_link_libraries(trance PRIVATE
  common_lib
  ws2_32
  winmm
  shlwapi
  opengl32
  glew
  freetype
  jpeg
  libgif.a
  openvr_api
  $<IF:$<CONFIG:Debug>,sfml-system-s-d,sfml-system-s>
  $<IF:$<CONFIG:Debug>,sfml-window-s-d,sfml-window-s>
  $<IF:$<CONFIG:Debug>,sfml-graphics-s-d,sfml-graphics-s>
  $<IF:$<CONFIG:Debug>,sfml-audio-s-d,sfml-audio-s>
  libx264
  libwebm
  $<IF:$<CONFIG:Debug>,vpxmdd,vpxmd>
  LibOVR
  libprotobuf
  gflags
  openal32
  flac
  ogg
  vorbis
  vorbisfile
)

target_link_options(trance PRIVATE /NODEFAULTLIB:libcmt)

# ---------------------------------------------------------------------------
# DLL copy for trance (post-build)
# libx264.dll: Debug folder for Debug, Release folder for Release
# ---------------------------------------------------------------------------
add_custom_command(TARGET trance POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<IF:$<CONFIG:Debug>,${DEPS}/x64/Debug/libx264.dll,${DEPS}/x64/Release/libx264.dll>"
    "$<TARGET_FILE_DIR:trance>"
  VERBATIM
)

# These DLLs only exist in the Release folder and are used by both configs
foreach(DLL openal32.dll openvr_api.dll libwinpthread-1.dll)
  add_custom_command(TARGET trance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${DEPS}/x64/Release/${DLL}"
      "$<TARGET_FILE_DIR:trance>"
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

target_link_libraries(creator PRIVATE
  common_lib
  ws2_32
  winmm
  shlwapi
  libx264
  libwebm
  freetype
  $<IF:$<CONFIG:Debug>,vpxmdd,vpxmd>
  opengl32
  jpeg
  libgif.a
  $<IF:$<CONFIG:Debug>,sfml-system-s-d,sfml-system-s>
  $<IF:$<CONFIG:Debug>,sfml-graphics-s-d,sfml-graphics-s>
  $<IF:$<CONFIG:Debug>,sfml-window-s-d,sfml-window-s>
  libprotobuf
)

target_link_options(creator PRIVATE
  /NODEFAULTLIB:libcmt
  $<$<NOT:$<CONFIG:Debug>>:/SAFESEH:NO>
)
