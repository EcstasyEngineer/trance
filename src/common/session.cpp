#include <common/session.h>
#include <common/session_json.h>
#include <common/session_legacy.h>
#include <common/util.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  trance_pb::Colour make_colour(float r, float g, float b, float a)
  {
    trance_pb::Colour colour;
    colour.set_r(r / 255);
    colour.set_g(g / 255);
    colour.set_b(b / 255);
    colour.set_a(a / 255);
    return colour;
  }

  std::string split_text_line(const std::string& text)
  {
    // Split strings into two lines at the space closest to the middle. This is
    // sort of ad-hoc. There should probably be a better way that can judge length
    // and split over more than two lines.
    auto l = text.length() / 2;
    auto r = l;
    while (true) {
      if (text[r] == ' ') {
        return text.substr(0, r) + '\n' + text.substr(r + 1);
      }

      if (text[l] == ' ') {
        return text.substr(0, l) + '\n' + text.substr(l + 1);
      }

      if (l == 0 || r == text.length() - 1) {
        break;
      }
      --l;
      ++r;
    }
    return text;
  }

  void set_default_visual_types(trance_pb::Program& program)
  {
    program.clear_visual_type();
    auto add = [&](trance_pb::Program::VisualType type_enum) {
      auto type = program.add_visual_type();
      type->set_type(type_enum);
      type->set_random_weight(1);
    };

    add(trance_pb::Program_VisualType_ACCELERATE);
    add(trance_pb::Program_VisualType_SLOW_FLASH);
    add(trance_pb::Program_VisualType_SUB_TEXT);
    add(trance_pb::Program_VisualType_FLASH_TEXT);
    add(trance_pb::Program_VisualType_PARALLEL);
    add(trance_pb::Program_VisualType_SUPER_PARALLEL);
    add(trance_pb::Program_VisualType_ANIMATION);
    add(trance_pb::Program_VisualType_SUPER_FAST);
  }

  void set_default_program(trance_pb::Session& session, const std::string& name)
  {
    auto& program = (*session.mutable_program_map())[name];
    set_default_visual_types(program);
    program.set_global_fps(120);
    program.set_zoom_intensity(.5f);
    *program.mutable_spiral_colour_a() = make_colour(255, 150, 200, 50);
    *program.mutable_spiral_colour_b() = make_colour(0, 0, 0, 50);
    program.set_reverse_spiral_direction(false);

    *program.mutable_main_text_colour() = make_colour(255, 150, 200, 224);
    *program.mutable_shadow_text_colour() = make_colour(0, 0, 0, 192);

    // Default entrainment bed: a binaural + isochronic drone (carrier / beat /
    // pulse / level). Stored in full so the session is self-contained; delete
    // these layers from the .session to disable the bed.
    auto add_layer = [&](float center, float binaural, float pulse, float amplitude_db) {
      auto* layer = program.mutable_entrainment()->add_layer();
      layer->set_center_hz(center);
      layer->set_binaural_hz(binaural);
      layer->set_pulse_hz(pulse);
      layer->set_amplitude_db(amplitude_db);
    };
    add_layer(312.f, 3.f, 5.f, 0.f);
    add_layer(60.f, 3.f, 3.25f, -6.f);
  }

  void set_default_playlist(trance_pb::Session& session, const std::string& program)
  {
    (*session.mutable_playlist())["default"].mutable_standard()->set_program(program);
  }

  void validate_colour(trance_pb::Colour& colour)
  {
    colour.set_r(std::max(0.f, std::min(1.f, colour.r())));
    colour.set_g(std::max(0.f, std::min(1.f, colour.g())));
    colour.set_b(std::max(0.f, std::min(1.f, colour.b())));
    colour.set_a(std::max(0.f, std::min(1.f, colour.a())));
  }

  void validate_program(trance_pb::Program& program, const trance_pb::Session& session)
  {
    for (const auto& deprecated_theme : program.enabled_theme_name()) {
      auto t = program.add_enabled_theme();
      t->set_theme_name(deprecated_theme);
      t->set_random_weight(1);
    }
    program.clear_enabled_theme_name();

    uint32_t count = 0;
    std::string pinned;
    for (auto& theme : *program.mutable_enabled_theme()) {
      if (session.theme_map().find(theme.theme_name()) != session.theme_map().end()) {
        count += theme.random_weight();
        if (theme.pinned()) {
          if (pinned.empty()) {
            pinned = theme.theme_name();
          } else {
            theme.set_pinned(false);
          }
        }
      } else {
        theme.set_random_weight(0);
        theme.set_pinned(false);
      }
    }
    if (!count) {
      program.clear_enabled_theme();
      if (!pinned.empty()) {
        auto t = program.add_enabled_theme();
        t->set_theme_name(pinned);
        t->set_random_weight(1);
      } else
        for (const auto& pair : session.theme_map()) {
          auto t = program.add_enabled_theme();
          t->set_theme_name(pair.first);
          t->set_random_weight(1);
        }
    }
    // Single-pin across the WHOLE visual pool: built-in visual_type entries and
    // custom_visual_pattern entries share one lottery (director.cpp), so they
    // share one pin too -- first pin in iteration order wins, the rest are cleared.
    // Mirrors the enabled_theme pass above.
    bool builtin_pinned = false;
    for (auto& type : *program.mutable_visual_type()) {
      if (type.pinned()) {
        // A pin on NONE is not a pin: NONE is the enum's zero, Director stores the
        // pinned type in a uint32 where 0 means "nothing pinned", and no visual is
        // compiled for it. Honouring it would suppress the all-zero rescue below and
        // leave Director with an empty pool (and a null _visual to dereference).
        if (builtin_pinned || type.type() == trance_pb::Program_VisualType_NONE) {
          type.set_pinned(false);
        } else {
          builtin_pinned = true;
        }
      }
    }
    bool visual_pinned = builtin_pinned;
    for (auto& pattern : *program.mutable_custom_visual_pattern()) {
      if (pattern.pinned()) {
        // A disabled pattern is skipped by the runtime outright, so a pin on it is
        // dead weight the UI would draw as an active force -- clear it here so the
        // panel and Director agree.
        if (visual_pinned || !pattern.enabled()) {
          pattern.set_pinned(false);
        } else {
          visual_pinned = true;
        }
      }
    }

    // Deliberately counts the BUILT-INS ONLY, even though the runtime lottery also
    // draws from custom_visual_pattern (director.cpp). A custom pattern can drop out
    // of that lottery at any time -- rebuild_custom_patterns skips one that fails to
    // parse -- so custom weight is not evidence the program has anything playable,
    // and letting it suppress this rescue can leave Director with an empty pool (and
    // Director::update() dereferences a null _visual). Built-ins always compile, so
    // they are the only safe guarantor. A PINNED built-in is likewise a guarantee,
    // and its zero weight is intentional ("only this one"), so it suppresses the
    // rescue where a merely-weighted custom must not.
    count = 0;
    for (const auto& type : program.visual_type()) {
      count += type.random_weight();
    }
    if (!count && !builtin_pinned) {
      set_default_visual_types(program);
    }
    program.set_global_fps(std::max(1u, std::min(240u, program.global_fps())));
    program.set_zoom_intensity(std::max(0.f, std::min(1.f, program.zoom_intensity())));
    validate_colour(*program.mutable_spiral_colour_a());
    validate_colour(*program.mutable_spiral_colour_b());
    validate_colour(*program.mutable_main_text_colour());
    validate_colour(*program.mutable_shadow_text_colour());
  }

  void validate_playlist_item(trance_pb::PlaylistItem& playlist_item, trance_pb::Session& session)
  {
    if (!playlist_item.program().empty() || playlist_item.play_time_seconds()) {
      playlist_item.mutable_standard()->set_program(playlist_item.program());
      playlist_item.mutable_standard()->set_play_time_seconds(playlist_item.play_time_seconds());
      playlist_item.clear_program();
      playlist_item.clear_play_time_seconds();
    }
    if (playlist_item.has_standard()) {
      auto it = session.program_map().find(playlist_item.standard().program());
      if (it == session.program_map().end()) {
        set_default_program(session, playlist_item.standard().program());
      }
    }
    if (playlist_item.has_subroutine()) {
      auto& subroutine = *playlist_item.mutable_subroutine();
      for (auto it = subroutine.mutable_playlist_item_name()->begin();
           it != subroutine.mutable_playlist_item_name()->end();) {
        auto jt = session.playlist().find(*it);
        if (jt == session.playlist().end()) {
          it = subroutine.mutable_playlist_item_name()->erase(it);
        } else {
          ++it;
        }
      }
    }

    for (auto it = playlist_item.mutable_next_item()->begin();
         it != playlist_item.mutable_next_item()->end();) {
      if (it->random_weight() == 0 ||
          session.playlist().find(it->playlist_item_name()) == session.playlist().end()) {
        it = playlist_item.mutable_next_item()->erase(it);
      } else {
        ++it;
      }
    }

    for (auto& next_item : *playlist_item.mutable_next_item()) {
      auto variable_it = session.variable_map().find(next_item.condition_variable_name());
      if (variable_it == session.variable_map().end()) {
        next_item.clear_condition_variable_name();
        next_item.clear_condition_variable_value();
      } else {
        auto& data = variable_it->second.value();
        if (std::find(data.begin(), data.end(), next_item.condition_variable_value()) ==
            data.end()) {
          next_item.clear_condition_variable_name();
          next_item.clear_condition_variable_value();
        }
      }
    }
  }

  void validate_variable(trance_pb::Variable& variable)
  {
    if (!variable.value_size()) {
      variable.add_value("Default");
    }
    bool found_default = false;
    for (const auto& value : variable.value()) {
      if (value == variable.default_value()) {
        found_default = true;
      }
    }
    if (!found_default) {
      variable.set_default_value(variable.value(0));
    }
  }

  // JSON is the only on-disk format the player reads (docs/session-json-format.md sec
  // 8): "*.session.json" for sessions, "system.json" for system config. A legacy
  // ".session" textproto path passed to load_session is auto-converted (see
  // convert_legacy_session below); anything else non-JSON is rejected rather than
  // silently misread as JSON.
  bool has_json_extension(const std::string& path)
  {
    return ext_is(path, "json");
  }

  bool has_legacy_session_extension(const std::string& path)
  {
    return ext_is(path, "session");
  }

  std::string sibling_with_extension(const std::string& path, const std::string& extension)
  {
    return std::filesystem::path{path}.replace_extension(extension).string();
  }

  [[noreturn]] void fatal_non_json_path(const std::string& path)
  {
    throw std::runtime_error(path +
                             ": unrecognized extension -- the player only reads the JSON "
                             "formats described in docs/session-json-format.md (legacy "
                             ".session files are auto-converted when loaded)");
  }

  // Transparent legacy auto-convert: reads the legacy textproto `.session`, writes the
  // same-stem `.json` next to it (the original is left untouched), then reloads from
  // the written JSON. This mirrors main.cpp's cold-start migration for the same
  // reasons: playback must come from what was WRITTEN, not the in-memory legacy proto
  // (the JSON saver normalizes Windows backslash media paths to forward slashes, spec
  // sec 1), and the sidecar is reset before the reload because save_session fills it
  // with fresh pattern-file paths.
  trance_pb::Session convert_legacy_session(const std::string& legacy_path,
                                            const std::string& json_path,
                                            SessionJsonSidecar& sidecar)
  {
    trance_pb::Session session;
    try {
      session = load_legacy_session(legacy_path);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error("auto-convert of legacy " + legacy_path + " -> " + json_path +
                               " failed: " + e.what());
    }
    validate_session(session);
    try {
      save_session(session, json_path, sidecar);
    } catch (const std::runtime_error& save_error) {
      // Failed write (read-only directory, disk full -- save_session_json checks its
      // output stream and throws): still playable this run, just not persisted (the
      // same tolerance as main.cpp's cold-start migration). This early return is
      // load-bearing: falling through to the reload below with no .json on disk
      // would send load_session straight back to the same legacy sibling and recurse
      // here forever. The in-memory session is already validated; reset the sidecar
      // in case save_session partially filled it.
      std::cerr << "couldn't write " << json_path << ": " << save_error.what() << std::endl;
      sidecar = SessionJsonSidecar{};
      return session;
    }
    std::cout << "auto-converted " << legacy_path << " -> " << json_path << std::endl;
    sidecar = SessionJsonSidecar{};
    return load_session(json_path, sidecar);
  }

} // anonymous namespace

