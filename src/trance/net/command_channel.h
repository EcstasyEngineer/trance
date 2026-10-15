#ifndef TRANCE_SRC_TRANCE_NET_COMMAND_CHANNEL_H
#define TRANCE_SRC_TRANCE_NET_COMMAND_CHANNEL_H
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

// In-process localhost command channel (docs/spec-mcp-ambient-daemon.md).
//
// Threading invariant (load-bearing, spec sec 2): the reader thread ONLY ever pushes raw
// lines into the mutex-guarded queue below. It never touches Director/Audio/anything else
// that isn't its own socket state. The render thread (main.cpp's per-frame loop) is the
// only thing that calls drain()/reply() and the only thing that dispatches a Command's line
// into a verb + executes it. This mirrors the ThemeBank async-loader discipline: the worker
// side (reader_loop here, async_update there) touches only its own state; the owning thread
// performs every mutation.
class CommandChannel
{
public:
  // Binds 127.0.0.1:port and spawns the reader thread. Loopback only, no auth -- see spec
  // sec 2/9. Throws std::runtime_error if the bind/listen fails.
  explicit CommandChannel(uint16_t port);
  ~CommandChannel();

  CommandChannel(const CommandChannel&) = delete;
  CommandChannel& operator=(const CommandChannel&) = delete;

  struct Command {
    std::uint64_t conn_id;
    std::string line;
  };

  // Render-thread only: move out everything received since the last call.
  std::vector<Command> drain();
  // Render-thread only: reply on the same connection (one line back per command, newline-
  // terminated on the wire). A conn_id that has already disconnected is a silent no-op --
  // the client is gone, there's nothing to reply to.
  void reply(std::uint64_t conn_id, const std::string& line);

private:
  // Pass the listen socket as uintptr_t, not int: Win64 SOCKET is pointer-sized and
  // truncates through int; POSIX fds round-trip losslessly.
  void reader_loop(std::uintptr_t listen_fd);

  std::mutex _mutex;
  std::vector<Command> _queue;  // guarded by _mutex
  std::atomic<bool> _running;
  std::thread _reader;

  // Per-connection state, guarded by _mutex (replies can arrive from the render thread while
  // the reader thread is still reading more lines from the same connection). MULTIPLE
  // connections are served concurrently -- the reader polls the listen socket and every live
  // connection in one wait, so an idle or slow controller cannot stall another. This was
  // originally "one accepted connection served to EOF before the next accept", which passed
  // every single-client test and then silently ignored a second controller: it connects (the
  // OS completes the handshake from the listen backlog) and its lines are read only once the
  // first client hangs up, which reads as a hung channel rather than a queued one (#29).
  struct Connection;
  std::vector<Connection>* _connections;  // pimpl-lite: keeps socket types out of the header
};

#endif
