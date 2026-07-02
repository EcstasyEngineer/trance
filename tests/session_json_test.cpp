// Sanity test for the session.json / system.json mapping (docs/session-json-format.md).
// Exercises the load/save round trip, strict unknown-key errors, colour hex <-> float
// conversion, and the both-oneof/duplicate-pattern-name/missing-pattern-file error paths --
// the parts of the spec most likely to silently drift from the hand-editing contract.
//
// Headless: no SFML. Links common_lib (protobuf + nlohmann_json), same as session.cpp
// itself; not "bare C++17 compiler" like v3_grammar_test but does not touch a window,
// GL context, or audio device. Run via ctest.
#include <common/session_json.h>
#include <common/trance.pb.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
  int g_fail = 0;
  void check(bool ok, const std::string& what)
  {
    std::cout << (ok ? "  ok  " : "FAIL  ") << what << "\n";
    if (!ok) ++g_fail;
  }

  // Scratch directory under the build tree so this is safe to run repeatedly / in parallel
  // ctest runs without clobbering a fixture checked into the repo.
  std::filesystem::path scratch_root()
  {
    auto p = std::filesystem::temp_directory_path() / "trance_session_json_test";
    std::filesystem::create_directories(p);
    return p;
  }

  void write_file(const std::filesystem::path& path, const std::string& content)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f{path};
    f << content;
  }

  std::string read_file(const std::filesystem::path& path)
  {
    std::ifstream f{path};
    return {std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
  }

  const char* kMinimalSession = R"json({
  "format": "trance-session",
  "format_version": 1,
  "_note": "keys starting with _ are comments",
  "first_playlist_item": "main",
  "playlist": { "main": { "standard": { "program": "default" } } },
  "program_map": {
    "default": {
      "enabled_theme": [ { "theme_name": "all", "random_weight": 1 } ],
      "visual_type": [ { "type": "slow_flash", "random_weight": 1 },
                       { "type": "animation", "random_weight": 1 } ],
      "custom_visual_pattern": [
        { "name": "breathe", "file": "patterns/breathe.pattern",
          "random_weight": 3, "enabled": true } ],
      "global_fps": 120,
      "zoom_intensity": 0.5,
      "spiral_colour_a": "#FF96C832",
      "spiral_colour_b": "#00000032",
      "main_text_colour": "#FF96C8E0",
      "shadow_text_colour": "#000000C0",
      "entrainment": {
        "master_db": -28.0,
        "layer": [
          { "center_hz": 312.0, "binaural_hz": 3.0, "pulse_hz": 5.0 },
          { "center_hz": 60.0, "binaural_hz": 3.0, "pulse_hz": 3.25, "amplitude_db": -6.0 } ]
      }
    }
  },
  "theme_map": { "all": { "scan": "media" } }
})json";

  void test_minimal_example_from_spec()
  {
    auto root = scratch_root() / "minimal";
    std::filesystem::remove_all(root);
    write_file(root / "default.session.json", kMinimalSession);
    write_file(root / "patterns" / "breathe.pattern", "# empty pattern\n");
    std::filesystem::create_directories(root / "media");

    SessionJsonSidecar sidecar;
    trance_pb::Session session;
    bool threw = false;
    try {
      session = load_session_json((root / "default.session.json").string(), root.string(), sidecar);
    } catch (const std::exception& e) {
      threw = true;
      std::cout << "  (unexpected) " << e.what() << "\n";
    }
    check(!threw, "spec minimal example loads without error");
    check(session.first_playlist_item() == "main", "first_playlist_item round-trips");
    check(session.playlist().count("main") == 1, "playlist item present");
    check(session.playlist().at("main").has_standard(), "playlist item is standard");
    check(session.playlist().at("main").standard().program() == "default",
          "standard.program round-trips");
    check(session.program_map().count("default") == 1, "program present");

    const auto& program = session.program_map().at("default");
    check(program.global_fps() == 120, "global_fps round-trips");
    check(program.custom_visual_pattern_size() == 1, "one custom_visual_pattern loaded");
    if (program.custom_visual_pattern_size() == 1) {
      check(program.custom_visual_pattern(0).source_text() == "# empty pattern\n",
            "pattern file content -> source_text");
      check(program.custom_visual_pattern(0).name() == "breathe", "pattern name preserved");
    }
    // #FF96C832: r=0xFF g=0x96 b=0xC8 a=0x32
    check(std::abs(program.spiral_colour_a().r() - 1.f) < 1e-6f, "colour r channel (0xFF)");
    check(std::abs(program.spiral_colour_a().g() - 0x96 / 255.f) < 1e-6f, "colour g channel (0x96)");
    check(std::abs(program.spiral_colour_a().b() - 0xC8 / 255.f) < 1e-6f, "colour b channel (0xC8)");
    check(std::abs(program.spiral_colour_a().a() - 0x32 / 255.f) < 1e-6f, "colour a channel (0x32)");
    check(program.entrainment().layer_size() == 2, "entrainment layers loaded");

    check(session.theme_map().count("all") == 1, "theme present");
    check(sidecar.theme_scan.at("all") == "media", "scan sidecar recorded");
    check(sidecar.pattern_file.at({"default", "breathe"}) == "patterns/breathe.pattern",
          "pattern-file sidecar recorded");
  }

  void test_colour_round_trip()
  {
    auto root = scratch_root() / "colour";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": { "main_text_colour": "#123456" } }
    })json");

    SessionJsonSidecar sidecar;
    auto session = load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    // No alpha given -> FF.
    check(std::abs(session.program_map().at("p").main_text_colour().a() - 1.f) < 1e-6f,
          "colour without alpha defaults to opaque");

    SessionJsonSidecar save_sidecar;
    save_session_json(session, (root / "out.session.json").string(), root.string(), save_sidecar);
    auto reloaded_text = read_file(root / "out.session.json");
    check(reloaded_text.find("\"#123456\"") != std::string::npos,
          "saved colour omits redundant alpha byte");
  }

  void test_unknown_key_is_error()
  {
    auto root = scratch_root() / "unknown_key";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" }, "totally_bogus_key": 1 } },
      "program_map": { "p": {} }
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    std::string message;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception& e) {
      threw = true;
      message = e.what();
    }
    check(threw, "unknown key is a load error");
    check(message.find("totally_bogus_key") != std::string::npos,
          "error message names the offending key");
  }

  void test_both_oneof_is_error()
  {
    auto root = scratch_root() / "both_oneof";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" }, "subroutine": [] } },
      "program_map": { "p": {} }
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "standard + subroutine both present is a load error");
  }

  void test_neither_oneof_is_error()
  {
    auto root = scratch_root() / "neither_oneof";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "next_item": [] } },
      "program_map": { "p": {} }
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "neither standard nor subroutine present is a load error");
  }

  void test_duplicate_pattern_name_is_error()
  {
    auto root = scratch_root() / "dup_pattern";
    std::filesystem::remove_all(root);
    write_file(root / "patterns" / "a.pattern", "");
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": { "custom_visual_pattern": [
        { "name": "x", "file": "patterns/a.pattern" },
        { "name": "x", "file": "patterns/a.pattern" }
      ] } }
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "duplicate custom_visual_pattern name within a program is a load error");
  }

  void test_missing_pattern_file_is_error()
  {
    auto root = scratch_root() / "missing_pattern";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": { "custom_visual_pattern": [
        { "name": "x", "file": "patterns/does-not-exist.pattern" }
      ] } }
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "missing pattern file is a load error");
  }

  void test_escaping_path_is_error()
  {
    auto root = scratch_root() / "escaping_path";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": {} },
      "theme_map": { "t": { "image_path": [ "../outside.png" ] } }
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "a '..'-escaping path is a load error");
  }

  void test_wrong_format_discriminator_is_error()
  {
    auto root = scratch_root() / "wrong_format";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-system", "format_version": 1,
      "first_playlist_item": "main"
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "wrong format discriminator is a load error");
  }

  void test_future_version_is_error()
  {
    auto root = scratch_root() / "future_version";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 2,
      "first_playlist_item": "main"
    })json");

    SessionJsonSidecar sidecar;
    bool threw = false;
    try {
      load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "a format_version greater than supported is a load error");
  }

  void test_system_json_round_trip()
  {
    auto root = scratch_root() / "system";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    trance_pb::System system;
    system.set_enable_vsync(true);
    system.set_renderer(trance_pb::System_Renderer_OPENVR);
    system.set_windowed(true);
    system.mutable_draw_depth()->set_draw_depth(.75f);
    system.set_image_cache_size(128);
    auto& last = (*system.mutable_last_session_map())["deep.session.json"];
    (*last.mutable_variable_map())["Mode"] = "Deep";

    auto path = (root / "system.json").string();
    save_system_json(system, path);
    auto reloaded = load_system_json(path);

    check(reloaded.enable_vsync() == true, "system enable_vsync round-trips");
    check(reloaded.renderer() == trance_pb::System_Renderer_OPENVR, "system renderer round-trips");
    check(reloaded.windowed() == true, "system windowed round-trips");
    check(reloaded.has_draw_depth(), "system draw_depth presence round-trips");
    check(std::abs(reloaded.draw_depth().draw_depth() - .75f) < 1e-6f,
          "system draw_depth value round-trips");
    check(!reloaded.has_eye_spacing(), "absent eye_spacing key stays absent (presence semantics)");
    check(reloaded.image_cache_size() == 128, "system image_cache_size round-trips");
    check(reloaded.last_session_map().count("deep.session.json") == 1,
          "last_session_map key round-trips");
    check(reloaded.last_session_map().at("deep.session.json").variable_map().at("Mode") == "Deep",
          "LastSession wrapper flattens to a direct string->string map");
  }

  void test_pattern_slug_assigned_on_save_without_sidecar()
  {
    auto root = scratch_root() / "slug";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    trance_pb::Session session;
    session.set_first_playlist_item("main");
    (*session.mutable_playlist())["main"].mutable_standard()->set_program("p");
    auto& program = (*session.mutable_program_map())["p"];
    auto* src = program.add_custom_visual_pattern();
    src->set_name("Breathe Deep!");
    src->set_source_text("effect flash;\n");
    src->set_enabled(true);

    SessionJsonSidecar sidecar; // empty: no known file yet
    save_session_json(session, (root / "s.session.json").string(), root.string(), sidecar);

    auto slug_path = root / "patterns" / "breathe_deep_.pattern";
    check(std::filesystem::exists(slug_path),
          "pattern with no sidecar entry saves to a slugged patterns/ file");
    check(read_file(slug_path) == "effect flash;\n", "slugged pattern file gets the source text");
  }

} // namespace

int main()
{
  test_minimal_example_from_spec();
  test_colour_round_trip();
  test_unknown_key_is_error();
  test_both_oneof_is_error();
  test_neither_oneof_is_error();
  test_duplicate_pattern_name_is_error();
  test_missing_pattern_file_is_error();
  test_escaping_path_is_error();
  test_wrong_format_discriminator_is_error();
  test_future_version_is_error();
  test_system_json_round_trip();
  test_pattern_slug_assigned_on_save_without_sidecar();

  if (g_fail) {
    std::cout << g_fail << " check(s) failed\n";
    return 1;
  }
  std::cout << "all checks passed\n";
  return 0;
}
