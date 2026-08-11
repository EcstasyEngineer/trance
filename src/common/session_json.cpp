#include <common/session_json.h>
#include <common/session.h>
#include <common/util.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <nlohmann/json.hpp>
#pragma warning(pop)

namespace
{
  using json = nlohmann::json;

  // ---------------------------------------------------------------------
  // Path contract (spec sec 1): root-relative, forward-slash, no ".." segments,
  // not absolute.
  // ---------------------------------------------------------------------

  void check_relative_path(const std::string& path, const std::string& json_path)
  {
    if (path.empty()) {
      return;
    }
    std::filesystem::path p{path};
    if (p.is_absolute()) {
      throw std::runtime_error(json_path + ": path '" + path + "' must be root-relative, not absolute");
    }
    for (const auto& part : p) {
      if (part == "..") {
        throw std::runtime_error(json_path + ": path '" + path + "' must not contain '..' segments");
      }
    }
  }

  std::string normalize_path_for_save(const std::string& path)
  {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
  }

  // ---------------------------------------------------------------------
  // Colours: "#RRGGBB" or "#RRGGBBAA" <-> trance_pb::Colour floats.
  // ---------------------------------------------------------------------

  uint8_t hex_byte(const std::string& s, std::size_t pos, const std::string& json_path)
  {
    auto hex_digit = [&](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      throw std::runtime_error(json_path + ": invalid hex colour '" + s + "'");
    };
    return uint8_t((hex_digit(s[pos]) << 4) | hex_digit(s[pos + 1]));
  }

  trance_pb::Colour parse_colour(const std::string& s, const std::string& json_path)
  {
    if (s.empty() || s[0] != '#' || (s.length() != 7 && s.length() != 9)) {
      throw std::runtime_error(json_path +
                                ": colour must be \"#RRGGBB\" or \"#RRGGBBAA\", got '" + s + "'");
    }
    uint8_t r = hex_byte(s, 1, json_path);
    uint8_t g = hex_byte(s, 3, json_path);
    uint8_t b = hex_byte(s, 5, json_path);
    uint8_t a = s.length() == 9 ? hex_byte(s, 7, json_path) : 0xFF;

    trance_pb::Colour colour;
    colour.set_r(r / 255.f);
    colour.set_g(g / 255.f);
    colour.set_b(b / 255.f);
    colour.set_a(a / 255.f);
    return colour;
  }

  std::string colour_byte_to_hex(float f)
  {
    int v = int(std::round(std::max(0.f, std::min(1.f, f)) * 255.f));
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02X", v);
    return buf;
  }

  std::string save_colour(const trance_pb::Colour& c)
  {
    std::string out = "#" + colour_byte_to_hex(c.r()) + colour_byte_to_hex(c.g()) +
        colour_byte_to_hex(c.b());
    if (c.a() < 1.f - .5f / 255.f) {
      out += colour_byte_to_hex(c.a());
    }
    return out;
  }

  bool colour_is_default(const trance_pb::Colour& c)
  {
    return c.r() == 0.f && c.g() == 0.f && c.b() == 0.f && c.a() == 0.f;
  }

  // ---------------------------------------------------------------------
  // Enums (spec sec 2.2): lowercase strings of the proto enum value name, no ints.
  // ---------------------------------------------------------------------

  const std::vector<std::pair<std::string, trance_pb::Program_VisualType>>& visual_type_table()
  {
    static const std::vector<std::pair<std::string, trance_pb::Program_VisualType>> table = {
        {"accelerate", trance_pb::Program_VisualType_ACCELERATE},
        {"slow_flash", trance_pb::Program_VisualType_SLOW_FLASH},
        {"sub_text", trance_pb::Program_VisualType_SUB_TEXT},
        {"flash_text", trance_pb::Program_VisualType_FLASH_TEXT},
        {"parallel", trance_pb::Program_VisualType_PARALLEL},
        {"super_parallel", trance_pb::Program_VisualType_SUPER_PARALLEL},
        {"animation", trance_pb::Program_VisualType_ANIMATION},
        {"super_fast", trance_pb::Program_VisualType_SUPER_FAST},
    };
    return table;
  }

  trance_pb::Program_VisualType parse_visual_type(const std::string& s, const std::string& json_path)
  {
    for (const auto& pair : visual_type_table()) {
      if (pair.first == s) {
        return pair.second;
      }
    }
    throw std::runtime_error(json_path + ": unknown visual_type '" + s + "'");
  }

  std::string save_visual_type(trance_pb::Program_VisualType type)
  {
    for (const auto& pair : visual_type_table()) {
      if (pair.second == type) {
        return pair.first;
      }
    }
    return "accelerate";
  }

  trance_pb::AudioEvent_Type parse_audio_event_type(const std::string& s, const std::string& json_path)
  {
    if (s == "play") return trance_pb::AudioEvent_Type_AUDIO_PLAY;
    if (s == "stop") return trance_pb::AudioEvent_Type_AUDIO_STOP;
    if (s == "fade") return trance_pb::AudioEvent_Type_AUDIO_FADE;
    throw std::runtime_error(json_path + ": unknown audio_event type '" + s + "'");
  }

  std::string save_audio_event_type(trance_pb::AudioEvent_Type t)
  {
    switch (t) {
    case trance_pb::AudioEvent_Type_AUDIO_PLAY: return "play";
    case trance_pb::AudioEvent_Type_AUDIO_STOP: return "stop";
    case trance_pb::AudioEvent_Type_AUDIO_FADE: return "fade";
    default: return "";
    }
  }

  // ---------------------------------------------------------------------
  // Common encoding rules (spec sec 2): format wrapper, comment keys, strict
  // unknown-key checking.
  // ---------------------------------------------------------------------

  bool is_comment_key(const std::string& key)
  {
    return !key.empty() && key[0] == '_';
  }