std::string make_relative(const std::string& from, const std::string& to)
{
  return std::filesystem::relative(to, from).string();
}

bool is_image(const std::string& path)
{
  return ext_is(path, "png") || ext_is(path, "bmp") || ext_is(path, "jpg") || ext_is(path, "jpeg");
}

bool is_animation(const std::string& path)
{
  // Should really check is_gif_animated(), but it takes far too long.
  return ext_is(path, "webm") || ext_is(path, "gif");
}

bool is_font(const std::string& path)
{
  return ext_is(path, "ttf");
}

bool is_text_file(const std::string& path)
{
  return ext_is(path, "txt");
}

bool is_audio_file(const std::string& path)
{
  return ext_is(path, "wav") || ext_is(path, "ogg") || ext_is(path, "flac") || ext_is(path, "aiff");
}

namespace
{
  bool is_scan_ignored(const std::filesystem::path& relative_path)
  {
    // "If it's in the folder, that's the content" (#36) -- classification below has no
    // allowlist, so the only files a scan drops are the ones that are obviously session
    // machinery rather than media. Without this, a session.json sitting next to its media
    // becomes an "image" that fails to decode once and is then dead weight in the theme.
    auto rel_str = relative_path.string();
    if (ext_is(rel_str, "json") || ext_is(rel_str, "session") || ext_is(rel_str, "pattern") ||
        ext_is(rel_str, "cfg") || ext_is(rel_str, "trance")) {
      return true;
    }
    // Hidden files and anything under a dotted directory (.git, .DS_Store): never content.
    for (const auto& component : relative_path) {
      const auto& s = component.string();
      if (s.length() > 1 && s[0] == '.') {
        return true;
      }
    }
    return false;
  }

