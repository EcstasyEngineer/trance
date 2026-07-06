#include <trance/playlist_runner.h>
#include <common/common.h>
#include <common/session.h>
#include <common/util.h>
#include <iostream>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  // Weighted pick among the item's enabled next_item entries; empty when no entry has
  // weight (the item then holds, or a subroutine pops).
  std::string next_playlist_item(const std::map<std::string, std::string>& variables,
                                 const trance_pb::PlaylistItem* item)
  {
    uint32_t total = 0;
    for (const auto& next : item->next_item()) {
      total += (is_enabled(next, variables) ? next.random_weight() : 0);
    }
    if (!total) {
      return {};
    }
    auto r = random(total);
    total = 0;
    for (const auto& next : item->next_item()) {
      total += (is_enabled(next, variables) ? next.random_weight() : 0);
      if (r < total) {
        return next.playlist_item_name();
      }
    }
    return {};
  }
}

PlaylistRunner::PlaylistRunner(const trance_pb::Session& session,
                               std::map<std::string, std::string> variables)
: _session{session}, _variables{std::move(variables)}
{
  _stack.push_back({&_session.playlist().find(_session.first_playlist_item())->second, 0});
}

const trance_pb::PlaylistItem& PlaylistRunner::current() const
{
  return *_stack.back().item;
}

void PlaylistRunner::start(int64_t now_ms)
{
  _last_switch = now_ms;
}

void PlaylistRunner::freeze(int64_t elapsed_ms)
{
  _last_switch += elapsed_ms;
}

void PlaylistRunner::advance(int64_t now_ms, const EnterCallback& on_enter)
{
  while (true) {
    auto time_since_switch = now_ms - _last_switch;
    auto& entry = _stack.back();
    // Hold a standard item until its play time elapses.
    if (entry.item->has_standard() &&
        time_since_switch < 1000 * entry.item->standard().play_time_seconds()) {
      break;
    }
    // Trigger the next step of a subroutine.
    if (entry.item->has_subroutine() &&
        entry.subroutine_step < entry.item->subroutine().playlist_item_name_size()) {
      if (_stack.size() >= MAXIMUM_STACK) {
        std::cerr << "error: subroutine stack overflow\n";
        entry.subroutine_step = entry.item->subroutine().playlist_item_name_size();
      } else {
        _last_switch = now_ms;
        auto name = entry.item->subroutine().playlist_item_name(entry.subroutine_step);
        _stack.push_back({&_session.playlist().find(name)->second, 0});
        if (on_enter) {
          on_enter(name, *_stack.back().item);
        }
        ++_stack[_stack.size() - 2].subroutine_step;
        continue;
      }
    }
    auto next = next_playlist_item(_variables, entry.item);
    // Finish a subroutine.
    if (next.empty() && _stack.size() > 1) {
      _stack.pop_back();
      continue;
    } else if (next.empty()) {
      break;
    }
    // Hand off to the next standard item.
    _last_switch = now_ms;
    _stack.back().item = &_session.playlist().find(next)->second;
    _stack.back().subroutine_step = 0;
    if (on_enter) {
      on_enter(next, *entry.item);
    }
  }
}