  // Throws on the first key of `obj` that isn't in `allowed` and isn't a `_` comment key.
  void check_unknown_keys(const json& obj, const std::vector<std::string>& allowed,
                           const std::string& json_path)
  {
    if (!obj.is_object()) {
      throw std::runtime_error(json_path + ": expected an object");
    }
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (is_comment_key(it.key())) {
        continue;
      }
      if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end()) {
        throw std::runtime_error(json_path + "/" + it.key() + ": unknown key");
      }
    }
  }

  const json* find(const json& obj, const std::string& key)
  {
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
  }

  std::string get_string(const json& obj, const std::string& key, const std::string& json_path)
  {
    const json* v = find(obj, key);
    if (!v) return {};
    if (!v->is_string()) {
      throw std::runtime_error(json_path + "/" + key + ": expected a string");
    }
    return v->get<std::string>();
  }

  uint32_t get_uint(const json& obj, const std::string& key, const std::string& json_path)
  {
    const json* v = find(obj, key);
    if (!v) return 0;
    if (!v->is_number_unsigned() && !(v->is_number_integer() && v->get<int64_t>() >= 0)) {
      throw std::runtime_error(json_path + "/" + key + ": expected a non-negative integer");
    }
    return v->get<uint32_t>();
  }

  bool get_bool(const json& obj, const std::string& key, const std::string& json_path)
  {
    const json* v = find(obj, key);
    if (!v) return false;
    if (!v->is_boolean()) {
      throw std::runtime_error(json_path + "/" + key + ": expected a bool");
    }
    return v->get<bool>();
  }

  float get_float(const json& obj, const std::string& key, const std::string& json_path)
  {
    const json* v = find(obj, key);
    if (!v) return 0.f;
    if (!v->is_number()) {
      throw std::runtime_error(json_path + "/" + key + ": expected a number");
    }
    return v->get<float>();
  }

  void require_format(const json& root, const std::string& expected_discriminator)
  {
    auto format = get_string(root, "format", "");
    if (format != expected_discriminator) {
      throw std::runtime_error("/format: expected \"" + expected_discriminator + "\", got \"" +
                                format + "\"");
    }
    const json* version = find(root, "format_version");
    if (!version || !version->is_number_integer()) {
      throw std::runtime_error("/format_version: expected an integer");
    }
    auto v = version->get<int64_t>();
    if (v > 1) {
      throw std::runtime_error("/format_version: version " + std::to_string(v) +
                                " is newer than supported version 1");
    }
  }

  // ---------------------------------------------------------------------
  // Slug rule (spec sec 5): lowercase name mapped to [a-z0-9_-], other chars -> '_',
  // collisions suffixed -2, -3, ...
  // ---------------------------------------------------------------------

  std::string slugify(const std::string& name)
  {
    std::string slug;
    slug.reserve(name.size());
    for (char c : name) {
      char lc = char(std::tolower(uint8_t(c)));
      if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') || lc == '_' || lc == '-') {
        slug += lc;
      } else {
        slug += '_';
      }
    }
    if (slug.empty()) {
      slug = "pattern";
    }
    return slug;
  }

  // ---------------------------------------------------------------------
  // Session JSON -> trance_pb::Session
  // ---------------------------------------------------------------------

  void load_playlist_item(const json& obj, trance_pb::PlaylistItem& item, const std::string& json_path)
  {
    check_unknown_keys(obj, {"standard", "subroutine", "next_item", "audio_event"}, json_path);

    const json* standard = find(obj, "standard");
    const json* subroutine = find(obj, "subroutine");
    if ((standard != nullptr) == (subroutine != nullptr)) {
      throw std::runtime_error(json_path +
                                ": exactly one of 'standard' or 'subroutine' must be present");
    }

    if (standard) {
      auto sp = json_path + "/standard";
      check_unknown_keys(*standard, {"program", "play_time_seconds"}, sp);
      auto* s = item.mutable_standard();
      s->set_program(get_string(*standard, "program", sp));
      s->set_play_time_seconds(get_uint(*standard, "play_time_seconds", sp));
    } else {
      auto sp = json_path + "/subroutine";
      if (!subroutine->is_array()) {
        throw std::runtime_error(sp + ": expected an array");
      }
      auto* sub = item.mutable_subroutine();
      for (const auto& entry : *subroutine) {
        if (!entry.is_string()) {
          throw std::runtime_error(sp + ": expected an array of strings");
        }
        sub->add_playlist_item_name(entry.get<std::string>());
      }
    }

    if (const json* next_items = find(obj, "next_item")) {
      auto np = json_path + "/next_item";
      if (!next_items->is_array()) {
        throw std::runtime_error(np + ": expected an array");
      }
      std::size_t idx = 0;
      for (const auto& entry : *next_items) {
        auto ep = np + "[" + std::to_string(idx++) + "]";
        check_unknown_keys(entry, {"playlist_item_name", "random_weight",
                                    "condition_variable_name", "condition_variable_value"},
                            ep);
        auto* next = item.add_next_item();
        next->set_playlist_item_name(get_string(entry, "playlist_item_name", ep));
        next->set_random_weight(get_uint(entry, "random_weight", ep));
        const json* cname = find(entry, "condition_variable_name");
        const json* cvalue = find(entry, "condition_variable_value");
        if ((cname != nullptr) != (cvalue != nullptr)) {
          throw std::runtime_error(ep + ": condition_variable_name and "
                                         "condition_variable_value must both be present or both absent");
        }
        if (cname) {
          next->set_condition_variable_name(get_string(entry, "condition_variable_name", ep));
          next->set_condition_variable_value(get_string(entry, "condition_variable_value", ep));
        }
      }
    }

    if (const json* audio_events = find(obj, "audio_event")) {
      auto ap = json_path + "/audio_event";
      if (!audio_events->is_array()) {
        throw std::runtime_error(ap + ": expected an array");
      }
      std::size_t idx = 0;
      for (const auto& entry : *audio_events) {
        auto ep = ap + "[" + std::to_string(idx++) + "]";
        check_unknown_keys(entry, {"type", "channel", "next_unused_channel", "path", "loop",
                                    "volume", "time_seconds"},
                            ep);
        auto* ev = item.add_audio_event();
        ev->set_type(parse_audio_event_type(get_string(entry, "type", ep), ep));
        ev->set_channel(get_uint(entry, "channel", ep));
        ev->set_next_unused_channel(get_bool(entry, "next_unused_channel", ep));
        auto path = get_string(entry, "path", ep);
        check_relative_path(path, ep + "/path");
        ev->set_path(path);
        ev->set_loop(get_bool(entry, "loop", ep));
        ev->set_volume(get_uint(entry, "volume", ep));
        ev->set_time_seconds(get_uint(entry, "time_seconds", ep));
      }
    }
  }

  void load_program(const json& obj, trance_pb::Program& program, const std::string& program_name,
                     const std::string& json_path, const std::string& root,
                     SessionJsonSidecar& sidecar)
  {
    check_unknown_keys(obj,
                        {"enabled_theme", "visual_type", "custom_visual_pattern", "global_fps",
                         "zoom_intensity", "spiral_colour_a", "spiral_colour_b",
                         "reverse_spiral_direction", "main_text_colour", "shadow_text_colour",
                         "entrainment"},
                        json_path);

    if (const json* themes = find(obj, "enabled_theme")) {
      auto tp = json_path + "/enabled_theme";
      if (!themes->is_array()) {
        throw std::runtime_error(tp + ": expected an array");
      }
      std::size_t idx = 0;
      for (const auto& entry : *themes) {
        auto ep = tp + "[" + std::to_string(idx++) + "]";
        check_unknown_keys(entry, {"theme_name", "random_weight", "pinned"}, ep);
        auto* t = program.add_enabled_theme();
        t->set_theme_name(get_string(entry, "theme_name", ep));
        t->set_random_weight(get_uint(entry, "random_weight", ep));
        t->set_pinned(get_bool(entry, "pinned", ep));
      }
    }

    if (const json* types = find(obj, "visual_type")) {
      auto tp = json_path + "/visual_type";
      if (!types->is_array()) {
        throw std::runtime_error(tp + ": expected an array");
      }
      std::size_t idx = 0;
      for (const auto& entry : *types) {
        auto ep = tp + "[" + std::to_string(idx++) + "]";
        check_unknown_keys(entry, {"type", "random_weight", "pinned"}, ep);
        auto* vt = program.add_visual_type();
        vt->set_type(parse_visual_type(get_string(entry, "type", ep), ep));
        vt->set_random_weight(get_uint(entry, "random_weight", ep));
        vt->set_pinned(get_bool(entry, "pinned", ep));
      }
    }

    if (const json* patterns = find(obj, "custom_visual_pattern")) {
      auto pp = json_path + "/custom_visual_pattern";
      if (!patterns->is_array()) {
        throw std::runtime_error(pp + ": expected an array");
      }
      std::vector<std::string> seen_names;
      std::size_t idx = 0;
      for (const auto& entry : *patterns) {
        auto ep = pp + "[" + std::to_string(idx++) + "]";
        check_unknown_keys(entry, {"name", "file", "random_weight", "enabled", "pinned"}, ep);
        auto name = get_string(entry, "name", ep);
        if (std::find(seen_names.begin(), seen_names.end(), name) != seen_names.end()) {
          throw std::runtime_error(ep + "/name: duplicate custom_visual_pattern name '" + name +
                                    "' in program '" + program_name + "'");
        }
        seen_names.push_back(name);

        auto file = get_string(entry, "file", ep);
        check_relative_path(file, ep + "/file");

        auto full_path = (std::filesystem::path(root) / file).string();
        std::ifstream f{full_path, std::ios::binary};
        if (!f) {
          throw std::runtime_error(ep + "/file: pattern file '" + file + "' is missing or unreadable");
        }
        std::string source_text{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};

        auto* src = program.add_custom_visual_pattern();
        src->set_name(name);
        src->set_source_text(source_text);
        src->set_random_weight(get_uint(entry, "random_weight", ep));
        src->set_enabled(get_bool(entry, "enabled", ep));
        src->set_pinned(get_bool(entry, "pinned", ep));

        sidecar.pattern_file[{program_name, name}] = file;
      }
    }

    if (find(obj, "global_fps")) {
      program.set_global_fps(get_uint(obj, "global_fps", json_path));
    }
    if (find(obj, "zoom_intensity")) {
      program.set_zoom_intensity(get_float(obj, "zoom_intensity", json_path));
    }
    if (const json* v = find(obj, "spiral_colour_a")) {
      *program.mutable_spiral_colour_a() =
          parse_colour(v->get<std::string>(), json_path + "/spiral_colour_a");
    }
    if (const json* v = find(obj, "spiral_colour_b")) {
      *program.mutable_spiral_colour_b() =
          parse_colour(v->get<std::string>(), json_path + "/spiral_colour_b");
    }
    program.set_reverse_spiral_direction(get_bool(obj, "reverse_spiral_direction", json_path));
    if (const json* v = find(obj, "main_text_colour")) {
      *program.mutable_main_text_colour() =
          parse_colour(v->get<std::string>(), json_path + "/main_text_colour");
    }
    if (const json* v = find(obj, "shadow_text_colour")) {
      *program.mutable_shadow_text_colour() =
          parse_colour(v->get<std::string>(), json_path + "/shadow_text_colour");
    }

    if (const json* entrainment = find(obj, "entrainment")) {
      auto np = json_path + "/entrainment";
      check_unknown_keys(*entrainment, {"master_db", "layer"}, np);
      auto* e = program.mutable_entrainment();
      e->set_master_db(get_float(*entrainment, "master_db", np));
      if (const json* layers = find(*entrainment, "layer")) {
        auto lp = np + "/layer";
        if (!layers->is_array()) {
          throw std::runtime_error(lp + ": expected an array");
        }
        std::size_t idx = 0;
        for (const auto& entry : *layers) {
          auto ep = lp + "[" + std::to_string(idx++) + "]";
          check_unknown_keys(entry, {"center_hz", "binaural_hz", "pulse_hz", "amplitude_db"}, ep);
          auto* layer = e->add_layer();
          layer->set_center_hz(get_float(entry, "center_hz", ep));
          layer->set_binaural_hz(get_float(entry, "binaural_hz", ep));
          layer->set_pulse_hz(get_float(entry, "pulse_hz", ep));
          layer->set_amplitude_db(get_float(entry, "amplitude_db", ep));
        }
      }
      // absent 'entrainment' key = no bed; presence with no 'layer' key = layer-less bed
      // (still counts as "has_entrainment" via the mutable_entrainment() call above).
    }
  }

  // Defined below load_theme (it is the larger routine); declared here because load_theme
  // is its first caller.
  void expand_scan_theme(trance_pb::Theme& theme, const std::string& theme_name,
                          const std::string& dir, const std::string& root,
                          SessionJsonSidecar& sidecar);

  void load_theme(const json& obj, trance_pb::Theme& theme, const std::string& theme_name,
                   const std::string& json_path, const std::string& root,
                   SessionJsonSidecar& sidecar)
  {
    check_unknown_keys(
        obj, {"scan", "image_path", "animation_path", "font_path", "text_line", "audio_path"},
        json_path);

    if (const json* image_path = find(obj, "image_path")) {
      auto ip = json_path + "/image_path";
      if (!image_path->is_array()) {
        throw std::runtime_error(ip + ": expected an array");
      }
      for (const auto& entry : *image_path) {
        auto p = entry.get<std::string>();
        check_relative_path(p, ip);
        theme.add_image_path(p);
      }
    }
    if (const json* animation_path = find(obj, "animation_path")) {
      auto ap = json_path + "/animation_path";
      if (!animation_path->is_array()) {
        throw std::runtime_error(ap + ": expected an array");
      }
      for (const auto& entry : *animation_path) {
        auto p = entry.get<std::string>();
        check_relative_path(p, ap);
        theme.add_animation_path(p);
      }
    }
    if (const json* font_path = find(obj, "font_path")) {
      auto fp = json_path + "/font_path";
      if (!font_path->is_array()) {
        throw std::runtime_error(fp + ": expected an array");
      }
      for (const auto& entry : *font_path) {
        auto p = entry.get<std::string>();
        check_relative_path(p, fp);
        theme.add_font_path(p);
      }
    }
    if (const json* text_line = find(obj, "text_line")) {
      auto lp = json_path + "/text_line";
      if (!text_line->is_array()) {
        throw std::runtime_error(lp + ": expected an array");
      }
      for (const auto& entry : *text_line) {
        theme.add_text_line(entry.get<std::string>());
      }
    }
    if (const json* audio_path = find(obj, "audio_path")) {
      auto ap = json_path + "/audio_path";
      if (!audio_path->is_array()) {
        throw std::runtime_error(ap + ": expected an array");
      }
      for (const auto& entry : *audio_path) {
        auto p = entry.get<std::string>();
        check_relative_path(p, ap);
        theme.add_audio_path(p);
      }
    }

    if (const json* scan = find(obj, "scan")) {
      auto sp = json_path + "/scan";
      // Two forms, ONE model. A STRING is shorthand for {"dir": <string>}; the OBJECT form
      // adds the per-theme `inherit` flag and `exclude` list. Both walk that directory's
      // OWN files and nothing else.
      //
      // The subtree walk the string form performed before the folder hierarchy landed is
      // gone, and is not coming back: a subdirectory is a theme in its own right now, so
      // recursing would put every nested file in two pools at once and hand a parent its
      // children's weight. What a pre-hierarchy session sees instead is its nested content
      // REDISTRIBUTED -- the scan root's discovery pass turns each subdirectory into a
      // theme of its own at weight 1 -- and `inherit` is how you fold it back together
      // where the old shape was actually wanted. A directory that holds nothing but
      // subdirectories therefore expands to an empty theme, which ThemeBank::set_program
      // keeps OUT of the rotation rather than giving it frames it can only draw black.
      std::string dir;
      if (scan->is_string()) {
        dir = scan->get<std::string>();
      } else if (scan->is_object()) {
        check_unknown_keys(*scan, {"dir", "inherit", "exclude"}, sp);
        const json* dir_value = find(*scan, "dir");
        if (!dir_value || !dir_value->is_string()) {
          throw std::runtime_error(sp + "/dir: expected a string");
        }
        dir = dir_value->get<std::string>();
        if (const json* inherit = find(*scan, "inherit")) {
          if (!inherit->is_boolean()) {
            throw std::runtime_error(sp + "/inherit: expected a boolean");
          }
          if (inherit->get<bool>()) {
            sidecar.theme_inherit.insert(theme_name);
          }
        }
        if (const json* exclude = find(*scan, "exclude")) {
          auto ep = sp + "/exclude";
          if (!exclude->is_array()) {
            throw std::runtime_error(ep + ": expected an array");
          }
          auto& list = sidecar.theme_exclude[theme_name];
          for (const auto& entry : *exclude) {
            if (!entry.is_string()) {
              throw std::runtime_error(ep + ": expected an array of strings");
            }
            auto p = entry.get<std::string>();
            check_relative_path(p, ep);
            // Normalized to the SAME form the rebased scan results take, because the
            // match is an exact string compare -- a hand-written backslash path would
            // otherwise parse fine, match nothing, and silently fail to exclude.
            list.push_back(normalize_path_for_save(p));
          }
        }
      } else {
        throw std::runtime_error(sp + ": expected a string or an object");
      }
      check_relative_path(dir, sp);
      expand_scan_theme(theme, theme_name, dir, root, sidecar);
    }
  }

  // Expands one scan theme in place: records the scan in the sidecar, walks the directory,
  // rebases results onto the session root, and drops the theme's exclusions. Exclusions are
  // never pruned here -- see the note at the end of the walk. Factored out of load_theme so
  // the auto-rescan pass (discover_new_themes) can materialize a newly-appeared folder
  // exactly the way a hand-written `scan` entry would be loaded -- one rule, not two.
  void expand_scan_theme(trance_pb::Theme& theme, const std::string& theme_name,
                          const std::string& dir, const std::string& root,
                          SessionJsonSidecar& sidecar)
  {
    sidecar.theme_scan[theme_name] = dir;
    auto full_dir = (std::filesystem::path(root) / dir).string();
    if (std::filesystem::exists(full_dir)) {
      {
        // search_resources() yields paths relative to the directory it walked, but every
        // consumer of theme.image_path() (ThemeBank's loader, the archive's file walk)
        // resolves against the SESSION ROOT per the sec 1 path contract. Scan into a
        // scratch theme and rebase each result onto `dir` rather than letting scan-dir-
        // relative paths reach trance_pb, where they'd resolve to nothing.
        trance_pb::Theme scanned;
        search_resources(scanned, full_dir);
        auto rebase = [&dir](const std::string& p) {
          // A scan of the session root itself -- `"dir": "."`, which is what the /root/
          // theme uses -- must NOT prefix "./" onto every result. The path contract (§1)
          // wants plain root-relative paths, every other theme produces them, and a
          // "./x.jpg" would silently fail to match an exclusion written as "x.jpg".
          if (dir.empty() || dir == ".") {
            return normalize_path_for_save(p);
          }
          return normalize_path_for_save((std::filesystem::path(dir) / p).string());
        };
        // Inverted persistence: the theme keeps everything the scan found EXCEPT the
        // recorded exclusions, so a file added to the folder later needs no edit to be
        // picked up. Compared against the rebased (session-root-relative) path, which is
        // the form the exclusion list is written in.
        const auto exclude_it = sidecar.theme_exclude.find(theme_name);
        const std::vector<std::string> empty_excludes;
        const auto& excludes =
            exclude_it != sidecar.theme_exclude.end() ? exclude_it->second : empty_excludes;
        auto excluded = [&excludes](const std::string& p) {
          return std::find(excludes.begin(), excludes.end(), p) != excludes.end();
        };
        auto add_unless_excluded = [&](const std::string& p, void (trance_pb::Theme::*add)(
                                                                 const std::string&)) {
          auto rebased = rebase(p);
          if (!excluded(rebased)) {
            (theme.*add)(rebased);
          }
        };
        for (const auto& p : scanned.image_path()) {
          add_unless_excluded(p, &trance_pb::Theme::add_image_path);
        }
        for (const auto& p : scanned.animation_path()) {
          add_unless_excluded(p, &trance_pb::Theme::add_animation_path);
        }
        for (const auto& p : scanned.font_path()) {
          add_unless_excluded(p, &trance_pb::Theme::add_font_path);
        }
        for (const auto& p : scanned.audio_path()) {
          add_unless_excluded(p, &trance_pb::Theme::add_audio_path);
        }
        // Text lines are content, not paths -- nothing to rebase or exclude.
        for (const auto& t : scanned.text_line()) {
          theme.add_text_line(t);
        }

        // DELIBERATELY no garbage-collection of exclusions that name nothing this scan
        // produced -- the same call the removal pass in discover_new_themes makes, for the
        // same reason.
        //
        // "The file was deleted" and "the file has not materialized yet" are one
        // observation from here: a name the walk did not yield. Cloud-synced and network
        // folders produce the second constantly. The all-or-nothing guard this replaces
        // (prune unless the scan found NOTHING) passes a OneDrive folder holding 3 of its
        // 200 files and then erases every exclusion naming the other 197; the next save
        // writes that out and the user's blacklist is gone with no way back.
        //
        // The cost of not pruning is bounded and cheap: one short string per file the user
        // ever excluded, matched by an exact string compare, and re-checking the box in
        // F2 removes it. Untidy beats unrecoverable.
      }
    }
  }

  // The directory a theme's name refers to, session-root-relative. Theme names are
  // SCAN-ROOT-relative (that is what the scrape produces), and kRootThemeName means the
  // scan root itself -- so the directory is the name rebased onto the scan root. One
  // function because migration and discovery disagreeing about it means a theme is
  // expanded from one directory and saved with another.
  std::string theme_dir_for(const std::string& theme_name, const std::string& scan_root)
  {
    const std::string relative = theme_name == kRootThemeName ? std::string{"."} : theme_name;
    if (scan_root.empty() || scan_root == ".") {
      return relative;
    }
    if (relative == ".") {
      return normalize_path_for_save(scan_root);
    }
    return normalize_path_for_save((std::filesystem::path(scan_root) / relative).string());
  }

  // Turns a legacy frozen theme -- an explicit media list with no `scan` -- into a folder
  // theme, in place, at load. There is exactly ONE theme model now (a directory plus a
  // blacklist), so this is a migration, not a second mode: it is what makes "media added
  // to a folder shows up next launch" true for sessions written before that was.
  //
  // Adopts the folder WHOLESALE -- the frozen list is discarded, not preserved as
  // exclusions. Preserving it was the obvious-looking choice and it is wrong: a file
  // sitting on disk but missing from the list is far more often one added since the
  // session was written than one deliberately omitted, and turning it into a permanent
  // exclusion reproduces the exact bug this whole change set exists to kill. Deliberate
  // omission has a real representation now (`exclude`), and a legacy frozen list has
  // none, so there is nothing to carry across.
  //
  // A theme with no directory of that name is left completely alone: it is a hand-written
  // list spanning unrelated folders, which no directory can reproduce.
  void migrate_frozen_themes(trance_pb::Session& session, const std::string& root,
                              SessionJsonSidecar& sidecar)
  {
    auto& themes = *session.mutable_theme_map();
    for (auto& pair : themes) {
      if (sidecar.theme_scan.count(pair.first)) {
        continue;  // already a folder theme
      }
      const auto dir = theme_dir_for(pair.first, sidecar.theme_scan_root);
      if (!std::filesystem::is_directory(std::filesystem::path(root) / dir)) {
        continue;
      }
      // Expand into a scratch theme and adopt the folder only if it actually produced
      // something. is_directory() alone is not enough, and the failure is permanent: a
      // pure container, a folder whose every file is denylisted junk, or one on a drive
      // that is mounted but not yet synced would clear the curated list, gain a scan
      // sidecar entry, and be written back on the next save as {"scan": ...} -- deleting
      // the only copy of that list from disk. An empty expansion is "no information", the
      // same reading the never-remove-a-missing-theme rule takes.
      trance_pb::Theme expanded;
      expand_scan_theme(expanded, pair.first, dir, root, sidecar);
      if (!expanded.image_path_size() && !expanded.animation_path_size() &&
          !expanded.font_path_size() && !expanded.text_line_size() &&
          !expanded.audio_path_size()) {
        // expand_scan_theme records the scan up front; take it back so the theme stays a
        // frozen list and still round-trips as its explicit media entries.
        sidecar.theme_scan.erase(pair.first);
        continue;
      }
      pair.second = std::move(expanded);
    }
  }

  // Re-derives the theme SET from the media tree at load, adding any directory that has
  // appeared since the session was written and dropping scan themes whose directory is
  // gone. This is the theme-level half of the inverted-persistence rule: the session
  // records what to leave OUT, so content that shows up on disk is IN by default. Without
  // it, `scan` keeps existing themes fresh but a brand-new folder stays invisible until
  // someone regenerates the session by hand -- which is the behaviour this whole change
  // set exists to kill.
  //
  // Only ever ADDS themes and removes provably-dead ones. A theme the user wrote by hand
  // (explicit media lists, no scan entry) is never touched, and an existing scan theme
  // keeps whatever weight/pin/inherit/exclude settings it already had, because all of
  // those are keyed by theme NAME and survive the rescan.
  void discover_new_themes(trance_pb::Session& session, const std::string& root,
                            SessionJsonSidecar& sidecar)
  {
    const auto scan_root = (std::filesystem::path(root) / sidecar.theme_scan_root).string();
    if (!std::filesystem::exists(scan_root)) {
      return;
    }
    // Reuse the cold-start scrape verbatim so discovery and bootstrap can never disagree
    // about which directories are themes. The scratch session is thrown away; only the
    // theme NAMES and their directories are wanted.
    trance_pb::Session scratch;
    std::map<std::string, std::string> discovered;
    search_resources(scratch, scan_root, discovered);

    auto& themes = *session.mutable_theme_map();
    for (const auto& pair : discovered) {
      if (themes.count(pair.first)) {
        continue;  // already known -- its own scan entry keeps it current
      }
      // A directory that appeared since the last save. Materialize it the same way a
      // hand-written scan theme loads, and enable it at the default weight in every
      // program: a folder the user has is content the user wants until they say
      // otherwise. The directory comes from theme_dir_for, the same helper migration
      // uses, so the two can't expand the same theme from different places.
      expand_scan_theme(themes[pair.first], pair.first, theme_dir_for(pair.first,
                                                                     sidecar.theme_scan_root),
                        root, sidecar);
      for (auto& program : *session.mutable_program_map()) {
        // Only if the program has no row for this name already. A theme_map entry can be
        // absent while an enabled_theme row survives -- a hand-edited file, or a folder
        // that went missing and came back -- and blindly appending would leave two rows
        // for one theme whose weights SUM, silently doubling its share of the lottery.
        bool listed = false;
        for (const auto& existing : program.second.enabled_theme()) {
          if (existing.theme_name() == pair.first) {
            listed = true;
            break;
          }
        }
        if (listed) {
          continue;
        }
        auto* enabled = program.second.add_enabled_theme();
        enabled->set_theme_name(pair.first);
        enabled->set_random_weight(1);
      }
    }

    // DELIBERATELY no removal pass for themes whose directory has gone missing.
    //
    // "The folder was deleted" and "the folder is not mounted right now" are the same
    // observation from here -- a failed exists() -- and the two want opposite handling. A
    // removable or network drive that is merely absent, plus one save while it is absent,
    // would erase that theme along with the weight, pin, inherit flag and exclusion list
    // the user built up; when the drive came back it would reappear as a brand-new theme
    // at weight 1 with all of that silently gone. Unrecoverable, and triggered by nothing
    // more deliberate than launching the app on a laptop.
    //
    // The cost of the other choice is merely untidy: a genuinely deleted folder leaves an
    // empty theme behind (it expands to nothing, so it draws nothing) until the user
    // removes it from the F2 Themes section. Untidy beats unrecoverable.
  }

  // Folds each inheriting theme's PARENT pool into it, after every theme has loaded.
  //
  // The rule composes transitively through the chain of flags, which is the whole point:
  // `hypno/spam` inherits `hypno`'s ALREADY-RESOLVED pool, so it reaches the root's loose
  // files only if `hypno` is inheriting too. Turning every flag on is the old
  // merge-everything-into-everything behaviour; turning them all off makes each theme
  // exactly its own directory; every mixture in between is reachable one checkbox at a
  // time. Crucially the SET of themes never changes -- only pool composition -- so
  // toggling can't strand a saved per-theme weight.
  //
  // A theme's parent is the theme named by its parent directory (kRootThemeName once the
  // path runs out). Missing parents are simply skipped: a pure container directory has no
  // theme of its own, and inheritance walks past it to whatever the chain resolves to.
  // Termination is guaranteed because each step strictly shortens the path, so no
  // cycle-guard is needed; `resolved` exists only to keep it linear rather than
  // exponential on deep trees.
  void resolve_theme_inheritance(trance_pb::Session& session, SessionJsonSidecar& sidecar)
  {
    auto& themes = *session.mutable_theme_map();
    // Record every theme's own size first: the UI reports "own -> effective" and the
    // union below destroys the evidence.
    for (const auto& pair : themes) {
      // The pool the rotation actually draws visuals from: images + animations. Fonts
      // and text lines are not pool size in any sense the user is weighing.
      sidecar.theme_own_count[pair.first] =
          static_cast<uint32_t>(pair.second.image_path_size() + pair.second.animation_path_size());
      // Tier 0: the theme's own images, before anything is folded in. Every theme gets
      // one even with inheritance off, so the runtime has a uniform description of every
      // pool rather than two shapes to branch on.
      sidecar.theme_tiers[pair.first] = {
          {pair.first, static_cast<uint32_t>(pair.second.image_path_size())}};
    }
    if (sidecar.theme_inherit.empty()) {
      return;
    }

    auto parent_of = [](const std::string& name) -> std::string {
      if (name == kRootThemeName) {
        return {};
      }
      auto slash = name.find_last_of('/');
      // A top-level theme's parent is the scan root's own loose-file theme.
      return slash == std::string::npos ? std::string{kRootThemeName} : name.substr(0, slash);
    };

    std::set<std::string> resolved;
    // Recursive lambda via explicit self-parameter (no std::function allocation).
    auto resolve = [&](const std::string& name, auto&& self) -> void {
      if (resolved.count(name)) {
        return;
      }
      resolved.insert(name);
      if (!sidecar.theme_inherit.count(name)) {
        return;
      }
      auto parent = parent_of(name);
      if (parent.empty()) {
        return;
      }
      auto parent_it = themes.find(parent);
      if (parent_it == themes.end()) {
        // No theme for that directory (a pure container). Keep walking up so an
        // intermediate container doesn't silently break the chain.
        auto grandparent = parent;
        while (!grandparent.empty() && themes.find(grandparent) == themes.end()) {
          grandparent = parent_of(grandparent);
        }
        if (grandparent.empty()) {
          return;
        }
        parent_it = themes.find(grandparent);
        if (parent_it == themes.end()) {
          return;
        }
      }
      // Resolve the parent FIRST so what we fold in is its effective pool, not just its
      // own files -- this is what makes the chain transitive.
      self(parent_it->first, self);
      auto& self_theme = themes[name];
      const auto& from = parent_it->second;
      // Inherit the parent's TIER LAYOUT along with its images. The parent's pool is
      // already split into (source, count) spans, and appending it wholesale appends
      // those spans in the same order -- so the child's layout is its own tier followed
      // by the parent's, which is exactly the ancestor chain. Recorded before the copy so
      // the counts describe the spans being appended, not the merged result.
      auto& self_tiers = sidecar.theme_tiers[name];
      const auto parent_tiers = sidecar.theme_tiers[parent_it->first];
      for (const auto& tier : parent_tiers) {
        self_tiers.push_back(tier);
      }
      for (const auto& p : from.image_path()) {
        self_theme.add_image_path(p);
      }
      for (const auto& p : from.animation_path()) {
        self_theme.add_animation_path(p);
      }
      for (const auto& p : from.font_path()) {
        self_theme.add_font_path(p);
      }
      for (const auto& p : from.audio_path()) {
        self_theme.add_audio_path(p);
      }
      for (const auto& t : from.text_line()) {
        self_theme.add_text_line(t);
      }
    };

    std::vector<std::string> names;
    names.reserve(themes.size());
    for (const auto& pair : themes) {
      names.push_back(pair.first);
    }
    for (const auto& name : names) {
      resolve(name, resolve);
    }
  }

  void load_variable(const json& obj, trance_pb::Variable& variable, const std::string& json_path)
  {
    check_unknown_keys(obj, {"description", "value", "default_value"}, json_path);
    variable.set_description(get_string(obj, "description", json_path));
    if (const json* values = find(obj, "value")) {
      auto vp = json_path + "/value";
      if (!values->is_array()) {
        throw std::runtime_error(vp + ": expected an array");
      }
      for (const auto& entry : *values) {
        variable.add_value(entry.get<std::string>());
      }
    }
    variable.set_default_value(get_string(obj, "default_value", json_path));
  }

  // ---------------------------------------------------------------------
  // trance_pb::Session -> Session JSON
  // ---------------------------------------------------------------------

  json save_playlist_item(const trance_pb::PlaylistItem& item)
  {
    json obj = json::object();
    if (item.has_standard()) {
      json s = json::object();
      if (!item.standard().program().empty()) {
        s["program"] = item.standard().program();
      }
      if (item.standard().play_time_seconds()) {
        s["play_time_seconds"] = item.standard().play_time_seconds();
      }
      obj["standard"] = std::move(s);
    } else if (item.has_subroutine()) {
      json sub = json::array();
      for (const auto& name : item.subroutine().playlist_item_name()) {
        sub.push_back(name);
      }
      obj["subroutine"] = std::move(sub);
    }

    if (item.next_item_size()) {
      json arr = json::array();
      for (const auto& next : item.next_item()) {
        json n = json::object();
        n["playlist_item_name"] = next.playlist_item_name();
        n["random_weight"] = next.random_weight();
        if (!next.condition_variable_name().empty()) {
          n["condition_variable_name"] = next.condition_variable_name();
          n["condition_variable_value"] = next.condition_variable_value();
        }
        arr.push_back(std::move(n));
      }
      obj["next_item"] = std::move(arr);
    }

    if (item.audio_event_size()) {
      json arr = json::array();
      for (const auto& ev : item.audio_event()) {
        if (ev.type() == trance_pb::AudioEvent_Type_NONE) {
          continue; // no JSON form (spec sec 7.5)
        }
        json e = json::object();
        e["type"] = save_audio_event_type(ev.type());
        if (ev.channel()) e["channel"] = ev.channel();
        if (ev.next_unused_channel()) e["next_unused_channel"] = ev.next_unused_channel();
        if (!ev.path().empty()) e["path"] = normalize_path_for_save(ev.path());
        if (ev.loop()) e["loop"] = ev.loop();
        if (ev.volume()) e["volume"] = ev.volume();
        if (ev.time_seconds()) e["time_seconds"] = ev.time_seconds();
        arr.push_back(std::move(e));
      }
      if (!arr.empty()) {
        obj["audio_event"] = std::move(arr);
      }
    }
    return obj;
  }

  json save_program(const trance_pb::Program& program, const std::string& program_name,
                     const std::string& root, SessionJsonSidecar& sidecar,
                     std::map<std::string, int>& slug_counts)
  {
    json obj = json::object();
    if (program.enabled_theme_size()) {
      json arr = json::array();
      for (const auto& t : program.enabled_theme()) {
        json e = json::object();
        e["theme_name"] = t.theme_name();
        e["random_weight"] = t.random_weight();
        if (t.pinned()) e["pinned"] = t.pinned();
        arr.push_back(std::move(e));
      }
      obj["enabled_theme"] = std::move(arr);
    }

    if (program.visual_type_size()) {
      json arr = json::array();
      for (const auto& vt : program.visual_type()) {
        json e = json::object();
        e["type"] = save_visual_type(vt.type());
        e["random_weight"] = vt.random_weight();
        if (vt.pinned()) e["pinned"] = vt.pinned();
        arr.push_back(std::move(e));
      }
      obj["visual_type"] = std::move(arr);
    }

    if (program.custom_visual_pattern_size()) {
      json arr = json::array();
      for (const auto& src : program.custom_visual_pattern()) {
        auto key = std::make_pair(program_name, src.name());
        auto it = sidecar.pattern_file.find(key);
        std::string file;
        if (it != sidecar.pattern_file.end()) {
          file = it->second;
        } else {
          auto slug = slugify(src.name());
          auto n = ++slug_counts[slug];
          file = "patterns/" + slug + (n == 1 ? "" : "-" + std::to_string(n)) + ".pattern";
          sidecar.pattern_file[key] = file;
        }

        auto full_path = (std::filesystem::path(root) / file).string();
        std::filesystem::create_directories(std::filesystem::path(full_path).parent_path());
        std::ofstream f{full_path, std::ios::binary};
        f << src.source_text();

        json e = json::object();
        e["name"] = src.name();
        e["file"] = normalize_path_for_save(file);
        if (src.random_weight()) e["random_weight"] = src.random_weight();
        if (src.enabled()) e["enabled"] = src.enabled();
        if (src.pinned()) e["pinned"] = src.pinned();
        arr.push_back(std::move(e));
      }
      obj["custom_visual_pattern"] = std::move(arr);
    }

    if (program.global_fps()) obj["global_fps"] = program.global_fps();
    if (program.zoom_intensity()) obj["zoom_intensity"] = program.zoom_intensity();
    if (!colour_is_default(program.spiral_colour_a())) {
      obj["spiral_colour_a"] = save_colour(program.spiral_colour_a());
    }
    if (!colour_is_default(program.spiral_colour_b())) {
      obj["spiral_colour_b"] = save_colour(program.spiral_colour_b());
    }
    if (program.reverse_spiral_direction()) {
      obj["reverse_spiral_direction"] = program.reverse_spiral_direction();
    }
    if (!colour_is_default(program.main_text_colour())) {
      obj["main_text_colour"] = save_colour(program.main_text_colour());
    }
    if (!colour_is_default(program.shadow_text_colour())) {
      obj["shadow_text_colour"] = save_colour(program.shadow_text_colour());
    }

    if (program.has_entrainment()) {
      json e = json::object();
      if (program.entrainment().master_db()) {
        e["master_db"] = program.entrainment().master_db();
      }
      if (program.entrainment().layer_size()) {
        json arr = json::array();
        for (const auto& layer : program.entrainment().layer()) {
          json l = json::object();
          l["center_hz"] = layer.center_hz();
          l["binaural_hz"] = layer.binaural_hz();
          l["pulse_hz"] = layer.pulse_hz();
          if (layer.amplitude_db()) l["amplitude_db"] = layer.amplitude_db();
          arr.push_back(std::move(l));
        }
        e["layer"] = std::move(arr);
      }
      obj["entrainment"] = std::move(e);
    }
    return obj;
  }

  json save_theme(const trance_pb::Theme& theme, const std::string& theme_name,
                   const SessionJsonSidecar& sidecar)
  {
    json obj = json::object();
    auto scan_it = sidecar.theme_scan.find(theme_name);
    const bool scanned = scan_it != sidecar.theme_scan.end();
    if (scanned) {
      const bool inherits = sidecar.theme_inherit.count(theme_name) != 0;
      auto exclude_it = sidecar.theme_exclude.find(theme_name);
      const bool has_excludes =
          exclude_it != sidecar.theme_exclude.end() && !exclude_it->second.empty();
      // Shorthand when the theme is nothing but its directory, which is the common case.
      if (!inherits && !has_excludes) {
        obj["scan"] = normalize_path_for_save(scan_it->second);
      } else {
        json s = json::object();
        s["dir"] = normalize_path_for_save(scan_it->second);
        if (inherits) s["inherit"] = true;
        if (has_excludes) {
          json arr = json::array();
          for (const auto& p : exclude_it->second) {
            arr.push_back(normalize_path_for_save(p));
          }
          s["exclude"] = std::move(arr);
        }
        obj["scan"] = std::move(s);
      }
    }
    // A scanned theme's media is ENTIRELY scan-derived now (#36: the theme-level
    // search_resources fills text_line and audio_path too), so a pure scan theme
    // round-trips as just {"scan": dir}. Writing the expansion back would duplicate
    // every entry on the next load.
    if (!scanned) {
      if (theme.image_path_size()) {
        json arr = json::array();
        for (const auto& p : theme.image_path()) arr.push_back(normalize_path_for_save(p));
        obj["image_path"] = std::move(arr);
      }
      if (theme.animation_path_size()) {
        json arr = json::array();
        for (const auto& p : theme.animation_path()) arr.push_back(normalize_path_for_save(p));
        obj["animation_path"] = std::move(arr);
      }
      if (theme.font_path_size()) {
        json arr = json::array();
        for (const auto& p : theme.font_path()) arr.push_back(normalize_path_for_save(p));
        obj["font_path"] = std::move(arr);
      }
      if (theme.text_line_size()) {
        json arr = json::array();
        for (const auto& t : theme.text_line()) arr.push_back(t);
        obj["text_line"] = std::move(arr);
      }
      if (theme.audio_path_size()) {
        json arr = json::array();
        for (const auto& p : theme.audio_path()) arr.push_back(normalize_path_for_save(p));
        obj["audio_path"] = std::move(arr);
      }
    }
    return obj;
  }

  json save_variable(const trance_pb::Variable& variable)
  {
    json obj = json::object();
    if (!variable.description().empty()) obj["description"] = variable.description();
    if (variable.value_size()) {
      json arr = json::array();
      for (const auto& v : variable.value()) arr.push_back(v);
      obj["value"] = std::move(arr);
    }
    if (!variable.default_value().empty()) obj["default_value"] = variable.default_value();
    return obj;
  }

  // ---------------------------------------------------------------------
  // system.json
  // ---------------------------------------------------------------------

  void load_export_settings(const json& obj, trance_pb::ExportSettings& settings,
                             const std::string& json_path)
  {
    check_unknown_keys(
        obj, {"path", "export_3d", "width", "height", "fps", "length", "quality", "threads"},
        json_path);
    settings.set_path(get_string(obj, "path", json_path));
    settings.set_export_3d(get_bool(obj, "export_3d", json_path));
    settings.set_width(get_uint(obj, "width", json_path));
    settings.set_height(get_uint(obj, "height", json_path));
    settings.set_fps(get_uint(obj, "fps", json_path));
    settings.set_length(get_uint(obj, "length", json_path));
    settings.set_quality(get_uint(obj, "quality", json_path));
    settings.set_threads(get_uint(obj, "threads", json_path));
  }

  json save_export_settings(const trance_pb::ExportSettings& s)
  {
    json obj = json::object();
    if (!s.path().empty()) obj["path"] = normalize_path_for_save(s.path());
    if (s.export_3d()) obj["export_3d"] = s.export_3d();
    if (s.width()) obj["width"] = s.width();
    if (s.height()) obj["height"] = s.height();
    if (s.fps()) obj["fps"] = s.fps();
    if (s.length()) obj["length"] = s.length();
    if (s.quality()) obj["quality"] = s.quality();
    if (s.threads()) obj["threads"] = s.threads();
    return obj;
  }

} // anonymous namespace

