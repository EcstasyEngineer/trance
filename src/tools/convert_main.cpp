// trance_convert -- one-shot legacy .session/.cfg (textproto) -> JSON converter.
// Spec: docs/session-json-format.md sec 7 ("Converter"). Playback (session.{h,cpp}) is
// JSON-only per sec 8. Parsing happens in load_legacy_session/load_legacy_system
// against the FROZEN fork-point schema (src/common/legacy.proto, issue #47): an
// upstream-era file imports; a file carrying a field this fork added is rejected with
// a diagnostic, not half-read. Fork-era files are already JSON and never come here.
//
// No SFML/window deps -- links common_lib (protobuf + nlohmann_json) only.
#include <common/common.h>
#include <common/session.h>
#include <common/session_json.h>
#include <common/session_legacy.h>
#include <common/util.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#pragma warning(push, 0)
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  namespace fs = std::filesystem;

  // Mirrors session.cpp's has_json_extension (that file only recognizes .json; the
  // legacy formats it now refuses to load are exactly what this tool reads).
  bool has_json_extension(const std::string& path)
  {
    return ext_is(path, "json");
  }

  bool has_extension(const std::string& path, const std::string& ext)
  {
    return ext_is(path, ext);
  }

  // Default output naming (spec sec 7): a sibling <name>.session.json / system.json.
  // ".session" and ".cfg" are stripped as a single trailing extension (not just the
  // last dot) so "default.session" -> "default.session.json", matching the spec's
  // double-extension convention, and "system.cfg" -> "system.json".
  std::string default_output_path(const std::string& input, bool is_system)
  {
    fs::path in{input};
    if (is_system) {
      return (in.parent_path() / "system.json").string();
    }
    std::string stem = in.filename().string();
    static const std::string suffix = ".session";
    if (stem.size() >= suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
      stem = stem.substr(0, stem.size() - suffix.size());
    }
    return (in.parent_path() / (stem + ".session.json")).string();
  }

  bool is_system_input(const std::string& path)
  {
    // .cfg is always system config; anything else (.session or extensionless) is a
    // session. This matches the two legacy filenames the spec and codebase use:
    // "*.session" and "system.cfg" (common.h).
    return has_extension(path, "cfg");
  }

  void print_usage(const char* argv0)
  {
    std::cerr << "usage: " << argv0 << " <in.session|in.cfg> [out] [--force] [--check]\n"
              << "  Converts a legacy trance textproto session/config file to JSON\n"
              << "  (docs/session-json-format.md). Refuses to overwrite an existing\n"
              << "  output file unless --force is given.\n"
              << "  --check: after writing, load the JSON back via the JSON loader and\n"
              << "           report whether it round-trips into an equivalent proto.\n"
              << "           NOTE: this only verifies the JSON round-trip is faithful to\n"
              << "           what was written -- it does not independently verify that\n"
              << "           validate_session()/validate_program() (session.cpp) produced\n"
              << "           correct data from the legacy proto in the first place. If the\n"
              << "           written program_map has more enabled_theme entries than the\n"
              << "           session's real theme count, or their random_weight is 0 where\n"
              << "           it shouldn't be, that's a bug upstream of this tool.\n";
  }

  // Session summary counts requested by the task (programs/playlists/themes/patterns).
  void print_session_summary(const trance_pb::Session& session)
  {
    uint32_t pattern_count = 0;
    for (const auto& pair : session.program_map()) {
      pattern_count += static_cast<uint32_t>(pair.second.custom_visual_pattern_size());
    }
    std::cout << "  programs:  " << session.program_map_size() << "\n"
              << "  playlists: " << session.playlist_size() << "\n"
              << "  themes:    " << session.theme_map_size() << "\n"
              << "  patterns:  " << pattern_count << "\n";
  }

  // The saver normalizes path separators to forward slashes (spec sec 1, "Path
  // contract"); this is a documented, intentional, lossy-but-equivalent transform, not
  // data loss -- so before comparing the freshly-parsed original against the
  // JSON-round-tripped copy, apply the same normalization to the original's path fields.
  // Without this, --check would false-positive-fail on every legacy session authored on
  // Windows (i.e. nearly all of them), since their `image_path`/`animation_path`/
  // `font_path`/`audio_event.path` entries are backslash-separated as authored.
  void normalize_backslash_paths(std::string& path)
  {
    std::replace(path.begin(), path.end(), '\\', '/');
  }

  void normalize_session_paths_for_compare(trance_pb::Session& session)
  {
    for (auto& theme_pair : *session.mutable_theme_map()) {
      auto& theme = theme_pair.second;
      for (auto& p : *theme.mutable_image_path()) normalize_backslash_paths(p);
      for (auto& p : *theme.mutable_animation_path()) normalize_backslash_paths(p);
      for (auto& p : *theme.mutable_font_path()) normalize_backslash_paths(p);
    }
    for (auto& playlist_pair : *session.mutable_playlist()) {
      for (auto& event : *playlist_pair.second.mutable_audio_event()) {
        normalize_backslash_paths(*event.mutable_path());
      }
    }
  }

  // Structural equivalence check for the --check round-trip, per spec sec 7's
  // "Acceptance test": MessageDifferencer with float tolerance 1/255 on colour
  // components (round(f*255) quantization on save is lossy but within that tolerance).
  // MessageDifferencer -- not a raw text-format string compare -- because
  // TextFormat::PrintToString's `map<>` field order reflects the map's internal hash
  // bucket layout, which differs between two separately-populated map instances (the
  // freshly-parsed proto vs. the one rebuilt from JSON) even when their *contents* are
  // identical; a string compare would false-positive-fail on every session with more
  // than one map entry.
  bool protos_equivalent(const google::protobuf::Message& a, const google::protobuf::Message& b,
                          std::string* report)
  {
    google::protobuf::util::MessageDifferencer differencer;
    differencer.set_float_comparison(google::protobuf::util::MessageDifferencer::APPROXIMATE);
    // trance_pb::Colour is the only float-bearing message (spec sec 2.3: colours quantize
    // to 8-bit on save/load, round(f*255)/255 -- lossless in practice but not bit-exact).
    static const auto* colour_descriptor = trance_pb::Colour::descriptor();
    differencer.SetFractionAndMargin(
        colour_descriptor->FindFieldByName("r"), 0.f, 1.f / 255);
    differencer.SetFractionAndMargin(
        colour_descriptor->FindFieldByName("g"), 0.f, 1.f / 255);
    differencer.SetFractionAndMargin(
        colour_descriptor->FindFieldByName("b"), 0.f, 1.f / 255);
    differencer.SetFractionAndMargin(
        colour_descriptor->FindFieldByName("a"), 0.f, 1.f / 255);
    differencer.ReportDifferencesToString(report);
    return differencer.Compare(a, b);
  }

} // namespace