  // Classifies a scanned file into the theme list it belongs in. The extension checks are
  // a dispatch, NOT a filter: anything that isn't a known animation/font/text/audio file
  // is treated as an image, because the decode layer already tolerates junk (a file that
  // won't decode is marked failed once and never retried -- theme_bank.cpp do_load).
  enum class ScanKind { image, animation, font, text, audio };

  ScanKind classify_scanned_file(const std::string& rel_str)
  {
    if (is_animation(rel_str)) {
      return ScanKind::animation;
    }
    if (is_font(rel_str)) {
      return ScanKind::font;
    }
    if (is_text_file(rel_str)) {
      return ScanKind::text;
    }
    if (is_audio_file(rel_str)) {
      return ScanKind::audio;
    }
    return ScanKind::image;
  }

  // Reads a scanned .txt into a theme's text lines, applying the same uppercasing and
  // mid-split treatment the session-level walker has always used.
  void add_text_lines_from_file(trance_pb::Theme& theme, const std::filesystem::path& path)
  {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (!line.length()) {
        continue;
      }
      for (auto& c : line) {
        c = char(toupper(static_cast<unsigned char>(c)));
      }
      theme.add_text_line(split_text_line(line));
    }
  }

  bool is_theme_empty(const trance_pb::Theme& theme)
  {
    return !theme.image_path_size() && !theme.animation_path_size() && !theme.font_path_size() &&
        !theme.text_line_size() && !theme.audio_path_size();
  }
} // anonymous namespace