trance_pb::Session load_session_json(const std::string& path, const std::string& root,
                                      SessionJsonSidecar& sidecar)
{
  std::ifstream f{path};
  if (!f) {
    throw std::runtime_error("couldn't open " + path);
  }
  json root_json;
  try {
    f >> root_json;
  } catch (const json::exception& e) {
    throw std::runtime_error(path + ": JSON parse error: " + e.what());
  }
  if (!root_json.is_object()) {
    throw std::runtime_error(path + ": expected a JSON object at top level");
  }

  require_format(root_json, "trance-session");
  check_unknown_keys(root_json,
                      {"format", "format_version", "first_playlist_item", "playlist",
                       "program_map", "theme_map", "theme_scan_root", "variable_map"},
                      "");

  trance_pb::Session session;
  session.set_first_playlist_item(get_string(root_json, "first_playlist_item", ""));

  // Session-level scan root: the theme SET is derived from this directory tree, not just
  // each theme's own content. Parsed before theme_map so discovery can run right after it.
  if (const json* scan_root = find(root_json, "theme_scan_root")) {
    const std::string sp = "/theme_scan_root";
    if (scan_root->is_string()) {
      sidecar.theme_scan_root = scan_root->get<std::string>();
    } else if (scan_root->is_object()) {
      check_unknown_keys(*scan_root, {"dir", "auto_rescan"}, sp);
      const json* dir_value = find(*scan_root, "dir");
      if (!dir_value || !dir_value->is_string()) {
        throw std::runtime_error(sp + "/dir: expected a string");
      }
      sidecar.theme_scan_root = dir_value->get<std::string>();
      if (const json* a = find(*scan_root, "auto_rescan")) {
        if (!a->is_boolean()) {
          throw std::runtime_error(sp + "/auto_rescan: expected a boolean");
        }
        sidecar.theme_scan_root_auto = a->get<bool>();
      }
    } else {
      throw std::runtime_error(sp + ": expected a string or an object");
    }
    check_relative_path(sidecar.theme_scan_root, sp);
  }

  if (const json* playlist = find(root_json, "playlist")) {
    if (!playlist->is_object()) {
      throw std::runtime_error("/playlist: expected an object");
    }
    for (auto it = playlist->begin(); it != playlist->end(); ++it) {
      if (is_comment_key(it.key())) continue;
      load_playlist_item(it.value(), (*session.mutable_playlist())[it.key()],
                          "/playlist/" + it.key());
    }
  }

  if (const json* programs = find(root_json, "program_map")) {
    if (!programs->is_object()) {
      throw std::runtime_error("/program_map: expected an object");
    }
    for (auto it = programs->begin(); it != programs->end(); ++it) {
      if (is_comment_key(it.key())) continue;
      load_program(it.value(), (*session.mutable_program_map())[it.key()], it.key(),
                   "/program_map/" + it.key(), root, sidecar);
    }
  }

  if (const json* themes = find(root_json, "theme_map")) {
    if (!themes->is_object()) {
      throw std::runtime_error("/theme_map: expected an object");
    }
    for (auto it = themes->begin(); it != themes->end(); ++it) {
      if (is_comment_key(it.key())) continue;
      load_theme(it.value(), (*session.mutable_theme_map())[it.key()], it.key(),
                 "/theme_map/" + it.key(), root, sidecar);
    }
  }
  // Every session is a live folder unless it says otherwise. There is one theme model --
  // a directory plus a blacklist -- so defaulting the scan root here is what removes the
  // whole "which kind of session is this" question, along with the migration button that
  // used to exist to answer it.
  if (sidecar.theme_scan_root.empty()) {
    sidecar.theme_scan_root = ".";
  }
  migrate_frozen_themes(session, root, sidecar);
  // Discovery BEFORE inheritance: a folder that just appeared is a theme like any other
  // and must be able to inherit (and to be inherited from) on the very first load that
  // sees it. Runs even with no theme_map key at all, so a session can be nothing but a
  // scan root.
  if (sidecar.theme_scan_root_auto) {
    discover_new_themes(session, root, sidecar);
  }
  // After every theme exists: a theme can only inherit a pool that has been loaded.
  resolve_theme_inheritance(session, sidecar);

  if (const json* variables = find(root_json, "variable_map")) {
    if (!variables->is_object()) {
      throw std::runtime_error("/variable_map: expected an object");
    }
    for (auto it = variables->begin(); it != variables->end(); ++it) {
      if (is_comment_key(it.key())) continue;
      load_variable(it.value(), (*session.mutable_variable_map())[it.key()],
                    "/variable_map/" + it.key());
    }
  }

  return session;
}