int main(int argc, char** argv)
{
  std::vector<std::string> positional;
  bool force = false;
  bool check = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--force") {
      force = true;
    } else if (arg == "--check") {
      check = true;
    } else if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    } else {
      positional.push_back(arg);
    }
  }

  if (positional.empty() || positional.size() > 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string input = positional[0];
  bool is_system = is_system_input(input);
  std::string output = positional.size() == 2 ? positional[1] : default_output_path(input, is_system);

  if (has_json_extension(input)) {
    std::cerr << "error: " << input << " is already JSON; nothing to convert\n";
    return 1;
  }

  if (fs::exists(output) && !force) {
    std::cerr << "error: refusing to overwrite existing file " << output << " (use --force)\n";
    return 1;
  }

  try {
    if (is_system) {
      auto system = load_legacy_system(input);
      // Legacy/dead-field handling + clamps happen here, matching the spec pipeline
      // step 2 -- the JSON emitter never sees unvalidated data.
      validate_system(system);
      save_system_json(system, output);

      std::cout << "wrote " << output << "\n";
      if (system.renderer() == trance_pb::System::OCULUS) {
        std::cerr << "warning: renderer OCULUS has no JSON form; converted to monitor\n";
      }

      if (check) {
        auto reloaded = load_system_json(output);
        validate_system(reloaded);
        std::string report;
        bool ok = protos_equivalent(system, reloaded, &report);
        std::cout << "round-trip check: " << (ok ? "OK" : "MISMATCH") << "\n";
        if (!ok) {
          std::cerr << report;
          return 2;
        }
      }
    } else {
      auto session = load_legacy_session(input);
      validate_session(session);

      auto root = fs::path{output}.parent_path().string();
      if (root.empty()) {
        root = ".";
      }
      SessionJsonSidecar sidecar;
      save_session_json(session, output, root, sidecar);

      std::cout << "wrote " << output << "\n";
      print_session_summary(session);

      if (check) {
        SessionJsonSidecar reload_sidecar;
        auto reloaded = load_session_json(output, root, reload_sidecar);
        validate_session(reloaded);
        // The saver normalizes path separators (spec sec 1); apply the same
        // normalization to the pre-save proto so the comparison isn't comparing
        // backslash-authored paths against their forward-slash-normalized JSON form.
        normalize_session_paths_for_compare(session);
        std::string report;
        bool ok = protos_equivalent(session, reloaded, &report);
        std::cout << "round-trip check: " << (ok ? "OK" : "MISMATCH") << "\n";
        if (!ok) {
          std::cerr << report;
          return 2;
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
