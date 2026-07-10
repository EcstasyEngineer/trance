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

    if (verb == "start" && tokens.size() == 1) {
      cmd.verb = Verb::kStart;
    } else if (verb == "stop" && tokens.size() == 1) {
      cmd.verb = Verb::kStop;
    } else if (verb == "pause" && tokens.size() == 1) {
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
    } else if (verb == "intensity" && tokens.size() == 2) {
      if (!parse_float(tokens[1], cmd.number)) {
        cmd.ok = false;
        cmd.error = "intensity: not a number: " + tokens[1];
        return cmd;
      }
      cmd.number = clamp01(cmd.number);
      cmd.verb = Verb::kIntensity;
    } else if (verb == "set" && tokens.size() == 3) {
      cmd.verb = Verb::kSet;
      cmd.key = tokens[1];
      cmd.value = tokens[2];
    } else if (verb == "get" && tokens.size() == 2) {
      cmd.verb = Verb::kGet;
      cmd.key = tokens[1];
    } else if (verb == "load" && tokens.size() == 3 && tokens[1] == "pattern") {
      cmd.verb = Verb::kLoadPattern;
      cmd.value = tokens[2];
    } else if (verb == "load" && tokens.size() == 3 && tokens[1] == "session") {
      cmd.verb = Verb::kLoadSession;
      cmd.value = tokens[2];
    } else if (verb == "load") {
      cmd.ok = false;
      cmd.error = "usage: load pattern|session FILE";
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
    } else {
      cmd.ok = false;
      cmd.error = "unknown verb: " + verb;
      return cmd;
    }
    return cmd;
  }
}
