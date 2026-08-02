#include <trance/net/command_protocol.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace command_protocol
{
  namespace
  {
    std::vector<std::string> split_ws(const std::string& line)
    {
      std::vector<std::string> tokens;
      std::istringstream in{line};
      std::string tok;
      while (in >> tok) {
        tokens.push_back(tok);
      }
      return tokens;
    }

    // Strict float parse: rejects trailing garbage ("0.5x") rather than silently truncating,
    // so a typo'd command line comes back `err ...` instead of a silently-wrong value.
    bool parse_float(const std::string& s, float& out)
    {
      if (s.empty()) {
        return false;
      }
      char* end = nullptr;
      double v = std::strtod(s.c_str(), &end);
      if (end != s.c_str() + s.size()) {
        return false;
      }
      out = float(v);
      return true;
    }

    // Strict non-negative integer parse, same trailing-garbage policy as parse_float.
    bool parse_index(const std::string& s, int& out)
    {
      if (s.empty()) {
        return false;
      }
      char* end = nullptr;
      long v = std::strtol(s.c_str(), &end, 10);
      if (end != s.c_str() + s.size() || v < 0) {
        return false;
      }
      out = int(v);
      return true;
    }

    float clamp(float value, float lo, float hi)
    {
      return value < lo ? lo : value > hi ? hi : value;
    }
  }

  float clamp01(float value)
  {
    return value < 0.f ? 0.f : value > 1.f ? 1.f : value;
  }

  std::string format_err(const std::string& message)
  {
    return "err " + message;
  }

  std::string format_ok(const std::string& body)
  {
    return body.empty() ? "ok" : "ok " + body;
  }

  ParsedCommand parse_command(const std::string& line)
  {
    ParsedCommand cmd;
    auto tokens = split_ws(line);
    if (tokens.empty()) {
      cmd.ok = false;
      cmd.error = "empty command";
      return cmd;
    }

    const std::string& verb = tokens[0];

    if (verb == "pause" && tokens.size() == 1) {
      cmd.verb = Verb::kPause;
    } else if (verb == "resume" && tokens.size() == 1) {
      cmd.verb = Verb::kResume;
    } else if (verb == "status" && tokens.size() == 1) {
      cmd.verb = Verb::kStatus;
    } else if (verb == "hide" && tokens.size() == 1) {
      cmd.verb = Verb::kHide;
    } else if (verb == "show" && tokens.size() == 1) {
      cmd.verb = Verb::kShow;
    } else if (verb == "overlay" && tokens.size() == 2 && tokens[1] == "on") {
      cmd.verb = Verb::kOverlayOn;
    } else if (verb == "overlay" && tokens.size() == 2 && tokens[1] == "off") {
      cmd.verb = Verb::kOverlayOff;
    } else if (verb == "overlay" && tokens.size() == 3 && tokens[1] == "opacity") {
      if (!parse_float(tokens[2], cmd.number)) {
        cmd.ok = false;
        cmd.error = "overlay opacity: not a number: " + tokens[2];
        return cmd;
      }
      cmd.number = clamp01(cmd.number);
      cmd.verb = Verb::kOverlayOpacity;
    } else if (verb == "overlay") {
      cmd.ok = false;
      cmd.error = "usage: overlay on|off|opacity VALUE";
      return cmd;
    } else if (verb == "load" && tokens.size() == 3 && tokens[1] == "pattern") {
      cmd.verb = Verb::kLoadPattern;
      cmd.value = tokens[2];
    } else if (verb == "load") {
      cmd.ok = false;
      cmd.error = "usage: load pattern FILE";
      return cmd;
    } else if (verb == "ui" && tokens.size() == 2 && tokens[1] == "on") {
      cmd.verb = Verb::kUiOn;
    } else if (verb == "ui" && tokens.size() == 2 && tokens[1] == "off") {
      cmd.verb = Verb::kUiOff;
    } else if (verb == "ui") {
      cmd.ok = false;
      cmd.error = "usage: ui on|off";
      return cmd;
    } else if (verb == "screenshot" && tokens.size() == 2) {
      cmd.verb = Verb::kScreenshot;
      cmd.value = tokens[1];
    } else if (verb == "screenshot") {
      cmd.ok = false;
      cmd.error = "usage: screenshot FILE.png";
      return cmd;
    } else if (verb == "mute" && tokens.size() == 2 &&
               (tokens[1] == "on" || tokens[1] == "off")) {
      cmd.verb = tokens[1] == "on" ? Verb::kMuteOn : Verb::kMuteOff;
    } else if (verb == "mute") {
      cmd.ok = false;
      cmd.error = "usage: mute on|off";
      return cmd;
    } else if (verb == "bed" && tokens.size() == 2 &&
               (tokens[1] == "on" || tokens[1] == "off")) {
      cmd.verb = tokens[1] == "on" ? Verb::kBedOn : Verb::kBedOff;
    } else if (verb == "bed" && tokens.size() == 3 && tokens[1] == "master") {
      if (!parse_float(tokens[2], cmd.number)) {
        cmd.ok = false;
        cmd.error = "bed master: not a number: " + tokens[2];
        return cmd;
      }
      // Same range as the F2 master slider; the top stays below 0 dB both as a
      // loudness guard and because 0 in the proto means "default".
      cmd.number = clamp(cmd.number, -60.f, -6.f);
      cmd.verb = Verb::kBedMaster;
    } else if (verb == "bed" && tokens.size() == 3 && tokens[1] == "layer" &&
               tokens[2] == "add") {
      cmd.verb = Verb::kBedLayerAdd;
    } else if (verb == "bed" && tokens.size() == 4 && tokens[1] == "layer" &&
               tokens[2] == "remove") {
      if (!parse_index(tokens[3], cmd.index)) {
        cmd.ok = false;
        cmd.error = "bed layer remove: not a layer index: " + tokens[3];
        return cmd;
      }
      cmd.verb = Verb::kBedLayerRemove;
    } else if (verb == "bed" && tokens.size() == 5 && tokens[1] == "layer") {
      if (!parse_index(tokens[2], cmd.index)) {
        cmd.ok = false;
        cmd.error = "bed layer: not a layer index: " + tokens[2];
        return cmd;
      }
      const std::string& field = tokens[3];
      if (!parse_float(tokens[4], cmd.number)) {
        cmd.ok = false;
        cmd.error = "bed layer " + field + ": not a number: " + tokens[4];
        return cmd;
      }
      // Clamped to the F2 sliders' ranges so both surfaces accept the same envelope.
      if (field == "carrier") {
        cmd.number = clamp(cmd.number, 20.f, 1000.f);
      } else if (field == "binaural") {
        cmd.number = clamp(cmd.number, 0.f, 40.f);
      } else if (field == "pulse") {
        cmd.number = clamp(cmd.number, 0.f, 40.f);
      } else if (field == "level") {
        cmd.number = clamp(cmd.number, -24.f, 0.f);
      } else {
        cmd.ok = false;
        cmd.error = "bed layer: unknown field: " + field +
            " (carrier|binaural|pulse|level)";
        return cmd;
      }
      cmd.value = field;
      cmd.verb = Verb::kBedLayerSet;
    } else if (verb == "bed") {
      cmd.ok = false;
      cmd.error = "usage: bed on|off | bed master DB | bed layer add | "
                  "bed layer remove I | bed layer I carrier|binaural|pulse|level VALUE";
      return cmd;
    } else {
      cmd.ok = false;
      cmd.error = "unknown verb: " + verb;
      return cmd;
    }
    return cmd;
  }
}
