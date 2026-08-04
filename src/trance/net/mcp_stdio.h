#ifndef TRANCE_SRC_TRANCE_NET_MCP_STDIO_H
#define TRANCE_SRC_TRANCE_NET_MCP_STDIO_H
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// MCP (Model Context Protocol) served over the process's OWN stdin/stdout -- newline-
// delimited JSON-RPC 2.0, the MCP stdio transport. The host (Claude Desktop, Claude
// Code, any MCP client) launches `trance --mcp ...` directly and owns the process;
// there is no sidecar, no bridge, no Python, and no socket.
//
// Same threading discipline as CommandChannel (docs/spec-mcp-ambient-daemon.md sec 2),
// because it IS the same mailbox shape: the reader thread answers pure-protocol methods
// (initialize / tools/list / ping) by itself -- they touch no app state -- and translates
// each tools/call 1:1 into a command-channel verb LINE (spec sec 4) pushed into the
// mutex-guarded queue. The render thread drains once per frame, executes against
// Director/Audio exactly as it does for socket commands, and hands the "ok ..."/"err ..."
// reply line back through reply(), which formats it as the JSON-RPC tool result.
//
// stdout belongs to the transport. main() redirects std::cout onto std::cerr's buffer in
// --mcp mode before anything logs, and this class writes JSON-RPC frames to the C stdout
// stream directly (unaffected by the std::cout redirect), serialized by a write mutex --
// the reader thread and the render thread both emit frames.
class McpStdio
{
public:
  McpStdio();
  ~McpStdio();

  struct Command {
    // Key into the pending-request map (the JSON-RPC id itself can be a string or a
    // number, so the raw token is kept internally and matched back up in reply()).
    std::uint64_t key;
    // A command-channel verb line, exactly what CommandChannel would have received.
    std::string line;
  };
  // Render-thread only: everything queued since the last call.
  std::vector<Command> drain();
  // Render-thread only: format `line` ("ok ..." / "err ...") as the tools/call result
  // for the pending request `key` and write the JSON-RPC response frame.
  void reply(std::uint64_t key, const std::string& line);
  // True once the host has closed our stdin -- the MCP convention for "server should
  // exit". The main loop treats it as a quit request.
  bool eof() const;

private:
  void reader_loop();

  std::mutex _mutex;                 // guards _queue and _pending_ids
  std::vector<Command> _queue;
  // key -> raw JSON id token (already-serialized), so reply() can echo string ids and
  // number ids alike without this header needing a JSON type.
  std::unordered_map<std::uint64_t, std::string> _pending_ids;
  std::uint64_t _next_key = 1;

  std::mutex _write_mutex;           // serializes stdout frames across threads
  std::atomic<bool> _running{true};
  std::atomic<bool> _eof{false};
  std::thread _reader;
};

#endif
