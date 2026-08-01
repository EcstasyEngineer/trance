// Protocol test for the command channel (docs/spec-mcp-ambient-daemon.md sec 3/4): pure
// line -> Verb parsing, no socket/Director/Audio dependency (see command_protocol.h's header
// comment for why that separation matters -- verb EXECUTION is command_channel.cpp/main.cpp's
// job, not this pure function's).
//
// Headless: no SFML/protobuf/sockets. Run via ctest.
#include <trance/net/command_protocol.h>

#include <cmath>
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

  using command_protocol::parse_command;
  using command_protocol::Verb;
}

int main()
{
  // Lifecycle verbs (spec sec 4): bare, no args. `start`/`stop` were byte-identical
  // aliases of these two and are gone; they must now read as unknown verbs, not as
  // silently-accepted synonyms.
  {
    auto c = parse_command("pause");
    check(c.ok && c.verb == Verb::kPause, "'pause' parses");
  }
  {
    auto c = parse_command("resume");
    check(c.ok && c.verb == Verb::kResume, "'resume' parses");
  }
  {
    auto c = parse_command("pause extra");
    check(!c.ok, "'pause extra' rejects trailing args");
  }
  {
    auto c = parse_command("start");
    check(!c.ok, "retired 'start' alias is an unknown verb");
  }
  {
    auto c = parse_command("stop");
    check(!c.ok, "retired 'stop' alias is an unknown verb");
  }

  // Overlay verbs.
  {
    auto c = parse_command("overlay on");
    check(c.ok && c.verb == Verb::kOverlayOn, "'overlay on' parses");
  }
  {
    auto c = parse_command("overlay off");
    check(c.ok && c.verb == Verb::kOverlayOff, "'overlay off' parses");
  }
  {
    auto c = parse_command("overlay opacity 0.6");
    check(c.ok && c.verb == Verb::kOverlayOpacity && std::abs(c.number - 0.6f) < 1e-4f,
         "'overlay opacity 0.6' parses value");
  }
  {
    // Spec sec 4: VALUE in 0..1, clamped -- not rejected.
    auto c = parse_command("overlay opacity 4.2");
    check(c.ok && c.verb == Verb::kOverlayOpacity && c.number == 1.f,
         "'overlay opacity 4.2' clamps to 1");
  }
  {
    auto c = parse_command("overlay opacity -1");
    check(c.ok && c.verb == Verb::kOverlayOpacity && c.number == 0.f,
         "'overlay opacity -1' clamps to 0");
  }
  {
    auto c = parse_command("overlay opacity notanumber");
    check(!c.ok, "'overlay opacity notanumber' errors");
  }
  {
    auto c = parse_command("overlay sideways");
    check(!c.ok, "'overlay sideways' errors (unknown overlay sub-verb)");
  }

  // Retired verbs. `intensity` acked ok and wrote a field nothing read; `set`/`get`
  // answered "unknown key" for every key; `load session` answered "not yet supported".
  // All four now fail at the parser, which is the honest reply -- an `ok` for a verb with
  // no consumer is worse than an error.
  {
    auto c = parse_command("intensity 0.6");
    check(!c.ok, "retired 'intensity' is an unknown verb");
  }
  {
    auto c = parse_command("set fps 30");
    check(!c.ok, "retired 'set' is an unknown verb");
  }
  {
    auto c = parse_command("get fps");
    check(!c.ok, "retired 'get' is an unknown verb");
  }

  // Loading.
  {
    auto c = parse_command("load pattern foo.v3p");
    check(c.ok && c.verb == Verb::kLoadPattern && c.value == "foo.v3p",
         "'load pattern foo.v3p' parses path");
  }
  {
    auto c = parse_command("load session foo.session");
    check(!c.ok, "retired 'load session' errors (only 'load pattern' remains)");
  }
  {
    auto c = parse_command("load nonsense foo");
    check(!c.ok, "'load nonsense foo' errors (unknown load sub-verb)");
  }

  // Status.
  {
    auto c = parse_command("status");
    check(c.ok && c.verb == Verb::kStatus, "'status' parses");
  }

  // Unknown verb / malformed lines -- spec sec 3: "never a crash, never a dropped connection".
  {
    auto c = parse_command("frobnicate");
    check(!c.ok && c.error == "unknown verb: frobnicate", "'frobnicate' -> exact spec error text");
  }
  {
    auto c = parse_command("");
    check(!c.ok, "empty line errors instead of hanging silently");
  }
  {
    auto c = parse_command("   ");
    check(!c.ok, "whitespace-only line errors");
  }

  // Reply formatting -- spec sec 3 grammar: "ok[ <space-separated key=value pairs>]" / "err
  // <message>".
  check(command_protocol::format_ok() == "ok", "format_ok() with no body");
  check(command_protocol::format_ok("fps=30") == "ok fps=30", "format_ok(body) prepends 'ok '");
  check(command_protocol::format_err("unknown verb: x") == "err unknown verb: x",
       "format_err(msg) prepends 'err '");

  // ui / screenshot -- the debug/validation verbs.
  {
    auto c = command_protocol::parse_command("ui on");
    check(c.ok && c.verb == Verb::kUiOn, "'ui on' parses");
    c = command_protocol::parse_command("ui off");
    check(c.ok && c.verb == Verb::kUiOff, "'ui off' parses");
    c = command_protocol::parse_command("ui");
    check(!c.ok, "'ui' without on|off rejects with usage");
    c = command_protocol::parse_command("ui sideways");
    check(!c.ok, "'ui sideways' rejects");
    c = command_protocol::parse_command("screenshot /tmp/x.png");
    check(c.ok && c.verb == Verb::kScreenshot && c.value == "/tmp/x.png",
          "'screenshot PATH' parses with the path in value");
    c = command_protocol::parse_command("screenshot");
    check(!c.ok, "'screenshot' without a path rejects with usage");
    c = command_protocol::parse_command("screenshot a b");
    check(!c.ok, "'screenshot' with extra args rejects");
  }

  // clamp01 -- the overlay-opacity clamp.
  check(command_protocol::clamp01(-5.f) == 0.f, "clamp01(-5) == 0");
  check(command_protocol::clamp01(5.f) == 1.f, "clamp01(5) == 1");
  check(command_protocol::clamp01(0.4f) == 0.4f, "clamp01(0.4) == 0.4");

  std::cout << "\n" << (g_fail ? "FAILED: " : "all command protocol checks passed (")
            << g_fail << (g_fail ? " failed\n" : " failures)\n");
  return g_fail ? 1 : 0;
}