bool is_enabled(const trance_pb::PlaylistItem_NextItem& next,
                const std::map<std::string, std::string>& variables)
{
  if (next.condition_variable_name().empty()) {
    return true;
  }
  std::string value;
  auto it = variables.find(next.condition_variable_name());
  if (it != variables.end()) {
    value = it->second;
  }
  return value == next.condition_variable_value();
};

void search_resources(trance_pb::Session& session, const std::string& root)
{
  std::map<std::string, std::string> ignored;
  search_resources(session, root, ignored);
}

void search_resources(trance_pb::Session& session, const std::string& root,
                      std::map<std::string, std::string>& theme_scan)
{
  static const std::string wildcards = "/wildcards/";
  auto& themes = *session.mutable_theme_map();

  std::filesystem::path root_path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(root_path);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (std::filesystem::is_regular_file(it->status())) {
      auto relative_path = std::filesystem::relative(it->path(), root_path);
      auto jt = relative_path.begin();
      if (jt == relative_path.end()) {
        continue;
      }
      if (is_scan_ignored(relative_path)) {
        continue;
      }
      auto theme_name = jt == --relative_path.end() ? wildcards : jt->string();

      auto rel_str = relative_path.string();
      auto& theme = themes[theme_name];
      switch (classify_scanned_file(rel_str)) {
      case ScanKind::animation:
        theme.add_animation_path(rel_str);
        break;
      case ScanKind::font:
        theme.add_font_path(rel_str);
        break;
      case ScanKind::text:
        add_text_lines_from_file(theme, it->path());
        break;
      case ScanKind::audio:
        theme.add_audio_path(rel_str);
        break;
      case ScanKind::image:
        theme.add_image_path(rel_str);
        break;
      }
    }
  }

  // #36: report which themes are pure folder references, so a caller writing this session
  // out (the cold-start bootstrap) can emit {"scan": <subdir>} instead of freezing the
  // expansion into explicit lists. Only valid while nothing has been merged in from
  // /wildcards/ -- once loose root files land in every theme, no single directory
  // reproduces a theme's content and the explicit lists are the only faithful record.
  const bool wildcards_empty = !themes.count(wildcards) || is_theme_empty(themes[wildcards]);

  // Merge wildcards theme into all others.
  for (auto& pair : themes) {
    if (pair.first == wildcards) {
      continue;
    }
    for (const auto& s : themes[wildcards].image_path()) {
      pair.second.add_image_path(s);
    }
    for (const auto& s : themes[wildcards].animation_path()) {
      pair.second.add_animation_path(s);
    }
    for (const auto& s : themes[wildcards].font_path()) {
      pair.second.add_font_path(s);
    }
    for (const auto& s : themes[wildcards].text_line()) {
      pair.second.add_text_line(s);
    }
    for (const auto& s : themes[wildcards].audio_path()) {
      pair.second.add_audio_path(s);
    }
  }

  // Leave wildcards theme if there are no others.
  themes.erase("default");
  if (themes.size() == 1) {
    themes["default"] = themes[wildcards];
  }
  themes.erase(wildcards);
  set_default_playlist(session, "default");
  auto& program = (*session.mutable_program_map())["default"];
  for (auto& pair : themes) {
    program.add_enabled_theme_name(pair.first);
    // The synthesized "default" theme is the wildcards content (loose root files), whose
    // scan directory would be the session root itself -- not a folder theme.
    if (wildcards_empty && pair.first != "default" &&
        std::filesystem::is_directory(root_path / pair.first)) {
      theme_scan[pair.first] = pair.first;
    }
  }
  session.set_first_playlist_item("default");
}

