#include <trance/net/command_channel.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
// Winsock path: written to the same reader_loop contract as the POSIX path below, but
// UNVALIDATED -- this repo's only build/test box this wave is Linux (see CLAUDE.md build
// gate). Compile-guarded so it never affects the Linux build; a Windows dev bringing up
// --command_port is expected to shake this out.
#pragma warning(push, 0)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma warning(pop)
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

namespace
{
  void close_socket(socket_t fd)
  {
#if defined(_WIN32)
    closesocket(fd);
#else
    ::close(fd);
#endif
  }

  // Wait until `fd` is readable or ~200ms passes. Returns true iff readable. The reader
  // thread calls this before every accept()/recv() so it re-checks _running on a bounded
  // cadence -- a bare blocking accept()/recv() would make ~CommandChannel's join() hang
  // forever when shutdown races an idle socket (observed: destructor deadlock).
  bool poll_readable(socket_t fd)
  {
#if defined(_WIN32)
    WSAPOLLFD p{fd, POLLRDNORM, 0};
    return WSAPoll(&p, 1, 200) > 0 && (p.revents & POLLRDNORM);
#else
    pollfd p{fd, POLLIN, 0};
    return ::poll(&p, 1, 200) > 0 && (p.revents & POLLIN);
#endif
  }

  std::string socket_last_error_message()
  {
#if defined(_WIN32)
    return std::system_category().message(WSAGetLastError());
#else
    return std::generic_category().message(errno);
#endif
  }
}

struct CommandChannel::Connection {
  std::uint64_t id;
  socket_t fd;
};

CommandChannel::CommandChannel(uint16_t port) : _running{true}
{
  _connections = new std::vector<Connection>();

#if defined(_WIN32)
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    delete _connections;
    throw std::runtime_error("CommandChannel: WSAStartup failed");
  }
#endif

  socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == kInvalidSocket) {
    delete _connections;
    throw std::runtime_error("CommandChannel: socket() failed");
  }

  int reuse = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
             sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // Loopback only -- the spec's entire trust boundary (sec 2/9): 127.0.0.1, never 0.0.0.0.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    const auto error = socket_last_error_message();
    close_socket(listen_fd);
    delete _connections;
    throw std::runtime_error("CommandChannel: bind(127.0.0.1:" + std::to_string(port) +
                              ") failed: " + error);
  }
  if (listen(listen_fd, 8) != 0) {
    close_socket(listen_fd);
    delete _connections;
    throw std::runtime_error("CommandChannel: listen() failed");
  }

  _reader = std::thread{[this, listen_fd] { reader_loop(std::uintptr_t(listen_fd)); }};
}

CommandChannel::~CommandChannel()
{
  _running = false;
  // reader_loop never blocks indefinitely: every accept()/recv() is gated by a ~200ms
  // poll_readable() tick that re-checks _running, so the join below is bounded. (The
  // original bare-blocking version deadlocked here whenever shutdown raced an idle
  // accept/recv -- the listen fd lives on reader_loop's stack and cannot be closed from
  // this thread to break the block.)
  if (_reader.joinable()) {
    _reader.join();
  }
  {
    std::lock_guard<std::mutex> lock{_mutex};
    for (auto& conn : *_connections) {
      close_socket(conn.fd);
    }
  }
  delete _connections;
#if defined(_WIN32)
  WSACleanup();
#endif
}

std::vector<CommandChannel::Command> CommandChannel::drain()
{
  std::lock_guard<std::mutex> lock{_mutex};
  std::vector<Command> out;
  out.swap(_queue);
  return out;
}

void CommandChannel::reply(std::uint64_t conn_id, const std::string& line)
{
  socket_t fd = kInvalidSocket;
  {
    std::lock_guard<std::mutex> lock{_mutex};
    for (const auto& conn : *_connections) {
      if (conn.id == conn_id) {
        fd = conn.fd;
        break;
      }
    }
  }
  if (fd == kInvalidSocket) {
    // Connection already gone -- silent no-op (see header comment).
    return;
  }
  std::string wire = line + "\n";
  send(fd, wire.data(),
#if defined(_WIN32)
       int(wire.size()),
#else
       wire.size(),
#endif
       0);
}

void CommandChannel::reader_loop(std::uintptr_t listen_fd_raw)
{
  socket_t listen_fd = socket_t(listen_fd_raw);
  std::uint64_t next_conn_id = 1;

  // v0 concurrency model: single accepted connection served fully (read-to-EOF) before the
  // next accept, matching the spec's line-in/line-out shape and the netcat/pytest test
  // clients in sec 6 -- nothing in the spec asks for concurrent controllers. accept() and
  // recv() both block only until _running flips or the peer disconnects/errors, so shutdown
  // is bounded by at most one in-flight syscall.
  while (_running) {
    if (!poll_readable(listen_fd)) {
      continue;  // timeout tick: re-check _running (bounded-shutdown invariant)
    }
    sockaddr_in peer{};
#if defined(_WIN32)
    int peer_len = sizeof(peer);
#else
    socklen_t peer_len = sizeof(peer);
#endif
    socket_t conn_fd =
        accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (!_running) {
      if (conn_fd != kInvalidSocket) {
        close_socket(conn_fd);
      }
      break;
    }
    if (conn_fd == kInvalidSocket) {
      continue;
    }

    std::uint64_t conn_id = next_conn_id++;
    {
      std::lock_guard<std::mutex> lock{_mutex};
      _connections->push_back(Connection{conn_id, conn_fd});
    }

    std::string buffer;
    char chunk[512];
    while (_running) {
      if (!poll_readable(conn_fd)) {
        continue;  // timeout tick: re-check _running (idle client must not pin shutdown)
      }
      auto n = recv(conn_fd, chunk,
#if defined(_WIN32)
                    int(sizeof(chunk)),
#else
                    sizeof(chunk),
#endif
                    0);
      if (n <= 0) {
        break;  // peer closed or error -- either way, this connection is done.
      }
      buffer.append(chunk, std::size_t(n));

      // Drain every complete newline-terminated line in the buffer; a partial trailing
      // line waits for more bytes. CR is stripped so `nc`/telnet-style CRLF clients work
      // without a special case.
      std::size_t pos;
      while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        std::lock_guard<std::mutex> lock{_mutex};
        _queue.push_back(Command{conn_id, std::move(line)});
      }
    }

    close_socket(conn_fd);
    {
      std::lock_guard<std::mutex> lock{_mutex};
      auto& conns = *_connections;
      conns.erase(std::remove_if(conns.begin(), conns.end(),
                                 [conn_id](const Connection& c) { return c.id == conn_id; }),
                 conns.end());
    }
  }

  close_socket(listen_fd);
}