namespace
{
  // Write-then-rename, shared by both savers below. The F2 panel autosaves the live
  // session on every committed edit, so these files are rewritten constantly -- and
  // default.json is the user's only copy. A plain ofstream truncates on open, so a
  // process that dies mid-write (crash, kill, full disk) would leave half a JSON where
  // the session used to be. Renaming a sibling temp file over the target is atomic on
  // the same volume, so a reader (or the next launch) sees either the old file or the
  // new one, never a partial one.
  void write_file_atomically(const std::string& path, const std::string& contents)
  {
    const auto temp = path + ".tmp";
    {
      // Text mode, matching what these files were always written in -- switching to
      // binary would silently rewrite every line ending on Windows.
      std::ofstream f{temp};
      if (!f) {
        throw std::runtime_error("couldn't open " + temp + " for writing");
      }
      f << contents;
      f.flush();
      if (!f) {
        throw std::runtime_error("couldn't write " + temp);
      }
    }
    std::error_code ec;
    // std::filesystem::rename replaces an existing regular file (MoveFileEx with
    // REPLACE_EXISTING under MSVC), so there is no unlink-first window to lose.
    std::filesystem::rename(temp, path, ec);
    if (ec) {
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      throw std::runtime_error("couldn't replace " + path + ": " + ec.message());
    }
  }
}

