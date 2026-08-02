#ifndef TRANCE_SRC_TRANCE_NET_COMMAND_PROTOCOL_H
#define TRANCE_SRC_TRANCE_NET_COMMAND_PROTOCOL_H
#include <string>

// Pure protocol layer for the command channel (docs/spec-mcp-ambient-daemon.md sec 3/4):
// line -> Verb struct, and the couple of pure formatting helpers for reply lines. Deliberately
// has ZERO dependency on sockets, Director, Audio, or any runtime state -- keeping it separate
// from verb EXECUTION, which lives in main.cpp's per-frame drain/dispatch (execute_command /
// handle_commands) on the render thread. QA'd end-to-end over a real socket by
// tests/qa_command_channel.py against a live exe (#29).
namespace command_protocol
{
  enum class Verb {
    kUnknown,
    // Playback. `pause`/`resume`, not `start`/`stop`: the runtime only ever freezes and
    // unfreezes (program state is retained, nothing is torn down or reloaded), so those
    // are the names that describe what actually happens -- `start`'s "begin from a stopped
    // state" and `stop`'s "return to idle" dispatched to the same two lines as these.
    kPause,
    kResume,
    kOverlayOn,
    kOverlayOff,
    kOverlayOpacity,
    kLoadPattern,
    kStatus,
    // Hide-everything / silent running (spec sec 4): window invisible + playback paused +
    // audio muted, process alive. Idempotent -- `hide` while hidden and `show` while shown
    // are both `ok` no-ops. Same state Shift+F11 and the tray's Hide/Show item toggle.
    kHide,
    kShow,
    // Debug/validation verbs (not in the ambient-daemon spec's settings surface, but shaped
    // the same): toggle the F2 ImGui panels remotely and dump the next rendered frame to a
    // PNG -- together they make headless/display-locked validation possible (a controller
    // can SEE what the app is drawing without keyboard access to the window).
    kUiOn,
    kUiOff,
    kScreenshot,
  };

  struct ParsedCommand {
    Verb verb = Verb::kUnknown;
    // Populated depending on verb: kOverlayOpacity -> number; kLoadPattern/kScreenshot ->
    // value (the file path).
    std::string value;
    float number = 0.f;
    // Set when the line is malformed (wrong arg count, non-numeric where a number is
    // required, or an unrecognised verb token). `error` carries what to put after "err ".
    bool ok = true;
    std::string error;
  };

  // Parses one input line (no trailing newline expected either way -- callers strip it) into
  // a ParsedCommand. Never throws; a malformed or unknown line comes back with ok=false and
  // `error` set to what the "err ..." reply body should say (spec sec 3: "unknown verb:
  // frobnicate" etc). Whitespace-only / empty lines are also reported as an error rather than
  // silently ignored -- the channel always replies exactly once per line received (sec 3).
  ParsedCommand parse_command(const std::string& line);

  // Formats the "ok ..." / "err ..." reply line for a parse failure. Convenience so
  // command_channel.cpp and the test share one grammar for the failure shape.
  std::string format_err(const std::string& message);
  std::string format_ok(const std::string& body = {});

  // Clamps to [0, 1] for `overlay opacity VALUE` (spec sec 4: "VALUE in 0..1, clamped").
  float clamp01(float value);
}

#endif
