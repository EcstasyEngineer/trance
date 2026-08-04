#include <trance/media/async_streamer.h>
#include <algorithm>

namespace
{
  std::size_t prev_index(std::size_t i, std::size_t buffer_size)
  {
    return (i + buffer_size - 1) % buffer_size;
  }

  // Upper bound on frames advanced in one advance_frame() call. A tick cannot usefully
  // show more than one frame anyway -- only the last one drawn is seen -- so this only
  // exists to keep a nonsense delay from walking the whole buffer every tick.
  const std::size_t kMaxFramesPerTick = 8;
}

AsyncStreamer::AsyncStreamer(const std::function<StreamerLoad()>& load_function,
                             size_t buffer_size)
: _load_function{load_function}, _buffer_size{buffer_size}
{
  auto initial = load_function();
  _a.streamer = std::move(initial.streamer);
  _a.tag = initial.tag;
  _a.buffer.resize(_buffer_size);
  _b.buffer.resize(_buffer_size);
  _a.delays.assign(_buffer_size, kDefaultFrameDelaySeconds);
  _b.delays.assign(_buffer_size, kDefaultFrameDelaySeconds);
  _current = &_a;
  _next = &_b;
  while (_a.streamer && !_a.end && _a.size < _buffer_size) {
    auto image = _a.streamer->next_frame();
    if (image) {
      _a.buffer[_a.size] = image;
      _a.delays[_a.size] = _a.streamer->frame_delay_seconds();
      ++_a.size;
    } else {
      _a.end = true;
    }
  }
}

Image AsyncStreamer::get_frame(const void* live_tag,
                               const std::function<void(const Image&)>& function) const
{
  std::lock_guard<std::mutex> lock{_swap_mutex};
  // A frame from a streamer loaded for something other than the lane's current tag is
  // the wrong theme's content, however it got here (installed by an old switch, or the
  // lane's theme changed under a playing streamer). Refusing it -- rather than serving
  // it because it happens to be valid -- is what keeps every caller's "current theme"
  // reasoning honest; the callers' fallback chains own what shows instead.
  if (!_current->streamer || _current->tag != live_tag) {
    return {};
  }
  function(_current->buffer[_index]);
  Image image = _current->buffer[_index];
  return image;
}

void AsyncStreamer::advance_frame(uint32_t global_fps, bool maybe_switch, const void* live_tag)
{
  std::lock_guard<std::mutex> lock{_swap_mutex};
  // A switch installs _next, so EVERY branch requires it fresh (loaded for the lane's
  // current tag -- installing a known-stale streamer is how wrong-theme content used to
  // reach the screen) and fully buffered. Fully buffered also closes a race the old
  // dead-current branch had: async_update() decodes into _next with _swap_mutex
  // RELEASED around next_frame(), and a swap during that window redirects the decode
  // into the wrong Animation. A full (or ended) next is one the async thread has
  // stopped decoding.
  //
  // Three reasons to switch onto it:
  //   - current is dead or was never loaded (a lane whose first theme had no
  //     animations used to be bricked forever here: can_change demanded a non-null
  //     current, which only a switch could provide);
  //   - current is STALE (its tag is not the lane's): self-healing, no maybe_switch
  //     traffic required -- patterns that never fire `anim` on a lane still get the
  //     right theme's animation;
  //   - the ordinary case: an `anim` effect asked (maybe_switch) and the current
  //     animation has completed a pass (_reached_end).
  const bool next_ready = _next->streamer && _next->tag == live_tag && !_old_streamer &&
      (_next->end || _next->size >= _buffer_size);
  const bool current_dead =
      !_current->streamer || !_current->streamer->success() || !_current->size;
  const bool current_stale = _current->streamer && _current->tag != live_tag;
  const bool can_change =
      next_ready && (current_dead || current_stale || (maybe_switch && _reached_end));
  if (can_change) {
    std::swap(_current, _next);
    _next->begin = 0;
    _next->size = 0;
    _next->end = false;
    _reached_end = false;
    _backwards = false;
    _index = 0;
    // The new animation's first frame starts its own delay from zero rather than
    // inheriting whatever was banked against the outgoing one's.
    _frame_time = 0.f;
    std::lock_guard<std::mutex> lock{_old_mutex};
    _old_streamer.swap(_next->streamer);
    for (auto& image : _next->buffer) {
      _old_buffer.emplace_back(std::move(image));
    }
  }

  // One tick of content time. Frames advance when the CURRENT frame's own delay has been
  // paid for, so an animation plays at the speed its file specifies -- independent of
  // global_fps and of whatever the visual driving it is doing. (This replaced
  // `_update_counter += (120/global_fps)/8`, which advanced exactly one frame per 15
  // ticks: every animation played at a flat 15fps no matter what it was authored at,
  // so slow GIFs raced and fast ones ran as stop-motion.)
  //
  // A frame slower than the tick rate simply waits several ticks; a frame faster than
  // the tick rate consumes several buffer entries in one tick, which is what the loop is
  // for. kMaxFramesPerTick bounds that: with a broken delay the loop would otherwise run
  // the whole buffer -- and a tick can't show more frames than the buffer holds anyway.
  _frame_time += 1.f / static_cast<float>(global_fps ? global_fps : 1);
  for (std::size_t steps = 0; steps < kMaxFramesPerTick; ++steps) {
    // Clamped, not trusted: delays[] comes from file metadata, and a zero would spin
    // here forever.
    const float delay = std::max(1.f / 240.f, _current->delays[_index]);
    if (_frame_time < delay) {
      break;
    }
    _frame_time -= delay;
    if (_backwards) {
      if (_index != _current->begin) {
        _index = prev_index(_index, _buffer_size);
      } else {
        _backwards = false;
        if (_index != prev_index(_current->begin + _current->size, _buffer_size)) {
          _index = (_index + 1) % _buffer_size;
        }
      }
    } else {
      if (_index != prev_index(_current->begin + _current->size, _buffer_size)) {
        _index = (_index + 1) % _buffer_size;
      } else {
        _backwards = true;
        if (_index != _current->begin) {
          _index = prev_index(_index, _buffer_size);
        }
      }
    }
  }
  if (_current->end && _index == prev_index(_current->begin + _current->size, _buffer_size)) {
    _reached_end = true;
  }
}