void save_session_json(const trance_pb::Session& session, const std::string& path,
                        const std::string& root, SessionJsonSidecar& sidecar)
{
  json root_json = json::object();
  root_json["format"] = "trance-session";
  root_json["format_version"] = 1;
  if (!session.first_playlist_item().empty()) {
    root_json["first_playlist_item"] = session.first_playlist_item();
  }

  if (session.playlist_size()) {
    json playlist = json::object();
    for (const auto& pair : session.playlist()) {
      playlist[pair.first] = save_playlist_item(pair.second);
    }
    root_json["playlist"] = std::move(playlist);
  }

  if (session.program_map_size()) {
    json programs = json::object();
    std::map<std::string, int> slug_counts;
    for (const auto& pair : session.program_map()) {
      programs[pair.first] = save_program(pair.second, pair.first, root, sidecar, slug_counts);
    }
    root_json["program_map"] = std::move(programs);
  }

  if (session.theme_map_size()) {
    json themes = json::object();
    for (const auto& pair : session.theme_map()) {
      themes[pair.first] = save_theme(pair.second, pair.first, sidecar);
    }
    root_json["theme_map"] = std::move(themes);
  }

  if (!sidecar.theme_scan_root.empty()) {
    // Plain string when auto-rescan is on (the default and the whole point); the object
    // form only appears once the user has deliberately frozen the theme set.
    if (sidecar.theme_scan_root_auto) {
      root_json["theme_scan_root"] = normalize_path_for_save(sidecar.theme_scan_root);
    } else {
      json s = json::object();
      s["dir"] = normalize_path_for_save(sidecar.theme_scan_root);
      s["auto_rescan"] = false;
      root_json["theme_scan_root"] = std::move(s);
    }
  }

  if (session.variable_map_size()) {
    json variables = json::object();
    for (const auto& pair : session.variable_map()) {
      variables[pair.first] = save_variable(pair.second);
    }
    root_json["variable_map"] = std::move(variables);
  }

  // A silent failure here is worse than a throw: callers (the F2 panel's autosave and
  // Export, legacy auto-convert) report "saved"/reload the file on a normal return, so
  // a write that never landed (read-only dir, disk full) must surface as an error.
  write_file_atomically(path, root_json.dump(2));
}

