# Headless tests (no SFML/protobuf unless noted). Built only under
# -DTRANCE_BUILD_TESTS=ON; run via `ctest`. Living in their own subdirectory keeps
# every test artifact — the exes, the MSBuild .dir intermediates, the .vcxproj
# files — under build/<preset>/tests/ instead of scattered across the top of the
# build tree, so Release/ holds only the shippable binaries.

# The grammar's lean proof runs INSIDE the product binary: `trance --lint` parses,
# lowers, compiles and expr-evaluates all 8 built-ins (main.cpp run_lint). No GL
# context is created on the lint path, so a bare CI runner can execute it. No
# parallel grammar test target exists -- the product exe IS the harness, and
# `trance --lint some.session` gives pattern authors the same check.
add_test(NAME grammar_lint COMMAND trance --lint)

# Playlist stack machine test. Headless but links common_lib for the proto types the
# playlist map is made of.
add_executable(playlist_runner_test
  ${CMAKE_SOURCE_DIR}/src/trance/playlist_runner.cpp
  playlist_runner_test.cpp)
target_include_directories(playlist_runner_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(playlist_runner_test PRIVATE common_lib trance_options)
add_test(NAME playlist_runner_test COMMAND playlist_runner_test)

# session.json / system.json mapping test (docs/session-json-format.md), including the
# legacy importer's frozen-schema contract (issue #47). Headless (no SFML/window/GL/
# audio): links common_lib to exercise the real session_json.cpp against protobuf +
# nlohmann_json, the same dependency shape session.cpp itself has. Its assertions are
# on in-memory proto state -- unreachable from outside the process, which is why this
# one stays C++.
add_executable(session_json_test session_json_test.cpp)
target_link_libraries(session_json_test PRIVATE common_lib trance_options)
if(MSVC)
  target_compile_options(session_json_test PRIVATE /wd4267)
endif()
add_test(NAME session_json_test COMMAND session_json_test)

# ThemeBank tiered-selection regression test. The ONE test here that is not headless:
# ThemeBank::get_image uploads through Image::ensure_texture_uploaded, which needs a
# current OpenGL context (sf::Context -- no window), so it links SFML and GL and is
# labelled `gpu` so `ctest -LE gpu` stays green on a GPU-less runner. It skips loudly
# with success when no context can be created, rather than reddening CI.
add_executable(theme_bank_test
  ${CMAKE_SOURCE_DIR}/src/trance/media/async_streamer.cpp
  ${CMAKE_SOURCE_DIR}/src/trance/theme_bank.cpp
  theme_bank_test.cpp)
target_include_directories(theme_bank_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(theme_bank_test PRIVATE
  common_lib
  trance_options
  SFML::System SFML::Window SFML::Graphics
  OpenGL::GL)
if(MSVC)
  target_compile_options(theme_bank_test PRIVATE /wd4267)
endif()
add_test(NAME theme_bank_test COMMAND theme_bank_test)
set_tests_properties(theme_bank_test PROPERTIES LABELS gpu TIMEOUT 120)

# The command channel is exercised end-to-end from Python against a LIVE trance.exe
# (qa_command_channel.py -- issue #29): real socket, real dispatch, real replies.
# Deliberately not registered with ctest: it opens a window and needs a desktop
# session, so it is a hands-on QA harness, not a build gate.
