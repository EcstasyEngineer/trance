#include <trance/net/command_channel.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
// Winsock path: the same reader_loop contract as the POSIX path below. QA'd on Windows
// 2026-08-12 (#29) end to end -- every verb over a real socket, malformed lines, two
// concurrent clients, and a window-close exit with a client still connected.
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

#if defined(_WIN32)
  using poll_fd = WSAPOLLFD;
  constexpr short kPollRead = POLLRDNORM;
#else
  using poll_fd = pollfd;
  constexpr short kPollRead = POLLIN;
#endif

  // Wait until any of `fds` is readable or ~200ms passes. The 200ms tick is load-bearing:
  // it is what lets the reader thread re-check _running on a bounded cadence, keeping
  // ~CommandChannel's join() bounded -- a bare blocking accept()/recv() could not be
  // interrupted from another thread.
  int poll_readable(poll_fd* fds, std::size_t count)
  {
#if defined(_WIN32)
    return WSAPoll(fds, ULONG(count), 200);
#else
    return ::poll(fds, nfds_t(count), 200);
#endif
  }

  // POLLHUP/POLLERR count as "readable": when the peer disconnects, WSAPoll reports POLLHUP
  // on the dead socket WITHOUT POLLRDNORM, so testing POLLRDNORM alone never fires, recv()
  // never runs to observe the EOF, and the reader ticks on the corpse forever. (Linux poll()
  // reports EOF as POLLIN, which is why the POSIX path never showed it; both paths treat
  // hangup/error as readable so recv() can return 0/-1 and the loop can retire the socket.)
  bool poll_hit(const poll_fd& p)
  {
    return (p.revents & (kPollRead | POLLHUP | POLLERR)) != 0;
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
  // Bytes received on this connection that do not yet end in a newline. Per-connection
  // because two clients interleave freely now: one shared buffer would splice half a line
  // from one controller onto half a line from another.
  std::string buffer;
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
  // listen fd lives on reader_loop's stack and cannot be closed from this thread to
  // break a blocking call -- the poll gate is what makes shutdown safe.)
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

  // Concurrency model: the listen socket and every live connection are polled in ONE wait,
  // so any number of controllers are served together (#29's "two simultaneous client
  // connections both get replies"). A slow or idle client costs nothing but a slot in the
  // poll set; it cannot delay another client's line or the ~200ms shutdown tick.
  //
  // Only this thread mutates _connections, so it can read the vector without the lock while
  // it works; it takes the lock to add or retire an entry, which is all reply() needs to see
  // a consistent list.
  std::vector<poll_fd> fds;
  std::vector<socket_t> polled;
  char chunk[512];
  while (_running) {
    fds.clear();
    polled.clear();
    fds.push_back(poll_fd{listen_fd, kPollRead, 0});
    for (const auto& conn : *_connections) {
      fds.push_back(poll_fd{conn.fd, kPollRead, 0});
      polled.push_back(conn.fd);
    }
    if (poll_readable(fds.data(), fds.size()) <= 0) {
      continue;  // timeout tick: re-check _running (bounded-shutdown invariant)
    }

    if (poll_hit(fds.front())) {
      sockaddr_in peer{};
#if defined(_WIN32)
      int peer_len = sizeof(peer);
#else
      socklen_t peer_len = sizeof(peer);
#endif
      socket_t conn_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
      if (!_running) {
        if (conn_fd != kInvalidSocket) {
          close_socket(conn_fd);
        }
        break;
      }
      if (conn_fd != kInvalidSocket) {
        std::lock_guard<std::mutex> lock{_mutex};
        _connections->push_back(Connection{next_conn_id++, conn_fd, {}});
      }
    }

    // Readable connections, matched back by fd: the poll set was built from a snapshot, and
    // an accept above may already have appended to _connections, so index-into-the-vector
    // would be reading the wrong entry.
    for (std::size_t i = 0; i < polled.size(); ++i) {
      if (!poll_hit(fds[i + 1])) {
        continue;
      }
      auto it = std::find_if(_connections->begin(), _connections->end(),
                             [fd = polled[i]](const Connection& c) { return c.fd == fd; });
      if (it == _connections->end()) {
        continue;
      }
      auto n = recv(it->fd, chunk,
#if defined(_WIN32)
                    int(sizeof(chunk)),
#else
                    sizeof(chunk),
#endif
                    0);
      if (n <= 0) {
        // Peer closed or errored -- either way this connection is done. Closed BEFORE it is
        // erased, and erased under the lock, so reply() either finds a live fd or finds
        // nothing; it can never be handed a recycled one.
        close_socket(it->fd);
        std::lock_guard<std::mutex> lock{_mutex};
        _connections->erase(it);
        continue;
      }
      it->buffer.append(chunk, std::size_t(n));

      // Drain every complete newline-terminated line; a partial trailing line waits for more
      // bytes. CR is stripped so `nc`/telnet-style CRLF clients work without a special case.
      std::size_t pos;
      while ((pos = it->buffer.find('\n')) != std::string::npos) {
        std::string line = it->buffer.substr(0, pos);
        it->buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        std::lock_guard<std::mutex> lock{_mutex};
        _queue.push_back(Command{it->id, std::move(line)});
      }
    }
  }

  close_socket(listen_fd);
}