trance_pb::System load_system_json(const std::string& path)
{
  std::ifstream f{path};
  if (!f) {
    throw std::runtime_error("couldn't open " + path);
  }
  json root_json;
  try {
    f >> root_json;
  } catch (const json::exception& e) {
    throw std::runtime_error(path + ": JSON parse error: " + e.what());
  }
  if (!root_json.is_object()) {
    throw std::runtime_error(path + ": expected a JSON object at top level");
  }

  require_format(root_json, "trance-system");
  // `last_export_settings` is DEAD SCHEMA deliberately retained. The video-export path
  // it configured is gone, and nothing reads these values any more -- but every
  // system.json ever written by get_default_system() contains the key, and
  // check_unknown_keys THROWS on anything not listed here. Dropping it from the
  // allow-list would make trance.exe fail to start on every existing install. It goes
  // when the format_version is bumped and old configs are migrated, not before.
  //
  // `renderer` is NOT on the list, deliberately: there is no renderer choice any more
  // (docs/spec-xr-unified.md D2), and no back-compat entry was added for it. A
  // system.json still carrying the key therefore fails here and regenerates with
  // defaults, losing its other settings -- the accepted, spec'd cost of the greenfield
  // config rule.
  check_unknown_keys(root_json,
                      {"format", "format_version", "enable_vsync", "windowed",
                       "draw_depth", "eye_spacing", "image_cache_size", "animation_buffer_size",
                       "font_cache_size", "last_root_directory", "last_export_settings",
                       "last_session_map"},
                      "");

  trance_pb::System system;
  system.set_enable_vsync(get_bool(root_json, "enable_vsync", ""));
  system.set_windowed(get_bool(root_json, "windowed", ""));

  if (const json* dd = find(root_json, "draw_depth")) {
    if (!dd->is_number()) {
      throw std::runtime_error("/draw_depth: expected a number");
    }
    system.mutable_draw_depth()->set_draw_depth(dd->get<float>());
  }
  if (const json* es = find(root_json, "eye_spacing")) {
    if (!es->is_number()) {
      throw std::runtime_error("/eye_spacing: expected a number");
    }
    system.mutable_eye_spacing()->set_eye_spacing(es->get<float>());
  }

  system.set_image_cache_size(get_uint(root_json, "image_cache_size", ""));
  system.set_animation_buffer_size(get_uint(root_json, "animation_buffer_size", ""));
  system.set_font_cache_size(get_uint(root_json, "font_cache_size", ""));
  system.set_last_root_directory(get_string(root_json, "last_root_directory", ""));

  if (const json* export_settings = find(root_json, "last_export_settings")) {
    load_export_settings(*export_settings, *system.mutable_last_export_settings(),
                          "/last_export_settings");
  }

  if (const json* session_map = find(root_json, "last_session_map")) {
    if (!session_map->is_object()) {
      throw std::runtime_error("/last_session_map: expected an object");
    }
    for (auto it = session_map->begin(); it != session_map->end(); ++it) {
      if (is_comment_key(it.key())) continue;
      auto& last_session = (*system.mutable_last_session_map())[it.key()];
      auto vp = "/last_session_map/" + it.key();
      if (!it.value().is_object()) {
        throw std::runtime_error(vp + ": expected an object");
      }
      for (auto vit = it.value().begin(); vit != it.value().end(); ++vit) {
        if (is_comment_key(vit.key())) continue;
        if (!vit.value().is_string()) {
          throw std::runtime_error(vp + "/" + vit.key() + ": expected a string");
        }
        (*last_session.mutable_variable_map())[vit.key()] = vit.value().get<std::string>();
      }
    }
  }

  return system;
}

