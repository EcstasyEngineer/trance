#ifndef TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_H
#define TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_H
#include <cstdint>
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
  struct Layer {
    // Source fields, copied straight off the config message.
    double center_hz;
    double binaural_hz;
    double pulse_hz;     // 0 = continuous (no gate)
    double amplitude_db;

    // Derived once at Configure() time and consumed by synth_frame().
    double carrier_left_hz;
    double carrier_right_hz;
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
  std::vector<std::int16_t> _buffer;  // interleaved stereo scratch reused per chunk
  std::string _last_config;           // serialized config, to skip no-op reconfigures
};

#endif
