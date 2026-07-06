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

  trance_pb::System_Renderer parse_renderer(const std::string& s, const std::string& json_path)
  {
    if (s == "monitor") return trance_pb::System_Renderer_MONITOR;
    if (s == "openvr") return trance_pb::System_Renderer_OPENVR;
    throw std::runtime_error(json_path + ": unknown renderer '" + s + "'");
  }

  std::string save_renderer(trance_pb::System_Renderer r)
  {
    // OCULUS is dead (spec sec 6); anything that isn't OPENVR saves as "monitor".
    return r == trance_pb::System_Renderer_OPENVR ? "openvr" : "monitor";
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
        check_unknown_keys(entry, {"type", "random_weight"}, ep);
        auto* vt = program.add_visual_type();
        vt->set_type(parse_visual_type(get_string(entry, "type", ep), ep));
        vt->set_random_weight(get_uint(entry, "random_weight", ep));
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
        check_unknown_keys(entry, {"name", "file", "random_weight", "enabled"}, ep);
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
      if (!scan->is_string()) {
        throw std::runtime_error(sp + ": expected a string");
      }
      auto dir = scan->get<std::string>();
      check_relative_path(dir, sp);
      sidecar.theme_scan[theme_name] = dir;
      auto full_dir = (std::filesystem::path(root) / dir).string();
      if (std::filesystem::exists(full_dir)) {
        search_resources(theme, full_dir);
      }
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
      obj["scan"] = normalize_path_for_save(scan_it->second);
    }
    // Scan-derived media lists (image/animation/font -- exactly what the theme-level
    // search_resources fills) are omitted for scanned themes: reloading re-derives them,
    // and writing them would duplicate entries on the next load. text_line and audio_path
    // are NEVER scan-derived, so they must be written regardless.
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
                       "program_map", "theme_map", "variable_map"},
                      "");

  trance_pb::Session session;
  session.set_first_playlist_item(get_string(root_json, "first_playlist_item", ""));

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

  if (session.variable_map_size()) {
    json variables = json::object();
    for (const auto& pair : session.variable_map()) {
      variables[pair.first] = save_variable(pair.second);
    }
    root_json["variable_map"] = std::move(variables);
  }

  std::ofstream f{path};
  f << root_json.dump(2);
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
  check_unknown_keys(root_json,
                      {"format", "format_version", "enable_vsync", "renderer", "windowed",
                       "draw_depth", "eye_spacing", "image_cache_size", "animation_buffer_size",
                       "font_cache_size", "last_root_directory", "last_export_settings",
                       "last_session_map"},
                      "");

  trance_pb::System system;
  system.set_enable_vsync(get_bool(root_json, "enable_vsync", ""));
  if (const json* renderer = find(root_json, "renderer")) {
    system.set_renderer(parse_renderer(renderer->get<std::string>(), "/renderer"));
  }
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
  if (system.renderer() != trance_pb::System_Renderer_MONITOR) {
    root_json["renderer"] = save_renderer(system.renderer());
  }
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

  std::ofstream f{path};
  f << root_json.dump(2);
}
