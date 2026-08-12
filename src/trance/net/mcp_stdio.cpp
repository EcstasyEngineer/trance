#include <trance/net/mcp_stdio.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>

#pragma warning(push, 0)
#include <nlohmann/json.hpp>
#pragma warning(pop)

namespace
{
  using nlohmann::json;

  const char* kProtocolVersionFallback = "2024-11-05";
  const char* kServerVersion = "0.4.0";

  json obj_schema(json properties = json::object(), json required = json::array())
  {
    json schema{{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) {
      schema["required"] = std::move(required);
    }
    return schema;
  }

  // Typed argument readers for the line builders below. Throwing keeps the builders
  // one-liners; the tools/call handler catches and turns it into an isError result.
  double num_arg(const json& args, const char* name)
  {
    if (!args.contains(name) || !args[name].is_number()) {
      throw std::runtime_error(std::string{"missing or non-numeric argument: "} + name);
    }
    return args[name].get<double>();
  }

  long int_arg(const json& args, const char* name)
  {
    if (!args.contains(name) || !args[name].is_number_integer()) {
      throw std::runtime_error(std::string{"missing or non-integer argument: "} + name);
    }
    return args[name].get<long>();
  }

  std::string str_arg(const json& args, const char* name)
  {
    if (!args.contains(name) || !args[name].is_string()) {
      throw std::runtime_error(std::string{"missing or non-string argument: "} + name);
    }
    return args[name].get<std::string>();
  }

  // json-formatted numerics ("0.5", "-30") rather than std::to_string ("0.500000"):
  // the verb parser reads either, but the compact form is what a human sees in logs.
  std::string num_str(double value)
  {
    return json(value).dump();
  }

  // One tool per verb, exactly as the spec's MCP section frames the mapping
  // (docs/spec-mcp-ambient-daemon.md sec 8): a tool call IS a verb line; on the
  // execute side it is indistinguishable from a socket client.
  struct Tool {
    const char* name;
    const char* description;
    json schema;
    std::string (*build)(const json& args);
  };

  const std::vector<Tool>& tools()
  {
    static const std::vector<Tool> table = {
        {"status",
         "One-line status of the running trance instance: current visual, bed on/off, "
         "muted, overlay, hidden, uptime, headset (xr), the four theme queue slots "
         "(prev|primary|secondary|next), and whether a theme or text pin is in effect. "
         "Cheap and side-effect-free -- call it before anything else to see where you are.",
         obj_schema(), [](const json&) { return std::string{"status"}; }},
        {"pause", "Freeze playback in place (program state retained; the frame stays up).",
         obj_schema(), [](const json&) { return std::string{"pause"}; }},
        {"resume", "Unfreeze playback after a pause.", obj_schema(),
         [](const json&) { return std::string{"resume"}; }},
        {"hide",
         "Silent running: window invisible, playback paused, audio muted, process "
         "alive. Idempotent. The instant off-switch for 'screen share starting' / "
         "'someone walked in'.",
         obj_schema(), [](const json&) { return std::string{"hide"}; }},
        {"show", "Restore from hide (idempotent).", obj_schema(),
         [](const json&) { return std::string{"show"}; }},
        {"overlay_on", "Switch the click-through overlay mode on for the running window.",
         obj_schema(), [](const json&) { return std::string{"overlay on"}; }},
        {"overlay_off", "Switch the click-through overlay mode off.", obj_schema(),
         [](const json&) { return std::string{"overlay off"}; }},
        {"overlay_opacity", "Set overlay opacity, 0..1 (clamped by trance).",
         obj_schema({{"opacity", {{"type", "number"}, {"description", "0..1"}}}},
                    {"opacity"}),
         [](const json& args) { return "overlay opacity " + num_str(num_arg(args, "opacity")); }},
        {"load_pattern",
         "Load and pin a v3 pattern from a source file, resolved ON THE TRANCE MACHINE "
         "(replaces the playing visual until unload_pattern). If you did not put that file "
         "on that machine yourself, use load_pattern_source instead.",
         obj_schema({{"file", {{"type", "string"}, {"description", "pattern source path"}}}},
                    {"file"}),
         [](const json& args) { return "load pattern " + str_arg(args, "file"); }},
        {"load_pattern_source",
         "Load and pin a v3 pattern from source text sent over this connection -- no file "
         "on the trance machine needed. Newlines are ordinary whitespace to the grammar, so "
         "a one-line source is fine; write \\n where you need a real line break (only a `#` "
         "comment requires one). A parse error comes back as the parser's line:col "
         "diagnostic and the playing visual is left alone. Released by unload_pattern.",
         obj_schema({{"source", {{"type", "string"}, {"description", "v3 pattern source text"}}}},
                    {"source"}),
         [](const json& args) { return "load pattern source " + str_arg(args, "source"); }},
        {"unload_pattern",
         "Release whatever visual is pinned -- by load_pattern, load_pattern_source or "
         "visual -- and return to the program's own visual schedule. Idempotent.",
         obj_schema(), [](const json&) { return std::string{"unload pattern"}; }},
        {"themes",
         "List every theme in the session (the answer to 'what may I pin?'), as "
         "name:weight with markers: * pinned, + live on one of the two theme slots right "
         "now, ! nothing to draw so it cannot be pinned. weight is the program's rotation "
         "weight; 0 means it is not in the rotation but can still be pinned.",
         obj_schema(), [](const json&) { return std::string{"themes"}; }},
        {"theme_pin",
         "Hold the session on specific themes instead of letting them rotate. One name "
         "puts that theme on BOTH live theme slots; two names put one on each (the engine "
         "holds exactly two live themes, so two is the maximum). Takes a few seconds to "
         "become visible -- the bank has to load the theme in -- so do not read a "
         "screenshot taken immediately as a failed pin. Released by theme_unpin. This does "
         "not edit the session file.",
         obj_schema({{"names",
                      {{"type", "string"},
                       {"description", "one or two theme names, comma-separated"}}}},
                    {"names"}),
         [](const json& args) { return "theme pin " + str_arg(args, "names"); }},
        {"theme_unpin", "Release the theme pin: back to the program's own theme rotation.",
         obj_schema(), [](const json&) { return std::string{"theme unpin"}; }},
        {"visuals",
         "List what `visual` accepts: the built-in visuals and the active program's custom "
         "patterns, as name:weight with * marking the one playing now, plus whether a force "
         "is currently in effect (forced=yes means someone pinned it, not that the schedule "
         "chose it).",
         obj_schema(), [](const json&) { return std::string{"visuals"}; }},
        {"visual",
         "Force every visual selection to one built-in (accelerate, slow_flash, sub_text, "
         "flash_text, simple, super_parallel, animation, super_fast) or to one of the "
         "active program's custom patterns, by name. Themes, audio and the playlist are "
         "unaffected -- only the visual schedule. Released by unload_pattern.",
         obj_schema({{"name",
                      {{"type", "string"},
                       {"description", "built-in or custom pattern name; send `visuals` to list"}}}},
                    {"name"}),
         [](const json& args) { return "visual " + str_arg(args, "name"); }},
        {"text_pin",
         "Display exactly these words: every text draw (word/caption/subtext/line) serves "
         "from this list, round-robin, instead of the themes' own text pools. The words are "
         "yours, not a theme's, so both theme slots draw from the same list. Released by "
         "text_unpin. Note that a visual with no text draws in it shows none of this -- "
         "pair it with `visual sub_text` or `visual flash_text` if nothing appears.",
         obj_schema({{"words",
                      {{"type", "string"},
                       {"description", "comma-separated words or phrases"}}}},
                    {"words"}),
         [](const json& args) { return "text pin " + str_arg(args, "words"); }},
        {"text_unpin", "Release pinned text: back to drawing text from the themes.",
         obj_schema(), [](const json&) { return std::string{"text unpin"}; }},
        {"ui_on", "Open the in-app F2 control panel (debug/validation).", obj_schema(),
         [](const json&) { return std::string{"ui on"}; }},
        {"ui_off", "Close the in-app F2 control panel.", obj_schema(),
         [](const json&) { return std::string{"ui off"}; }},
        {"screenshot",
         "Dump the next rendered frame to a PNG on this machine -- lets a controller "
         "see what is being drawn without keyboard access to the window.",
         obj_schema({{"file", {{"type", "string"}, {"description", "output .png path"}}}},
                    {"file"}),
         [](const json& args) { return "screenshot " + str_arg(args, "file"); }},
        {"mute_on", "Global audio mute (same toggle as the M key).", obj_schema(),
         [](const json&) { return std::string{"mute on"}; }},
        {"mute_off", "Release the global audio mute.", obj_schema(),
         [](const json&) { return std::string{"mute off"}; }},
        {"bed_on",
         "Enable the entrainment bed (restores the program's default bed if it has none).",
         obj_schema(), [](const json&) { return std::string{"bed on"}; }},
        {"bed_off", "Disable the entrainment bed.", obj_schema(),
         [](const json&) { return std::string{"bed off"}; }},
        {"bed_master",
         "Set the bed's master level in dB, clamped to -60..-6. THIS is the bed's absolute "
         "volume control: the mix is normalised to this level, so a layer's own `level` "
         "cannot make the bed quieter (see bed_layer_set).",
         obj_schema({{"db", {{"type", "number"}, {"description", "master level, dB"}}}},
                    {"db"}),
         [](const json& args) { return "bed master " + num_str(num_arg(args, "db")); }},
        {"bed_layers",
         "Read the bed back: layer count, master level, and every layer's carrier/binaural/"
         "pulse/level. Read-only. Call it BEFORE bed_layer_remove or a level change -- "
         "nothing else can recover a layer's parameters afterwards.",
         obj_schema(), [](const json&) { return std::string{"bed layers"}; }},
        {"bed_layer_add",
         "Append a new entrainment layer (carrier 200 Hz, binaural 3 Hz, level -6 dB) to the "
         "active program's bed. Returns the layer count AFTER the append, as `layers=N`.",
         obj_schema(), [](const json&) { return std::string{"bed layer add"}; }},
        {"bed_layer_remove",
         "Remove the entrainment layer at the given index. Returns the layer count AFTER the "
         "removal, as `layers=N`. Read bed_layers first if you might want it back -- the "
         "parameters are gone once it is removed.",
         obj_schema({{"index", {{"type", "integer"}, {"description", "layer index, 0-based"}}}},
                    {"index"}),
         [](const json& args) {
           return "bed layer remove " + std::to_string(int_arg(args, "index"));
         }},
        {"bed_layer_set",
         "Set one field of one entrainment layer; the reconfigure morphs live rather "
         "than cutting. Two things worth knowing before you diagnose anything from what you "
         "hear: (1) every layer emits a binaural beat AND an independent isochronic pulse, "
         "and both are audible at once, so a layer with binaural 10 and pulse 4 is producing "
         "two perceived rates -- it is not a leftover layer that failed to tear down; "
         "(2) `level` is a RELATIVE balance between layers, not a volume: the bed normalises "
         "its summed level to the master, so turning the only layer down to -60 dB just "
         "raises the master gain to compensate and produces no silence. Use bed_master for "
         "absolute level, bed_off or mute_on for silence.",
         obj_schema({{"index", {{"type", "integer"}, {"description", "layer index, 0-based"}}},
                     {"field",
                      {{"type", "string"},
                       {"enum", {"carrier", "binaural", "pulse", "level"}},
                       {"description",
                        "carrier Hz (tone centre) | binaural Hz (L/R difference) | pulse Hz "
                        "(isochronic gate, independent of and simultaneous with binaural) | "
                        "level dB (relative balance between layers, not absolute volume)"}}},
                     {"value", {{"type", "number"}}}},
                    {"index", "field", "value"}),
         [](const json& args) {
           const std::string field = str_arg(args, "field");
           if (field != "carrier" && field != "binaural" && field != "pulse" &&
               field != "level") {
             throw std::runtime_error("field must be carrier|binaural|pulse|level");
           }
           return "bed layer " + std::to_string(int_arg(args, "index")) + " " + field + " " +
               num_str(num_arg(args, "value"));
         }},
    };
    return table;
  }

  json tool_result(const std::string& text, bool is_error)
  {
    return json{{"content", json::array({json{{"type", "text"}, {"text", text}}})},
                {"isError", is_error}};
  }
}

McpStdio::McpStdio()
{
  _reader = std::thread{[this] { reader_loop(); }};
}

McpStdio::~McpStdio()
{
  _running = false;
  // The reader blocks in getline(std::cin) with no portable way to interrupt it. The
  // NORMAL shutdown is host-driven -- the host closes our stdin, getline returns EOF,
  // the thread exits, and this join is immediate. The abnormal path (the app quitting
  // while the host still holds stdin open -- tray Quit, window close) detaches: the
  // thread is stuck in a read that only process exit can end, and process exit is
  // precisely what follows.
  if (_reader.joinable()) {
    if (_eof) {
      _reader.join();
    } else {
      _reader.detach();
    }
  }
}

std::vector<McpStdio::Command> McpStdio::drain()
{
  std::lock_guard<std::mutex> lock{_mutex};
  std::vector<Command> out;
  out.swap(_queue);
  return out;
}

bool McpStdio::eof() const
{
  return _eof;
}

namespace
{
  // Writes one JSON-RPC frame to the REAL stdout (the C stream is unaffected by main()'s
  // std::cout-to-cerr redirect). Callers hold the write mutex.
  void write_frame_locked(const json& message)
  {
    const std::string wire = message.dump() + "\n";
    // fwrite, not std::cout: stdout is the transport, and it must not pass through the
    // redirected C++ stream. (On Windows, stdout for a piped process is binary enough
    // for our purposes; a \r\n translation would still parse, but the JSON itself never
    // contains raw newlines because dump() escapes them.)
    std::fwrite(wire.data(), 1, wire.size(), stdout);
    std::fflush(stdout);
  }
}

void McpStdio::reply(std::uint64_t key, const std::string& line)
{
  std::string id_token;
  {
    std::lock_guard<std::mutex> lock{_mutex};
    auto it = _pending_ids.find(key);
    if (it == _pending_ids.end()) {
      return;
    }
    id_token = it->second;
    _pending_ids.erase(it);
  }
  // Channel grammar (spec sec 3): "ok[ body]" or "err body", exactly one reply per line.
  bool is_error = !(line == "ok" || line.rfind("ok ", 0) == 0);
  std::string body = line.rfind("ok ", 0) == 0 ? line.substr(3) : line;
  if (line == "ok" || (!is_error && body.empty())) {
    body = "ok";
  }
  json response{{"jsonrpc", "2.0"},
                {"id", json::parse(id_token)},
                {"result", tool_result(body, is_error)}};
  std::lock_guard<std::mutex> lock{_write_mutex};
  write_frame_locked(response);
}

void McpStdio::reader_loop()
{
  std::string raw;
  while (_running && std::getline(std::cin, raw)) {
    if (!raw.empty() && raw.back() == '\r') {
      raw.pop_back();
    }
    if (raw.empty()) {
      continue;
    }
    json message = json::parse(raw, nullptr, /*allow_exceptions=*/false);
    if (message.is_discarded()) {
      std::lock_guard<std::mutex> lock{_write_mutex};
      write_frame_locked(json{{"jsonrpc", "2.0"},
                              {"id", nullptr},
                              {"error", {{"code", -32700}, {"message", "parse error"}}}});
      continue;
    }
    const std::string method = message.value("method", "");
    const bool is_notification = !message.contains("id");
    const json id = message.contains("id") ? message["id"] : json{};

    if (method == "initialize") {
      // Echo the client's protocol version -- this server's surface (tools only) is a
      // subset every revision supports, so agreeing with the client is always safe.
      json params = message.value("params", json::object());
      json response{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"result",
           {{"protocolVersion", params.value("protocolVersion", kProtocolVersionFallback)},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "trance"}, {"version", kServerVersion}}}}}};
      std::lock_guard<std::mutex> lock{_write_mutex};
      write_frame_locked(response);
    } else if (method == "notifications/initialized") {
      // Handshake complete; nothing to do and (as a notification) nothing to send.
    } else if (method == "ping") {
      std::lock_guard<std::mutex> lock{_write_mutex};
      write_frame_locked(json{{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}});
    } else if (method == "tools/list") {
      json list = json::array();
      for (const auto& tool : tools()) {
        list.push_back(json{{"name", tool.name},
                            {"description", tool.description},
                            {"inputSchema", tool.schema}});
      }
      std::lock_guard<std::mutex> lock{_write_mutex};
      write_frame_locked(
          json{{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"tools", list}}}});
    } else if (method == "tools/call") {
      json params = message.value("params", json::object());
      const std::string name = params.value("name", "");
      const json args = params.value("arguments", json::object());
      const Tool* tool = nullptr;
      for (const auto& t : tools()) {
        if (name == t.name) {
          tool = &t;
          break;
        }
      }
      if (!tool) {
        std::lock_guard<std::mutex> lock{_write_mutex};
        write_frame_locked(json{{"jsonrpc", "2.0"},
                                {"id", id},
                                {"result", tool_result("unknown tool: " + name, true)}});
        continue;
      }
      std::string line;
      try {
        line = tool->build(args);
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock{_write_mutex};
        write_frame_locked(
            json{{"jsonrpc", "2.0"},
                 {"id", id},
                 {"result", tool_result("bad arguments for " + name + ": " + e.what(), true)}});
        continue;
      }
      // Queue for the render thread; the reply comes back through reply() with the id
      // matched up. This is the ONLY branch that touches app state, and it does so
      // exactly the way the socket channel does: by mailbox.
      {
        std::lock_guard<std::mutex> lock{_mutex};
        auto key = _next_key++;
        _pending_ids[key] = id.dump();
        _queue.push_back(Command{key, std::move(line)});
      }
    } else if (is_notification) {
      // Unknown notifications are ignored by JSON-RPC rule; never reply.
    } else {
      std::lock_guard<std::mutex> lock{_write_mutex};
      write_frame_locked(
          json{{"jsonrpc", "2.0"},
               {"id", id},
               {"error", {{"code", -32601}, {"message", "method not found: " + method}}}});
    }
  }
  _eof = true;
}
