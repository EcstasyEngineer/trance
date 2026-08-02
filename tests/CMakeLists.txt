# Headless tests (no SFML/protobuf unless noted). Built only under
# -DTRANCE_BUILD_TESTS=ON; run via `ctest`. Living in their own subdirectory keeps
# every test artifact — the exes, the MSBuild .dir intermediates, the .vcxproj
# files — under build/<preset>/tests/ instead of scattered across the top of the
# build tree, so Release/ holds only the shippable binaries.

# v3 grammar test: the primitive grammar (nesting, crossfade-from-primitives, lexical
# register scoping, all 8 built-ins), PLUS evaluating (not just string-matching) every
# lowered render [expr] against live cycler state -- closes the gap where parse-time name
# resolution can't catch a malformed expr the lowering code itself emits. The evaluator
# (pattern::eval_expr / eval_cond_expr) lives inline in render_eval.h precisely so this stays
# headless: render_eval.cpp's eval_render() needs the full VisualRender type from api.h, which
# includes trance/media/font.h -> SFML/Graphics.hpp and would drag the vcpkg media deps into
# this "bare C++17 compiler" target. render_eval.cpp is deliberately NOT a source here.
add_executable(v3_grammar_test
  ${CMAKE_SOURCE_DIR}/src/trance/visual/builtin_patterns_v3.cpp
  ${CMAKE_SOURCE_DIR}/src/trance/visual/cyclers.cpp
  ${CMAKE_SOURCE_DIR}/src/trance/visual/pattern_compiler.cpp
  ${CMAKE_SOURCE_DIR}/src/trance/visual/pattern_parser_v3.cpp
  v3_grammar_test.cpp)
target_include_directories(v3_grammar_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(v3_grammar_test PRIVATE trance_options)
add_test(NAME v3_grammar_test COMMAND v3_grammar_test)

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
# audio) but not "bare C++17" like v3_grammar_test above: it links common_lib to
# exercise the real session_json.cpp against protobuf + nlohmann_json, the same
# dependency shape session.cpp itself has.
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

# #21 command channel protocol test (docs/spec-mcp-ambient-daemon.md sec 3/4). Bare
# C++17 like v3_grammar_test above: command_protocol.{h,cpp} is the pure line->Verb
# parser with zero socket/Director/Audio dependency by design (see the header comment),
# so this links neither common_lib nor any socket TU -- just the parser itself.
add_executable(command_protocol_test
  ${CMAKE_SOURCE_DIR}/src/trance/net/command_protocol.cpp
  command_protocol_test.cpp)
target_include_directories(command_protocol_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(command_protocol_test PRIVATE trance_options)
add_test(NAME command_protocol_test COMMAND command_protocol_test)