void search_resources(trance_pb::Theme& theme, const std::string& root)
{
  std::filesystem::path root_path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(root_path);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (std::filesystem::is_regular_file(it->status())) {
      auto relative_path = std::filesystem::relative(it->path(), root_path);
      auto jt = relative_path.begin();
      if (jt == relative_path.end()) {
        continue;
      }
      if (is_scan_ignored(relative_path)) {
        continue;
      }
      auto rel_str = relative_path.string();
      switch (classify_scanned_file(rel_str)) {
      case ScanKind::animation:
        theme.add_animation_path(rel_str);
        break;
      case ScanKind::font:
        theme.add_font_path(rel_str);
        break;
      case ScanKind::text:
        add_text_lines_from_file(theme, it->path());
        break;
      case ScanKind::audio:
        theme.add_audio_path(rel_str);
        break;
      case ScanKind::image:
        theme.add_image_path(rel_str);
        break;
      }
    }
  }
}

void search_audio_files(std::vector<std::string>& files, const std::string& root)
{
  std::filesystem::path root_path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(root_path);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (std::filesystem::is_regular_file(it->status())) {
      auto relative_path = std::filesystem::relative(it->path(), root_path);
      auto jt = relative_path.begin();
      if (jt == relative_path.end()) {
        continue;
      }
      auto rel_str = relative_path.string();
      if (is_audio_file(rel_str)) {
        files.push_back(rel_str);
      }
    }
  }
}

trance_pb::System load_system(const std::string& path)
{
  if (!has_json_extension(path)) {
    fatal_non_json_path(path);
  }
  auto system = load_system_json(path);
  validate_system(system);
  return system;
}

void save_system(const trance_pb::System& system, const std::string& path)
{
  save_system_json(system, path);
}

