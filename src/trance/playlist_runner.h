#ifndef TRANCE_SRC_TRANCE_PLAYLIST_RUNNER_H
#define TRANCE_SRC_TRANCE_PLAYLIST_RUNNER_H
// The playlist stack machine, extracted from play_session()'s main loop so the
// transition logic is testable. Standard items hold until their play_time elapses,
// then hand off by weighted next_item pick; subroutine items push their steps onto a
// stack (bounded by MAXIMUM_STACK) and pop when exhausted. The runner owns the stack
// and the switch clock; the caller owns the side effects -- it gets an on_enter
// callback per newly-entered item (audio events, program push, logging).
//
// Clocking: the caller feeds a wall-clock timestamp in milliseconds.
// Pause is the caller's concern too: freeze() shifts the
// switch clock forward by the paused frame's elapsed time, so held items don't time
// out under frozen visuals.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace trance_pb
{
  class PlaylistItem;
  class Session;
}

class PlaylistRunner
{
public:
  // Callback fired once per newly-entered item, after the stack reflects the entry.
  using EnterCallback = std::function<void(const std::string& name, const trance_pb::PlaylistItem&)>;

  // `session` must outlive the runner; the entry stack points into its playlist map.
  // Starts on session.first_playlist_item() (validated at load time).
  PlaylistRunner(const trance_pb::Session& session,
                 std::map<std::string, std::string> variables);

  // The item currently on top of the stack.
  const trance_pb::PlaylistItem& current() const;

  // Set the switch clock's reference point (call once, just before the main loop).
  void start(int64_t now_ms);
  // Paused frame: shift the switch clock forward so the held item doesn't time out.
  void freeze(int64_t elapsed_ms);
  // Run the transition loop at `now_ms`; fires on_enter per item entered.
  void advance(int64_t now_ms, const EnterCallback& on_enter);

private:
  struct Entry {
    const trance_pb::PlaylistItem* item;
    int subroutine_step;
  };

  const trance_pb::Session& _session;
  std::map<std::string, std::string> _variables;
  std::vector<Entry> _stack;
  int64_t _last_switch = 0;
};

#endif
