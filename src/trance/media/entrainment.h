#ifndef TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_H
#define TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_H
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include <SFML/Audio/SoundStream.hpp>
#pragma warning(pop)

namespace trance_pb
{
  class Entrainment;
}

// Continuously synthesises a binaural/isochronic entrainment bed on SFML's
// audio thread. Each layer is a carrier with an optional binaural split
// (left = center - beat/2, right = center + beat/2) and an optional isochronic
// amplitude gate. The gate runs the L/R pulse 180 degrees out of phase: when one
// ear pulses to its peak the other is at its trough (see entrainment.cpp). The
// intent is to entrain with or without headphones -- the binaural carrier split
// needs headphones, but the anti-phase isochronic pulsing still drives on speakers.
class EntrainmentStream : public sf::SoundStream
{
public:
  EntrainmentStream();
  ~EntrainmentStream() override;

  // (Re)configure from a program's Entrainment message. While the stream is
  // playing this is a MORPH, not a cut: the synthesis keeps its cumulative phase
  // accumulators and every parameter glides to its new target over a short window
  // (entrainment.cpp glide_seconds), so a frequency change bends like a doppler
  // sweep instead of splicing a new waveform, gains ramp instead of stepping,
  // added layers fade in and removed layers fade out. An empty configuration
  // fades the bed to silence (the stream then idles streaming zeros -- see
  // Configure's comment). Safe to call repeatedly; unchanged configs no-op.
  void Configure(const trance_pb::Entrainment& config);

  // One glideable synthesis parameter: `current` is what this frame's synthesis
  // uses, walking toward `target` by `step` per frame (step carries the sign;
  // 0 = arrived). Frequencies gliding under cumulative phase accumulators bend
  // pitch with no waveform discontinuity -- that is the entire smoothing model.
  struct Glide {
    double current = 0.0;
    double target = 0.0;
    double step = 0.0;

    void advance()
    {
      if (step == 0.0) {
        return;
      }
      current += step;
      if ((step > 0.0 && current >= target) || (step < 0.0 && current <= target)) {
        current = target;
        step = 0.0;
      }
    }
    void snap(double value)
    {
      current = target = value;
      step = 0.0;
    }
    void glide_to(double value, double frames)
    {
      target = value;
      step = frames > 0.0 ? (target - current) / frames : 0.0;
      if (step == 0.0) {
        current = target;
      }
    }
  };

  // One resolved layer's running state. Public only so the .cpp synthesis
  // helpers can operate on it; not part of the intended interface.
  struct Layer {
    Glide carrier_left_hz;
    Glide carrier_right_hz;
    // The isochronic gate is rate + DEPTH rather than rate alone: turning the
    // gate on/off morphs depth between 0 (continuous tone) and 1 (full gate),
    // because a rate gliding to zero slows asymptotically and stepping the
    // envelope to flat clicks. Rate only glides while the gate is audible.
    Glide pulse_hz;
    Glide depth;
    Glide gain;  // linear per-layer level; layers fade in/out through this
    // Fading to zero gain after being dropped from the config; pruned from the
    // tail once it arrives. Only ever set on layers past the config's count.
    bool dying = false;

    double phase_left = 0.0;
    double phase_right = 0.0;
    double pulse_phase = 0.0;
  };

private:
  bool onGetData(Chunk& chunk) override;
  void onSeek(sf::Time) override
  {
  }

  // Configure (main thread) mutates layer/gain state the audio thread is
  // synthesising from; everything below is guarded. play() is never called with
  // the lock held (the backend may pull a first chunk synchronously).
  std::mutex _mutex;
  Glide _master;
  std::vector<Layer> _layers;
  std::vector<std::int16_t> _buffer;  // interleaved stereo scratch reused per chunk
  std::string _last_config;           // serialized config, to skip no-op reconfigures
};

#endif
