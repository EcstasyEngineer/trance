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
         "One-line status of the running trance instance (playing/paused, current "
         "visual, overlay, hidden, bed, themes).",
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
         "Load and pin a v3 pattern from a source file on this machine (replaces the "
         "playing visual).",
         obj_schema({{"file", {{"type", "string"}, {"description", "pattern source path"}}}},
                    {"file"}),
         [](const json& args) { return "load pattern " + str_arg(args, "file"); }},
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
        {"bed_master", "Set the bed's master level in dB (clamped to the F2 slider range).",
         obj_schema({{"db", {{"type", "number"}, {"description", "master level, dB"}}}},
                    {"db"}),
         [](const json& args) { return "bed master " + num_str(num_arg(args, "db")); }},
        {"bed_layer_add", "Append a new entrainment layer to the active program's bed.",
         obj_schema(), [](const json&) { return std::string{"bed layer add"}; }},
        {"bed_layer_remove", "Remove the entrainment layer at the given index.",
         obj_schema({{"index", {{"type", "integer"}, {"description", "layer index, 0-based"}}}},
                    {"index"}),
         [](const json& args) {
           return "bed layer remove " + std::to_string(int_arg(args, "index"));
         }},
        {"bed_layer_set",
         "Set one field of one entrainment layer; the reconfigure morphs live rather "
         "than cutting.",
         obj_schema({{"index", {{"type", "integer"}, {"description", "layer index, 0-based"}}},
                     {"field",
                      {{"type", "string"},
                       {"enum", {"carrier", "binaural", "pulse", "level"}},
                       {"description", "carrier Hz | binaural Hz | pulse Hz | level dB"}}},
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
