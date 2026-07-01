#ifndef TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_H
#define TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_H
#include <string>
#include <vector>

#include <trance/media/entrainment_keyframes.h>

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
// ear pulses to its peak the other is at its trough (see entrainment.cpp:50). The
// intent is to entrain with or without headphones -- the binaural carrier split
// needs headphones, but the anti-phase isochronic pulsing still drives on speakers.
class EntrainmentStream : public sf::SoundStream
{
public:
  EntrainmentStream();
  ~EntrainmentStream() override;

  // (Re)configure from a program's Entrainment message: resolves a named preset
  // or uses explicit layers, normalises the bed to the target level, and starts
  // playback. An empty configuration stops the stream (silence). Safe to call
  // repeatedly; it stops the audio thread before mutating shared state.
  void Configure(const trance_pb::Entrainment& config);

  // One resolved layer's running state. Public only so the .cpp synthesis
  // helpers can operate on it; not part of the intended interface.
  //
  // Each sweepable source field (center_hz, binaural_hz, pulse_hz,
  // amplitude_db) is carried as a Track, sampled once per synthesis block
  // against the stream's running time rather than per-sample -- the
  // cumulative-phase accumulators below already tolerate a frequency that
  // changes between blocks without a click, which is what makes block-rate
  // sampling safe. Configure() with today's static (non-keyframed) config
  // builds degenerate single-keyframe (constant) Tracks, so playback is
  // bit-for-bit unchanged until something actually authors a sweep.
  struct Layer {
    trance::Track center_hz;
    trance::Track binaural_hz;
    trance::Track pulse_hz;      // 0 = continuous (no gate)
    trance::Track amplitude_db;  // interpolated in dB, converted to gain per block

    // Per-block resolved values (Track::eval() results for the current
    // block), consumed by synth_frame(). carrier_left/right_hz and gain are
    // derived from center_hz/binaural_hz/amplitude_db each block.
    double carrier_left_hz;
    double carrier_right_hz;
    double resolved_pulse_hz;
    double gain;  // linear, per-layer relative level

    double phase_left;
    double phase_right;
    double pulse_phase;
  };

private:
  bool onGetData(Chunk& chunk) override;
  void onSeek(sf::Time) override
  {
  }

  double _master_gain;
  std::vector<Layer> _layers;
  std::vector<sf::Int16> _buffer;  // interleaved stereo scratch reused per chunk
  std::string _last_config;        // serialized config, to skip no-op reconfigures
  double _running_seconds = 0.0;   // stream time at the start of the next block, for Track::eval()
};

#endif
