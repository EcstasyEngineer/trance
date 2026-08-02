#include <common/session_legacy.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

#pragma warning(push, 0)
#include <google/protobuf/text_format.h>
#include <common/legacy.pb.h>
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  // Legacy .session/.cfg files are hand/tool-authored on Windows and are riddled with
  // literal backslash path separators, e.g. `image_path: ".\\hypno\\103827022_p4.png"`.
  // protobuf TextFormat's string-literal grammar treats a backslash followed by 1-3
  // octal digits (or \x../\u..../\U........) as an escape sequence -- so `\103` silently
  // decodes to the single byte 0x43 ('C'), and `\227` decodes to the single byte 0x97,
  // which is invalid as standalone UTF-8 and makes JSON emission throw. This corpus has
  // no intentionally-authored octal/hex/unicode string escapes (paths and text_line
  // strings are the only string content; colours are float fields, not string escapes --
  // docs/session-json-format.md sec 2.3), so inside quoted string literals we treat any
  // backslash immediately followed by a digit or x/X/u/U as a Windows path separator, not
  // an escape, and double it so protobuf reads it as a literal backslash. True escapes
  // actually used in this corpus (`\n` line breaks in text_line, `\"` quoted phrases,
  // `\\` itself) are left untouched. `#` line comments are also tracked so a `#` inside a
  // string doesn't get misread as a comment starting outside one (and vice versa).
  std::string protect_legacy_windows_backslashes(const std::string& in, std::size_t& fixed_count)
  {
    enum class State { normal, line_comment, string };
    State state = State::normal;
    char quote = 0;

    std::string out;
    out.reserve(in.size());
    fixed_count = 0;

    for (std::size_t i = 0; i < in.size(); ++i) {
      char c = in[i];

      if (state == State::line_comment) {
        out.push_back(c);
        if (c == '\n') {
          state = State::normal;
        }
        continue;
      }

      if (state == State::normal) {
        out.push_back(c);
        if (c == '#') {
          state = State::line_comment;
        } else if (c == '"' || c == '\'') {
          state = State::string;
          quote = c;
        }
        continue;
      }

      // state == State::string
      if (c == quote) {
        out.push_back(c);
        state = State::normal;
        continue;
      }

      if (c != '\\' || i + 1 == in.size()) {
        out.push_back(c);
        continue;
      }

      unsigned char next = static_cast<unsigned char>(in[i + 1]);
      bool is_digit_or_hex_unicode_lead =
          (next >= '0' && next <= '9') || next == 'x' || next == 'X' || next == 'u' || next == 'U';
      if (is_digit_or_hex_unicode_lead) {
        // Not a real escape in this corpus -- a Windows path separator followed by a
        // filename/directory that starts with a digit (or looks hex-ish). Double the
        // backslash so TextFormat parses it as one literal backslash; do not consume
        // `next` here, it's copied verbatim on the following iteration.
        out.push_back('\\');
        out.push_back('\\');
        ++fixed_count;
        continue;
      }

      // A genuine escape used in this corpus (\n, \", \\, ...) -- copy both chars as-is.
      out.push_back(c);
      out.push_back(in[i + 1]);
      ++i;
    }

    return out;
  }

  // Field names this fork added to trance.proto AFTER the fork point (0e97381). They
  // are not part of the frozen legacy schema and never appear in an upstream-era file,
  // so a parse failure mentioning one of them means the input was written by this fork
  // rather than by upstream trance. Diagnostics only -- the schema itself lives in
  // legacy.proto, and this list never gates what parses.
  const char* const kForkAddedFieldNames[] = {"entrainment", "custom_visual_pattern",
                                              "audio_path"};

  std::string fork_field_hint(const std::string& text)
  {
    for (const char* name : kForkAddedFieldNames) {
      if (text.find(name) != std::string::npos) {
        return std::string{" (found '"} + name +
            "', a field this fork added after the fork point -- the importer reads"
            " upstream-era files only, see docs/session-json-format.md sec 7)";
      }
    }
    return {};
  }

  // Parses a legacy text-format protobuf file into T (trance_legacy_pb::Session or
  // trance_legacy_pb::System -- the FROZEN upstream-era descriptor, never the live
  // trance.proto one). Throws std::runtime_error on missing file or parse failure.
  template <typename T>
  T load_legacy_proto(const std::string& path)
  {
    std::ifstream f{path};
    if (!f) {
      throw std::runtime_error("couldn't open " + path);
    }
    std::string str{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};

    std::size_t fixed_count = 0;
    str = protect_legacy_windows_backslashes(str, fixed_count);
    if (fixed_count) {
      std::cerr << "warning: " << path << ": normalized " << fixed_count
                << " ambiguous backslash-digit/hex/unicode sequences as literal Windows"
                   " path separators before parsing (see session_legacy.cpp comment)\n";
    }

    T proto;
    if (!google::protobuf::TextFormat::ParseFromString(str, &proto)) {
      throw std::runtime_error(path + ": failed to parse as legacy trance textproto" +
                               fork_field_hint(str));
    }
    return proto;
  }

  // ---------------------------------------------------------------------------
  // Frozen legacy schema -> live model. This is the ONLY place the two schemas meet.
  // Every field below existed at the fork point; nothing this fork added has a source
  // to be translated from, by construction. When trance.proto changes shape, these
  // functions stop compiling -- fix the mapping here rather than widening legacy.proto.
  // ---------------------------------------------------------------------------

  trance_pb::Colour convert(const trance_legacy_pb::Colour& in)
  {
    trance_pb::Colour out;
    out.set_r(in.r());
    out.set_g(in.g());
    out.set_b(in.b());
    out.set_a(in.a());
    return out;
  }

  trance_pb::System::Renderer convert(trance_legacy_pb::System::Renderer in)
  {
    switch (in) {
    case trance_legacy_pb::System::OCULUS:
      // Still translated faithfully; validate_system() is what downgrades it to
      // MONITOR with a warning (docs/session-json-format.md sec 7).
      return trance_pb::System::OCULUS;
    case trance_legacy_pb::System::OPENVR:
      return trance_pb::System::OPENVR;
    case trance_legacy_pb::System::MONITOR:
    default:
      return trance_pb::System::MONITOR;
    }
  }

  trance_pb::Program::VisualType convert(trance_legacy_pb::Program::VisualType in)
  {
    switch (in) {
    case trance_legacy_pb::Program::ACCELERATE:
      return trance_pb::Program::ACCELERATE;
    case trance_legacy_pb::Program::SLOW_FLASH:
      return trance_pb::Program::SLOW_FLASH;
    case trance_legacy_pb::Program::SUB_TEXT:
      return trance_pb::Program::SUB_TEXT;
    case trance_legacy_pb::Program::FLASH_TEXT:
      return trance_pb::Program::FLASH_TEXT;
    case trance_legacy_pb::Program::PARALLEL:
      return trance_pb::Program::PARALLEL;
    case trance_legacy_pb::Program::SUPER_PARALLEL:
      return trance_pb::Program::SUPER_PARALLEL;
    case trance_legacy_pb::Program::ANIMATION:
      return trance_pb::Program::ANIMATION;
    case trance_legacy_pb::Program::SUPER_FAST:
      return trance_pb::Program::SUPER_FAST;
    case trance_legacy_pb::Program::NONE:
    default:
      return trance_pb::Program::NONE;
    }
  }

  trance_pb::AudioEvent::Type convert(trance_legacy_pb::AudioEvent::Type in)
  {
    switch (in) {
    case trance_legacy_pb::AudioEvent::AUDIO_PLAY:
      return trance_pb::AudioEvent::AUDIO_PLAY;
    case trance_legacy_pb::AudioEvent::AUDIO_STOP:
      return trance_pb::AudioEvent::AUDIO_STOP;
    case trance_legacy_pb::AudioEvent::AUDIO_FADE:
      return trance_pb::AudioEvent::AUDIO_FADE;
    case trance_legacy_pb::AudioEvent::NONE:
    default:
      return trance_pb::AudioEvent::NONE;
    }
  }

  trance_pb::ExportSettings convert(const trance_legacy_pb::ExportSettings& in)
  {
    trance_pb::ExportSettings out;
    out.set_path(in.path());
    out.set_export_3d(in.export_3d());
    out.set_width(in.width());
    out.set_height(in.height());
    out.set_fps(in.fps());
    out.set_length(in.length());
    out.set_quality(in.quality());
    out.set_threads(in.threads());
    return out;
  }

  trance_pb::AudioEvent convert(const trance_legacy_pb::AudioEvent& in)
  {
    trance_pb::AudioEvent out;
    out.set_type(convert(in.type()));
    out.set_next_unused_channel(in.next_unused_channel());
    out.set_channel(in.channel());
    out.set_path(in.path());
    out.set_loop(in.loop());
    out.set_volume(in.volume());
    out.set_time_seconds(in.time_seconds());
    return out;
  }

  trance_pb::PlaylistItem convert(const trance_legacy_pb::PlaylistItem& in)
  {
    trance_pb::PlaylistItem out;
    // Deprecated flat fields 100/101: carried across untouched. validate_session() is
    // what migrates them into standard{} (docs/session-json-format.md sec 7 step 2).
    out.set_program(in.program());
    out.set_play_time_seconds(in.play_time_seconds());

    switch (in.contents_case()) {
    case trance_legacy_pb::PlaylistItem::kStandard:
      out.mutable_standard()->set_program(in.standard().program());
      out.mutable_standard()->set_play_time_seconds(in.standard().play_time_seconds());
      break;
    case trance_legacy_pb::PlaylistItem::kSubroutine:
      for (const auto& name : in.subroutine().playlist_item_name()) {
        out.mutable_subroutine()->add_playlist_item_name(name);
      }
      break;
    case trance_legacy_pb::PlaylistItem::CONTENTS_NOT_SET:
    default:
      break;
    }

    for (const auto& next : in.next_item()) {
      auto& out_next = *out.add_next_item();
      out_next.set_playlist_item_name(next.playlist_item_name());
      out_next.set_random_weight(next.random_weight());
      out_next.set_condition_variable_name(next.condition_variable_name());
      out_next.set_condition_variable_value(next.condition_variable_value());
    }
    for (const auto& event : in.audio_event()) {
      *out.add_audio_event() = convert(event);
    }
    return out;
  }

  trance_pb::Program convert(const trance_legacy_pb::Program& in)
  {
    trance_pb::Program out;
    // Deprecated field 100: carried across untouched; validate_session() migrates it.
    for (const auto& name : in.enabled_theme_name()) {
      out.add_enabled_theme_name(name);
    }
    for (const auto& theme : in.enabled_theme()) {
      auto& out_theme = *out.add_enabled_theme();
      out_theme.set_theme_name(theme.theme_name());
      out_theme.set_random_weight(theme.random_weight());
      out_theme.set_pinned(theme.pinned());
    }
    for (const auto& visual : in.visual_type()) {
      auto& out_visual = *out.add_visual_type();
      out_visual.set_type(convert(visual.type()));
      out_visual.set_random_weight(visual.random_weight());
      // VisualTypeConfig.pinned is a fork addition -- no legacy source, left default.
    }
    out.set_global_fps(in.global_fps());
    out.set_zoom_intensity(in.zoom_intensity());
    // Message-typed fields are copied only when actually present: proto3 message
    // presence is observable (has_draw_depth() gates the default in session.cpp:662,
    // has_* gates JSON emission in session_json.cpp), so materializing an absent
    // submessage as an all-zero one would change what the importer produces.
    if (in.has_spiral_colour_a()) {
      *out.mutable_spiral_colour_a() = convert(in.spiral_colour_a());
    }
    if (in.has_spiral_colour_b()) {
      *out.mutable_spiral_colour_b() = convert(in.spiral_colour_b());
    }
    out.set_reverse_spiral_direction(in.reverse_spiral_direction());
    if (in.has_main_text_colour()) {
      *out.mutable_main_text_colour() = convert(in.main_text_colour());
    }
    if (in.has_shadow_text_colour()) {
      *out.mutable_shadow_text_colour() = convert(in.shadow_text_colour());
    }
    // Program.entrainment and Program.custom_visual_pattern are fork additions -- no
    // legacy source, left unset.
    return out;
  }

  trance_pb::Theme convert(const trance_legacy_pb::Theme& in)
  {
    trance_pb::Theme out;
    for (const auto& path : in.image_path()) {
      out.add_image_path(path);
    }
    for (const auto& path : in.animation_path()) {
      out.add_animation_path(path);
    }
    for (const auto& path : in.font_path()) {
      out.add_font_path(path);
    }
    for (const auto& line : in.text_line()) {
      out.add_text_line(line);
    }
    // Theme.audio_path is a fork addition -- no legacy source, left empty.
    return out;
  }

  trance_pb::Variable convert(const trance_legacy_pb::Variable& in)
  {
    trance_pb::Variable out;
    out.set_description(in.description());
    for (const auto& value : in.value()) {
      out.add_value(value);
    }
    out.set_default_value(in.default_value());
    return out;
  }

  trance_pb::Session convert(const trance_legacy_pb::Session& in)
  {
    trance_pb::Session out;
    out.set_first_playlist_item(in.first_playlist_item());
    for (const auto& entry : in.playlist()) {
      (*out.mutable_playlist())[entry.first] = convert(entry.second);
    }
    for (const auto& entry : in.program_map()) {
      (*out.mutable_program_map())[entry.first] = convert(entry.second);
    }
    for (const auto& entry : in.theme_map()) {
      (*out.mutable_theme_map())[entry.first] = convert(entry.second);
    }
    for (const auto& entry : in.variable_map()) {
      (*out.mutable_variable_map())[entry.first] = convert(entry.second);
    }
    return out;
  }

  trance_pb::System convert(const trance_legacy_pb::System& in)
  {
    trance_pb::System out;
    out.set_enable_vsync(in.enable_vsync());
    out.set_renderer(convert(in.renderer()));
    // Presence-sensitive (see the note in convert(Program)): an absent draw_depth is
    // what makes validate_system fill in the default rather than leaving it at 0.
    if (in.has_draw_depth()) {
      out.mutable_draw_depth()->set_draw_depth(in.draw_depth().draw_depth());
    }
    if (in.has_eye_spacing()) {
      out.mutable_eye_spacing()->set_eye_spacing(in.eye_spacing().eye_spacing());
    }
    out.set_image_cache_size(in.image_cache_size());
    out.set_animation_buffer_size(in.animation_buffer_size());
    out.set_font_cache_size(in.font_cache_size());
    out.set_windowed(in.windowed());
    out.set_last_root_directory(in.last_root_directory());
    if (in.has_last_export_settings()) {
      *out.mutable_last_export_settings() = convert(in.last_export_settings());
    }
    for (const auto& entry : in.last_session_map()) {
      auto& out_session = (*out.mutable_last_session_map())[entry.first];
      for (const auto& variable : entry.second.variable_map()) {
        (*out_session.mutable_variable_map())[variable.first] = variable.second;
      }
    }
    return out;
  }
}

trance_pb::Session load_legacy_session(const std::string& path)
{
  return convert(load_legacy_proto<trance_legacy_pb::Session>(path));
}

trance_pb::System load_legacy_system(const std::string& path)
{
  return convert(load_legacy_proto<trance_legacy_pb::System>(path));
}
