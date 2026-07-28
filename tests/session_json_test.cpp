// Sanity test for the session.json / system.json mapping (docs/session-json-format.md).
// Exercises the load/save round trip, strict unknown-key errors, colour hex <-> float
// conversion, and the both-oneof/duplicate-pattern-name/missing-pattern-file error paths --
// the parts of the spec most likely to silently drift from the hand-editing contract.
//
// Headless: no SFML. Links common_lib (protobuf + nlohmann_json), same as session.cpp
// itself; not "bare C++17 compiler" like v3_grammar_test but does not touch a window,
// GL context, or audio device. Run via ctest.
#include <common/session.h>
#include <common/session_archive.h>
#include <common/session_json.h>
#include <common/trance.pb.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#pragma warning(push, 0)
#include <miniz/miniz.h>
#pragma warning(pop)

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
    std::ofstream f{path, std::ios::binary};
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

  // #34: `pinned` on a built-in visual_type entry and on a custom_visual_pattern is
  // additive and omitted-when-false, mirroring enabled_theme[].pinned. Round-trips the
  // pin through save+reload, and checks the false case leaves no key behind (a session
  // that never pins must not grow noise on every save).
  void test_visual_pinned_round_trip()
  {
    auto root = scratch_root() / "visual_pinned";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": {
        "visual_type": [ { "type": "slow_flash", "random_weight": 3, "pinned": true },
                         { "type": "animation", "random_weight": 1 } ],
        "custom_visual_pattern": [
          { "name": "a", "file": "patterns/a.pattern", "random_weight": 2, "enabled": true },
          { "name": "b", "file": "patterns/b.pattern", "random_weight": 5, "enabled": true,
            "pinned": true } ]
      } }
    })json");
    write_file(root / "patterns" / "a.pattern", "# a\n");
    write_file(root / "patterns" / "b.pattern", "# b\n");

    SessionJsonSidecar sidecar;
    auto session = load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    const auto& program = session.program_map().at("p");
    check(program.visual_type(0).pinned(), "visual_type[].pinned loads");
    check(!program.visual_type(1).pinned(), "absent visual_type[].pinned defaults false");
    check(!program.custom_visual_pattern(0).pinned(),
          "absent custom_visual_pattern[].pinned defaults false");
    check(program.custom_visual_pattern(1).pinned(), "custom_visual_pattern[].pinned loads");

    save_session_json(session, (root / "out.session.json").string(), root.string(), sidecar);
    SessionJsonSidecar reload_sidecar;
    auto reloaded =
        load_session_json((root / "out.session.json").string(), root.string(), reload_sidecar);
    const auto& rp = reloaded.program_map().at("p");
    check(rp.visual_type(0).pinned() && !rp.visual_type(1).pinned(),
          "visual_type[].pinned survives save+reload");
    check(rp.custom_visual_pattern(1).pinned() && !rp.custom_visual_pattern(0).pinned(),
          "custom_visual_pattern[].pinned survives save+reload");

    // Exactly two "pinned" keys in the output: the unpinned rows must stay silent.
    auto saved = read_file(root / "out.session.json");
    std::size_t pinned_keys = 0;
    for (std::size_t at = saved.find("\"pinned\""); at != std::string::npos;
         at = saved.find("\"pinned\"", at + 1)) {
      ++pinned_keys;
    }
    check(pinned_keys == 2, "pinned is omitted when false (only the two true rows emit it)");
  }

  // #34: validate_program enforces ONE pin across the whole visual pool -- built-in
  // visual_type entries and custom patterns share a single selection lottery
  // (director.cpp), so they share a single pin. Built-ins win over customs, matching
  // the iteration order the enabled_theme pass uses.
  void test_single_visual_pin_enforced()
  {
    auto root = scratch_root() / "visual_pin_single";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": {
        "enabled_theme": [ { "theme_name": "all", "random_weight": 1 } ],
        "visual_type": [ { "type": "slow_flash", "random_weight": 1, "pinned": true },
                         { "type": "animation", "random_weight": 1, "pinned": true } ],
        "custom_visual_pattern": [
          { "name": "a", "file": "patterns/a.pattern", "random_weight": 1, "enabled": true,
            "pinned": true } ]
      } },
      "theme_map": { "all": { "scan": "media" } }
    })json");
    write_file(root / "patterns" / "a.pattern", "# a\n");
    std::filesystem::create_directories(root / "media");

    auto session = load_session((root / "s.session.json").string());
    const auto& program = session.program_map().at("p");
    int pins = 0;
    for (const auto& vt : program.visual_type()) {
      if (vt.pinned()) ++pins;
    }
    for (const auto& p : program.custom_visual_pattern()) {
      if (p.pinned()) ++pins;
    }
    check(pins == 1, "validate_program leaves exactly one pin across the visual pool");
    check(program.visual_type(0).pinned(), "the first pin in iteration order is the one kept");
  }

  // #34: a pin on a DISABLED custom pattern is dead -- rebuild_custom_patterns skips
  // the pattern outright, so the pin would force a visual that never compiles. Cleared
  // at validation so the panel and Director can't disagree about it.
  void test_pin_on_disabled_pattern_is_cleared()
  {
    auto root = scratch_root() / "pin_disabled";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": {
        "enabled_theme": [ { "theme_name": "all", "random_weight": 1 } ],
        "visual_type": [ { "type": "slow_flash", "random_weight": 1 } ],
        "custom_visual_pattern": [
          { "name": "a", "file": "patterns/a.pattern", "random_weight": 4, "pinned": true } ]
      } },
      "theme_map": { "all": { "scan": "media" } }
    })json");
    write_file(root / "patterns" / "a.pattern", "# a\n");
    std::filesystem::create_directories(root / "media");

    auto session = load_session((root / "s.session.json").string());
    const auto& program = session.program_map().at("p");
    check(!program.custom_visual_pattern(0).pinned(),
          "a pin on a disabled custom pattern is cleared by validate_program");
  }

  // #34: the all-zero visual rescue keys off BUILT-IN weights only. A custom pattern
  // can fail to parse and drop out of Director's lottery at runtime, so its weight is
  // not evidence the program has anything playable -- letting it suppress the rescue
  // would leave Director with an empty pool and a null _visual to dereference.
  void test_custom_weight_does_not_suppress_visual_default_rescue()
  {
    auto root = scratch_root() / "custom_no_rescue";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": {
        "enabled_theme": [ { "theme_name": "all", "random_weight": 1 } ],
        "visual_type": [ { "type": "slow_flash", "random_weight": 0 } ],
        "custom_visual_pattern": [
          { "name": "a", "file": "patterns/a.pattern", "random_weight": 9, "enabled": true } ]
      } },
      "theme_map": { "all": { "scan": "media" } }
    })json");
    write_file(root / "patterns" / "a.pattern", "# a\n");
    std::filesystem::create_directories(root / "media");

    auto session = load_session((root / "s.session.json").string());
    const auto& program = session.program_map().at("p");
    uint64_t builtin_total = 0;
    for (const auto& vt : program.visual_type()) {
      builtin_total += vt.random_weight();
    }
    check(builtin_total > 0,
          "all-zero built-ins get the default rescue even when a custom carries weight");
  }

  // #34: a PINNED built-in at weight 0 is the deliberate "only this one" state, so it
  // must survive the rescue that an unpinned all-zero pool triggers.
  void test_pinned_builtin_survives_zero_weight_rescue()
  {
    auto root = scratch_root() / "pinned_zero";
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": {
        "enabled_theme": [ { "theme_name": "all", "random_weight": 1 } ],
        "visual_type": [ { "type": "slow_flash", "random_weight": 0, "pinned": true },
                         { "type": "animation", "random_weight": 0 } ]
      } },
      "theme_map": { "all": { "scan": "media" } }
    })json");
    std::filesystem::create_directories(root / "media");

    auto session = load_session((root / "s.session.json").string());
    const auto& program = session.program_map().at("p");
    check(program.visual_type_size() == 2, "a pinned all-zero visual pool is left alone");
    check(program.visual_type_size() == 2 && program.visual_type(0).pinned(),
          "the pin survives (it is the whole point of the zero weights)");
  }

  // #34: a pin on the NONE visual type is not a pin. NONE is the enum's zero and
  // Director stores its pinned type in a uint32 where 0 means "nothing pinned", so
  // honouring such a pin would suppress the all-zero rescue and leave Director with an
  // empty pool -- which change_visual() leaves as a null _visual for update() to
  // dereference. Cleared at validation, and the rescue must still fire.
  // Built through the proto rather than JSON on purpose: "none" is not in the JSON
  // enum table (session_json.cpp), so a pinned NONE can only reach validation from a
  // converted legacy .session or a default-constructed entry -- exactly the paths that
  // would otherwise slip past unnoticed.
  void test_pinned_none_visual_type_is_not_a_pin()
  {
    trance_pb::Session session;
    session.set_first_playlist_item("main");
    (*session.mutable_playlist())["main"].mutable_standard()->set_program("p");
    (*session.mutable_theme_map())["all"];
    auto& built = (*session.mutable_program_map())["p"];
    auto* theme = built.add_enabled_theme();
    theme->set_theme_name("all");
    theme->set_random_weight(1);
    auto* vt = built.add_visual_type();
    vt->set_type(trance_pb::Program_VisualType_NONE);
    vt->set_random_weight(0);
    vt->set_pinned(true);
    validate_session(session);

    const auto& program = session.program_map().at("p");
    uint64_t total = 0;
    bool any_pinned = false;
    for (const auto& vt : program.visual_type()) {
      total += vt.random_weight();
      any_pinned = any_pinned || vt.pinned();
    }
    check(!any_pinned, "a pin on NONE is cleared rather than honoured");
    check(total > 0, "the all-zero rescue still fires, so the pool is never left empty");
  }

  // A one-image PNG, byte-for-byte. is_image() only sniffs the extension, but the archive
  // path and any future loader-side check want a file that is actually a PNG.
  const unsigned char kTinyPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
      0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
      0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00,
      0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
      0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

  void write_png(const std::filesystem::path& path)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f{path, std::ios::binary};
    f.write(reinterpret_cast<const char*>(kTinyPng), sizeof(kTinyPng));
  }

  // Builds a session root with a single `scan` theme over `media/`, containing one image
  // in the scan dir itself and one in a nested subdirectory.
  std::filesystem::path make_scan_session(const std::string& name)
  {
    auto root = scratch_root() / name;
    std::filesystem::remove_all(root);
    write_file(root / "s.session.json", R"json({
      "format": "trance-session", "format_version": 1,
      "first_playlist_item": "main",
      "playlist": { "main": { "standard": { "program": "p" } } },
      "program_map": { "p": { "enabled_theme": [ { "theme_name": "all" } ] } },
      "theme_map": { "all": { "scan": "media" } }
    })json");
    write_png(root / "media" / "a.png");
    write_png(root / "media" / "nested" / "b.png");
    return root;
  }

  // #37: `scan` expands via search_resources(theme, <scan dir>), which yields paths
  // relative to the SCAN DIRECTORY. Every consumer of theme.image_path() -- ThemeBank's
  // loader and the archive's file walk alike -- resolves against the SESSION ROOT, so the
  // expansion has to be root-relative ("media/a.png"), not scan-relative ("a.png").
  void test_scan_expands_to_root_relative_paths()
  {
    auto root = make_scan_session("scan_paths");
    SessionJsonSidecar sidecar;
    auto session = load_session_json((root / "s.session.json").string(), root.string(), sidecar);

    std::set<std::string> images;
    for (const auto& p : session.theme_map().at("all").image_path()) {
      images.insert(p);
    }
    check(images.count("media/a.png") == 1, "scan image path is root-relative");
    check(images.count("media/nested/b.png") == 1, "nested scan image path is root-relative");
  }

  // #37: the archive walks the in-memory session's theme media lists and resolves each
  // entry against the session root, so scan-derived media only lands in the zip if the
  // expansion above is root-relative.
  void test_archive_includes_scan_derived_media()
  {
    auto root = make_scan_session("scan_archive");
    auto archive = root / "out.trance";

    std::string error;
    bool ok = export_session_archive((root / "s.session.json").string(), archive.string(), error);
    check(ok, "export_session_archive succeeds for a scan theme");
    if (!ok) {
      std::cout << "  (unexpected) " << error << "\n";
      return;
    }

    std::set<std::string> members;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archive.string().c_str(), 0)) {
      check(false, "archive is readable as a zip");
      return;
    }
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
      mz_zip_archive_file_stat stat{};
      if (mz_zip_reader_file_stat(&zip, i, &stat)) {
        members.insert(stat.m_filename);
      }
    }
    mz_zip_reader_end(&zip);

    check(members.count("session.json") == 1, "archive holds session.json at its root");
    check(members.count("media/a.png") == 1, "archive includes scan-derived media");
    check(members.count("media/nested/b.png") == 1, "archive includes nested scan-derived media");
  }

  // The scan sidecar still suppresses the expanded lists on save (spec sec 5), so the
  // root-relative expansion above must not leak explicit image_path entries into the
  // archived session.json -- reloading it re-derives them from `scan`.
  void test_scan_theme_saves_as_scan_key_only()
  {
    auto root = make_scan_session("scan_save");
    SessionJsonSidecar sidecar;
    auto session = load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    save_session_json(session, (root / "out.session.json").string(), root.string(), sidecar);

    auto saved = read_file(root / "out.session.json");
    check(saved.find("\"scan\"") != std::string::npos, "saved scan theme re-emits the scan key");
    check(saved.find("image_path") == std::string::npos,
          "saved scan theme omits the expanded image list");
  }

  // #36: a folder theme is COMPLETE content -- the theme-level walker gains text/audio
  // branches, so dropping a .txt and a .wav into the scan dir yields text lines and theme
  // audio, not just images.
  void test_scan_theme_includes_text_and_audio()
  {
    auto root = make_scan_session("scan_complete");
    write_file(root / "media" / "lines.txt", "sink deeper now\n\nobey\n");
    write_file(root / "media" / "voice.wav", "not really a wav");

    SessionJsonSidecar sidecar;
    auto session = load_session_json((root / "s.session.json").string(), root.string(), sidecar);
    const auto& theme = session.theme_map().at("all");

    std::set<std::string> lines;
    for (const auto& t : theme.text_line()) {
      lines.insert(t);
    }
    check(lines.size() == 2, "scan reads every non-blank line of a .txt");
    // Uppercased and split at the space nearest the middle, like the session-level walker.
    check(lines.count("SINK\nDEEPER NOW") == 1, "scanned text is uppercased and mid-split");
    check(lines.count("OBEY") == 1, "a single-word line has nothing to split");

    std::set<std::string> audio;
    for (const auto& p : theme.audio_path()) {
      audio.insert(p);
    }
    check(audio.count("media/voice.wav") == 1, "scanned audio is root-relative theme audio");
  }

  // #36: "if it's in the folder, that's the content" -- no extension allowlist. An unknown
  // extension becomes an image (the decode layer marks a bad file dead once), while session
  // machinery and dotfiles are still skipped explicitly.
  void test_scan_does_not_filter_on_extension()
  {
    auto root = make_scan_session("scan_no_filter");
    write_file(root / "media" / "weird.tiff", "junk");
    write_file(root / "media" / "no_extension_at_all", "junk");
    write_file(root / "media" / "other.session.json", "{}");
    write_file(root / "media" / "notes.pattern", "effect flash;");
    write_file(root / "media" / ".hidden.png", "junk");
    write_file(root / "media" / ".git" / "config", "junk");

    SessionJsonSidecar sidecar;
    auto session = load_session_json((root / "s.session.json").string(), root.string(), sidecar);

    std::set<std::string> images;
    for (const auto& p : session.theme_map().at("all").image_path()) {
      images.insert(p);
    }
    check(images.count("media/weird.tiff") == 1, "an unknown extension is scanned as an image");
    check(images.count("media/no_extension_at_all") == 1, "an extensionless file is an image");
    check(images.count("media/other.session.json") == 0, "a session file is not content");
    check(images.count("media/notes.pattern") == 0, "a pattern file is not content");
    check(images.count("media/.hidden.png") == 0, "a dotfile is not content");
    check(images.count("media/.git/config") == 0, "a dotted directory's contents are not content");
  }

  // #36: the no-arg cold start must preserve folder-ness. search_resources reports which
  // themes are pure subdirectory references; seeding the sidecar with them makes the saved
  // default.json hold {"scan": <subdir>} rather than a frozen media list.
  void test_bootstrap_scan_writes_scan_themes()
  {
    auto root = scratch_root() / "bootstrap_scan";
    std::filesystem::remove_all(root);
    write_png(root / "ocean" / "a.png");
    write_file(root / "ocean" / "lines.txt", "drift\n");
    write_png(root / "fire" / "b.png");

    trance_pb::Session session = get_default_session();
    SessionJsonSidecar sidecar;
    search_resources(session, root.string(), sidecar.theme_scan);

    check(sidecar.theme_scan.size() == 2, "each subdirectory theme is reported as a scan");
    check(sidecar.theme_scan.count("ocean") == 1, "the ocean subdirectory is a scan theme");
    check(sidecar.theme_scan.count("fire") == 1, "the fire subdirectory is a scan theme");

    save_session_json(session, (root / "default.json").string(), root.string(), sidecar);
    auto saved = read_file(root / "default.json");
    check(saved.find("\"scan\": \"ocean\"") != std::string::npos, "default.json holds a scan key");
    check(saved.find("image_path") == std::string::npos,
          "bootstrap does not freeze the media list into default.json");
    check(saved.find("text_line") == std::string::npos,
          "bootstrap does not freeze scanned text lines either");

    // And the written file reloads to the same content it was scanned from.
    SessionJsonSidecar reloaded_sidecar;
    auto reloaded =
        load_session_json((root / "default.json").string(), root.string(), reloaded_sidecar);
    const auto& ocean = reloaded.theme_map().at("ocean");
    check(ocean.image_path_size() == 1 && ocean.image_path(0) == "ocean/a.png",
          "the reloaded scan theme re-derives its image");
    check(ocean.text_line_size() == 1 && ocean.text_line(0) == "DRIFT",
          "the reloaded scan theme re-derives its text");
  }

  // #36 guard: once loose files at the root are merged into every theme (the /wildcards/
  // pseudo-theme), no single directory reproduces a theme's content -- so nothing may be
  // reported as a scan and the explicit lists stay the faithful record.
  void test_bootstrap_wildcards_suppresses_scan_themes()
  {
    auto root = scratch_root() / "bootstrap_wildcards";
    std::filesystem::remove_all(root);
    write_png(root / "ocean" / "a.png");
    write_png(root / "loose.png");

    trance_pb::Session session = get_default_session();
    SessionJsonSidecar sidecar;
    search_resources(session, root.string(), sidecar.theme_scan);

    check(sidecar.theme_scan.empty(), "a merged wildcards theme suppresses scan reporting");
    const auto& ocean = session.theme_map().at("ocean");
    check(ocean.image_path_size() == 2, "the wildcards image still merges into the folder theme");
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
  test_visual_pinned_round_trip();
  test_single_visual_pin_enforced();
  test_pin_on_disabled_pattern_is_cleared();
  test_custom_weight_does_not_suppress_visual_default_rescue();
  test_pinned_builtin_survives_zero_weight_rescue();
  test_pinned_none_visual_type_is_not_a_pin();
  test_scan_expands_to_root_relative_paths();
  test_archive_includes_scan_derived_media();
  test_scan_theme_saves_as_scan_key_only();
  test_scan_theme_includes_text_and_audio();
  test_scan_does_not_filter_on_extension();
  test_bootstrap_scan_writes_scan_themes();
  test_bootstrap_wildcards_suppresses_scan_themes();

  if (g_fail) {
    std::cout << g_fail << " check(s) failed\n";
    return 1;
  }
  std::cout << "all checks passed\n";
  return 0;
}