void AsyncStreamer::async_update(const void* live_tag,
                                 const std::function<void(const Image&)>& cleanup_function)
{
  {
    std::lock_guard<std::mutex> lock{_old_mutex};
    if (_old_streamer) {
      _old_streamer.reset();
    }
    if (!_old_buffer.empty()) {
      cleanup_function(_old_buffer.front());
      _old_buffer.pop_front();
    }
  }
  do {
    std::lock_guard<std::mutex> lock{_old_mutex};
    if (_old_buffer.size() <= _buffer_size) {
      break;
    }
    cleanup_function(_old_buffer.front());
    _old_buffer.pop_front();
  } while (true);

  do {
    std::unique_lock<std::mutex> swap_lock{_swap_mutex};
    if (!_current->streamer || _current->end || _index == _current->begin) {
      break;
    }
    // The streamer being decoded, captured UNDER the lock and called through the local:
    // next_frame() runs with the lock released, and the render thread can swap current
    // and next in that window (a stale current switches to a fresh next the moment the
    // next is buffered -- no anim traffic required). After the swap, `_current` names
    // the OTHER Animation, so both halves of the old `_current->streamer->next_frame()`
    // were wrong: the member read raced the swap, and the decoded frame would be
    // appended into the freshly-installed animation's buffer -- a frame of the OUTGOING
    // theme inserted into a buffer whose tag says otherwise, which is exactly the
    // wrong-content class this file just closed. Streamer objects are only destroyed on
    // this thread (via _old_streamer), so calling through the local is safe; the
    // identity re-check after relocking drops the frame if a swap landed.
    Streamer* decoding = _current->streamer.get();
    swap_lock.unlock();
    auto image = decoding->next_frame();
    swap_lock.lock();
    if (_current->streamer.get() != decoding) {
      break;
    }
    if (!image) {
      _current->end = true;
      break;
    }
    if (_current->size == _buffer_size) {
      // Only the full-buffer branch can collide with the render thread: it recycles
      // buffer[begin] into _old_buffer, and if _index has landed on begin while the
      // decode above ran unlocked, that is the frame currently on screen -- the
      // cleanup_function would purge an sf::Image still being drawn. The append branch
      // below writes at begin+size, one past the displayed range, so it never needs to
      // guard. (The loop-top guard checked this already, but next_frame() drops the lock
      // for as long as a decode takes, so _index can advance onto begin meanwhile.)
      //
      // Same condition as the loop-top guard, so take the same exit: drop the frame we
      // just decoded and let the next async_update() pass re-decode it. Waiting here
      // instead would hang -- the main loop gates theme_bank->advance_frames() on
      // !playback_paused, so while paused or Shift+F11-hidden _index never moves at all,
      // and async_thread.join() on Quit would never return.
      if (_index == _current->begin) {
        break;
      }
      {
        std::lock_guard<std::mutex> lock{_old_mutex};
        _old_buffer.emplace_back(std::move(_current->buffer[_current->begin]));
      }
      _current->buffer[_current->begin] = image;
      _current->delays[_current->begin] = _current->streamer->frame_delay_seconds();
      _current->begin = (1 + _current->begin) % _buffer_size;
    } else {
      const auto slot = (_current->begin + _current->size) % _buffer_size;
      _current->buffer[slot] = image;
      _current->delays[slot] = _current->streamer->frame_delay_seconds();
      ++_current->size;
    }
  } while (true);

  std::unique_lock<std::mutex> swap_lock{_swap_mutex};
  // A preloaded next whose tag has fallen behind the lane is a wrong-theme animation
  // waiting to be installed. Retire it (when the old-streamer slot is free to absorb it
  // -- next pass otherwise) so the slot reloads for the lane's current theme; without
  // this the fresh load below never runs and the lane starves on stale content.
  // Same _swap_mutex-then-_old_mutex order as advance_frame's swap block.
  if (_next->streamer && _next->tag != live_tag) {
    std::lock_guard<std::mutex> lock{_old_mutex};
    if (!_old_streamer) {
      _old_streamer.swap(_next->streamer);
      for (auto& image : _next->buffer) {
        _old_buffer.emplace_back(std::move(image));
      }
      _next->begin = 0;
      _next->size = 0;
      _next->end = false;
    }
  }
  if (!_next->streamer) {
    swap_lock.unlock();
    auto next_load = _load_function();
    swap_lock.lock();
    _next->streamer = std::move(next_load.streamer);
    _next->tag = next_load.tag;
  }
  for (auto i = 0; i < 8; ++i) {
    if (!_next->streamer || _next->end || _next->size == _buffer_size) {
      break;
    }
    swap_lock.unlock();
    auto image = _next->streamer->next_frame();
    swap_lock.lock();
    if (!image) {
      _next->end = true;
      break;
    }
    _next->buffer[_next->size] = image;
    _next->delays[_next->size] = _next->streamer->frame_delay_seconds();
    ++_next->size;
  }
}