trance_pb::System get_default_system()
{
  trance_pb::System system;
  system.set_enable_vsync(true);
  system.set_renderer(trance_pb::System::MONITOR);
  system.mutable_draw_depth()->set_draw_depth(.5f);
  system.mutable_eye_spacing()->set_eye_spacing(1.f / 16);
  system.set_image_cache_size(64);
  system.set_animation_buffer_size(32);
  system.set_font_cache_size(8);

  auto& export_settings = *system.mutable_last_export_settings();
  export_settings.set_width(1280);
  export_settings.set_height(720);
  export_settings.set_fps(30);
  export_settings.set_length(60);
  export_settings.set_quality(2);
  export_settings.set_threads(4);

  return system;
}

void validate_system(trance_pb::System& system)
{
  if (!system.has_draw_depth()) {
    system.mutable_draw_depth()->set_draw_depth(.5f);
  }
  system.mutable_draw_depth()->set_draw_depth(
      std::max(0.f, std::min(1.f, system.draw_depth().draw_depth())));
  if (!system.has_eye_spacing()) {
    system.mutable_eye_spacing()->set_eye_spacing(1.f / 16);
  }
  system.mutable_eye_spacing()->set_eye_spacing(
      std::max(-1.f, std::min(1.f, system.eye_spacing().eye_spacing())));
  system.set_image_cache_size(std::max(16u, system.image_cache_size()));
  system.set_animation_buffer_size(std::max(8u, system.animation_buffer_size()));
  system.set_font_cache_size(std::max(2u, system.font_cache_size()));
}

trance_pb::Session load_session(const std::string& path, SessionJsonSidecar& sidecar)
{
  if (has_legacy_session_extension(path)) {
    auto json_path = sibling_with_extension(path, ".json");
    if (std::filesystem::exists(json_path)) {
      // A previous conversion (or hand-authored JSON) already sits next to the legacy
      // file -- prefer it; a conversion never overwrites an existing .json.
      std::cout << "using existing " << json_path << " for " << path << std::endl;
      return load_session(json_path, sidecar);
    }
    return convert_legacy_session(path, json_path, sidecar);
  }
  if (!has_json_extension(path)) {
    fatal_non_json_path(path);
  }
  if (!std::filesystem::exists(path)) {
    // A missing .json with a same-stem legacy .session sibling gets the same
    // auto-convert ("foo.json" -> "foo.session", "foo.session.json" -> "foo.session");
    // a missing .json with no sibling falls through to the ordinary missing-file error
    // from load_session_json.
    auto legacy_path = sibling_with_extension(path, "");
    if (!has_legacy_session_extension(legacy_path)) {
      legacy_path += ".session";
    }
    if (std::filesystem::exists(legacy_path)) {
      return convert_legacy_session(legacy_path, path, sidecar);
    }
  }
  auto root = std::filesystem::path{path}.parent_path().string();
  auto session = load_session_json(path, root, sidecar);
  validate_session(session);
  return session;
}

trance_pb::Session load_session(const std::string& path)
{
  SessionJsonSidecar sidecar;
  return load_session(path, sidecar);
}

void save_session(const trance_pb::Session& session, const std::string& path,
                   SessionJsonSidecar& sidecar)
{
  auto root = std::filesystem::path{path}.parent_path().string();
  save_session_json(session, path, root, sidecar);
}

void save_session(const trance_pb::Session& session, const std::string& path)
{
  SessionJsonSidecar sidecar;
  save_session(session, path, sidecar);
}

trance_pb::Session get_default_session()
{
  trance_pb::Session session;
  set_default_playlist(session, "default");
  set_default_program(session, "default");
  validate_session(session);
  return session;
}

void validate_session(trance_pb::Session& session)
{
  if (session.theme_map().empty()) {
    (*session.mutable_theme_map())["default"];
  }
  if (session.playlist().empty() && session.program_map().empty()) {
    set_default_playlist(session, "default");
    set_default_program(session, "default");
  }
  if (session.playlist().empty()) {
    set_default_playlist(session, session.program_map().begin()->first);
  }
  if (session.program_map().empty()) {
    set_default_program(session, "default");
  }
  for (auto& pair : *session.mutable_variable_map()) {
    validate_variable(pair.second);
  }
  for (auto& pair : *session.mutable_playlist()) {
    validate_playlist_item(pair.second, session);
  }
  for (auto& pair : *session.mutable_program_map()) {
    validate_program(pair.second, session);
  }
  auto it = session.playlist().find(session.first_playlist_item());
  if (it == session.playlist().end()) {
    session.set_first_playlist_item(session.playlist().begin()->first);
  }
}