cmake_minimum_required(VERSION 3.25)
project(v4_timing_audit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(TRANCE_ROOT "../../.." ABSOLUTE)
set(TRANCE_DEP_INCLUDE "${TRANCE_ROOT}/build/windows-msvc/vcpkg_installed/x64-windows-static-md/include"
    CACHE PATH "Directory containing nlohmann/json.hpp")
add_executable(timing_probe timing_probe.cpp
    "${TRANCE_ROOT}/src/trance/visual/pattern_parser_v4.cpp"
    "${TRANCE_ROOT}/src/trance/visual/pattern_compiler.cpp"
    "${TRANCE_ROOT}/src/trance/visual/cyclers.cpp")
target_include_directories(timing_probe PRIVATE "${TRANCE_ROOT}/src" "${TRANCE_DEP_INCLUDE}")