void save_system_json(const trance_pb::System& system, const std::string& path)
{
  json root_json = json::object();
  root_json["format"] = "trance-system";
  root_json["format_version"] = 1;

  if (system.enable_vsync()) root_json["enable_vsync"] = system.enable_vsync();
  if (system.windowed()) root_json["windowed"] = system.windowed();

  if (system.has_draw_depth()) {
    root_json["draw_depth"] = system.draw_depth().draw_depth();
  }
  if (system.has_eye_spacing()) {
    root_json["eye_spacing"] = system.eye_spacing().eye_spacing();
  }

  if (system.image_cache_size()) root_json["image_cache_size"] = system.image_cache_size();
  if (system.animation_buffer_size()) {
    root_json["animation_buffer_size"] = system.animation_buffer_size();
  }
  if (system.font_cache_size()) root_json["font_cache_size"] = system.font_cache_size();
  if (!system.last_root_directory().empty()) {
    root_json["last_root_directory"] = system.last_root_directory();
  }

  if (system.has_last_export_settings()) {
    root_json["last_export_settings"] = save_export_settings(system.last_export_settings());
  }

  if (system.last_session_map_size()) {
    json session_map = json::object();
    for (const auto& pair : system.last_session_map()) {
      json vars = json::object();
      for (const auto& vpair : pair.second.variable_map()) {
        vars[vpair.first] = vpair.second;
      }
      session_map[pair.first] = std::move(vars);
    }
    root_json["last_session_map"] = std::move(session_map);
  }

  // Same atomic write as save_session_json above: a failed write must throw rather
  // than return normally (the F2 UI's System section reports "saved" on return), and
  // the System section persists on every radio click, so this file is rewritten often
  // enough to deserve the same crash-safety.
  write_file_atomically(path, root_json.dump(2));
}
