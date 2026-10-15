#ifndef TRANCE_SRC_COMMON_MEDIA_STREAMER_H
#define TRANCE_SRC_COMMON_MEDIA_STREAMER_H
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

#define VPX_CODEC_DISABLE_COMPAT 1
#pragma warning(push, 0)
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>
#pragma warning(pop)

class Image;
struct GifFileType;

// How long a frame is shown when the file doesn't say (a GIF with no graphics-control
// block, a WebM with neither a default duration nor a frame rate). 20fps: the middle of
// what animated media actually uses, and what the fixed-rate playback this replaced was
// aiming at.
static const float kDefaultFrameDelaySeconds = 1.f / 20.f;

class Streamer
{
public:
  virtual ~Streamer() = default;
  virtual bool success() const = 0;
  virtual void reset() = 0;
  virtual Image next_frame() = 0;
  // How long the frame just returned by next_frame() should be shown, in seconds, as
  // the FILE specifies it. An animation is a piece of timed media in its own right --
  // it plays at its own speed regardless of what the visual driving it is doing, so
  // this is per-frame data that travels with the frame rather than a rate the player
  // picks. Meaningless before the first next_frame(); never zero or negative.
  virtual float frame_delay_seconds() const { return kDefaultFrameDelaySeconds; }
};

class GifStreamer : public Streamer
{
public:
  GifStreamer(const std::string& path);
  ~GifStreamer() override;

  bool success() const override;
  void reset() override;
  Image next_frame() override;
  float frame_delay_seconds() const override { return _frame_delay; }

private:
  const std::string _path;
  bool _success = false;
  GifFileType* _gif = nullptr;
  std::size_t _index = 0;
  std::unique_ptr<uint32_t[]> _pixels;
  // Delay of the frame last returned, from its graphics-control extension block.
  float _frame_delay = kDefaultFrameDelaySeconds;
};

class WebmStreamer : public Streamer
{
public:
  WebmStreamer(const std::string& path);
  ~WebmStreamer() override;

  bool success() const override;
  void reset() override;
  Image next_frame() override;
  float frame_delay_seconds() const override { return _frame_delay; }

private:
  void codec_error(const std::string& error);

  const std::string _path;
  std::atomic<bool> _success = false;

  mkvparser::MkvReader _reader;
  std::unique_ptr<mkvparser::Segment> _segment;
  vpx_codec_ctx_t _codec;

  const mkvparser::VideoTrack* _video_track = nullptr;
  const mkvparser::Cluster* _cluster = nullptr;
  const mkvparser::BlockEntry* _block = nullptr;
  bool _cluster_eos = false;

  int _block_index = -1;
  bool _iterating = false;
  vpx_codec_iter_t _it = nullptr;
  const vpx_image_t* _image = nullptr;
  std::unique_ptr<uint8_t[]> _data;

  // Duration of the frame last returned. Preferred source is the track's declared
  // default duration / frame rate (constant, known up front, set in the constructor);
  // failing that, the gap between consecutive block timestamps, which lags by one frame
  // and is why _prev_block_time_ns is kept.
  float _frame_delay = kDefaultFrameDelaySeconds;
  // Set when the track declared its own rate, so the timestamp fallback stays off.
  bool _fixed_frame_delay = false;
  long long _prev_block_time_ns = -1;
};

bool is_gif_animated(const std::string& path);
std::unique_ptr<Streamer> load_animation(const std::string& path);

#endif