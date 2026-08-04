#ifndef TRANCE_SRC_TRANCE_MEDIA_ASYNC_STREAMER_H
#define TRANCE_SRC_TRANCE_MEDIA_ASYNC_STREAMER_H
#include <common/media/image.h>
#include <common/media/streamer.h>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// One load request's result: the streamer plus an opaque tag naming what it was loaded
// FOR (ThemeBank passes the lane's ThemeInfo*). The tag travels with the streamer through
// the current/next double-buffer so staleness is detectable at USE time: a preloaded
// streamer is always picked under whatever theme occupies the lane at LOAD time, and by
// the time it is installed the lane may hold a different theme entirely. Before the tag,
// that was invisible -- every theme swap installed (or kept) an animation belonging to
// the OUTGOING theme, and the lane played the unloading theme's content on screen until
// something else happened to switch it.
struct StreamerLoad {
  std::unique_ptr<Streamer> streamer;
  const void* tag = nullptr;
};

// TODO: should really use image pools to avoid allocating every frame separately.
class AsyncStreamer
{
public:
  AsyncStreamer(const std::function<StreamerLoad()>& load_function, size_t buffer_size);

  // `live_tag` is the caller's CURRENT tag for this lane; a frame is only served when the
  // playing streamer was loaded for that same tag, so a wrong-theme frame is never handed
  // out (the caller's fallback chain owns what to show instead).
  Image get_frame(const void* live_tag, const std::function<void(const Image&)>& function) const;
  // A stale current streamer (tag != live_tag) switches to the preloaded next as soon as
  // that next is fresh and buffered, with no maybe_switch traffic required -- patterns
  // that never touch change_animation on a lane still get the right theme's animation.
  void advance_frame(uint32_t global_fps, bool maybe_switch, const void* live_tag);

  // Called from async update thread. Retires a preloaded next whose tag has fallen
  // behind `live_tag` so the slot reloads for the lane's current theme.
  void async_update(const void* live_tag, const std::function<void(const Image&)>& cleanup_function);

private:
  mutable std::mutex _swap_mutex;
  mutable std::mutex _old_mutex;
  struct Animation {
    std::unique_ptr<Streamer> streamer;
    // What this streamer was loaded FOR (StreamerLoad::tag). Guarded by _swap_mutex,
    // like every other Animation field.
    const void* tag = nullptr;
    std::vector<Image> buffer;
    // Parallel to buffer: how long each buffered frame is shown, in seconds, as its file
    // specifies (Streamer::frame_delay_seconds). Frames are decoded on the async thread
    // and displayed later by the render thread, so the timing has to travel WITH the
    // frame -- by the time a frame reaches the screen its streamer has moved on.
    std::vector<float> delays;
    std::size_t begin = 0;
    std::size_t size = 0;
    bool end = false;
  };
  std::function<StreamerLoad()> _load_function;
  const size_t _buffer_size;
  Animation _a;
  Animation _b;
  Animation* _current;
  Animation* _next;
  std::deque<Image> _old_buffer;
  std::unique_ptr<Streamer> _old_streamer;

  // Seconds of content time banked toward the current frame's delay. Content time, not
  // wall time: advance_frame() is called once per global_fps tick from the render loop,
  // so a tick is 1/global_fps of a second and pausing simply stops the clock.
  float _frame_time = 0.f;
  std::size_t _index = 0;
  bool _backwards = false;
  bool _reached_end = false;
};

#endif