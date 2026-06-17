#include <trance/media/entrainment.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#pragma warning(pop)

namespace
{
  constexpr double pi = 3.14159265358979323846;
  constexpr unsigned int sample_rate = 44100;
  constexpr double default_master_db = -28.0;
  // 0.5 s of stereo audio per streamed chunk. Latency is irrelevant for an
  // ambient bed, so we favour a deep buffer: with SFML's internal queue this
  // gives ~1.5 s of slack, which keeps the stream from underrunning (clicks)
  // when the CPU is busy.
  constexpr std::size_t chunk_frames = sample_rate / 2;
}

EntrainmentStream::EntrainmentStream() : _master_gain{0.0}
{
  initialize(2, sample_rate);
}

EntrainmentStream::~EntrainmentStream()
{
  // Halt the audio thread before our members are destroyed.
  stop();
}

namespace
{
  // Advance every layer by one frame and accumulate the (pre-master) stereo
  // sample. Mutates the layers' phase accumulators; keeps them wrapped to avoid
  // float drift over long sessions.
  void synth_frame(std::vector<EntrainmentStream::Layer>& layers, double& out_left,
                   double& out_right)
  {
    static const double two_pi = 2.0 * pi;
    const double step = two_pi / sample_rate;
    double left = 0.0;
    double right = 0.0;
    for (auto& layer : layers) {
      double env_left = 1.0;
      double env_right = 1.0;
      if (layer.pulse_hz > 0.0) {
        // Unipolar (0..1) gate, 180 degrees out of phase between the ears.
        env_left = 0.5 * (1.0 + std::cos(layer.pulse_phase + pi));
        env_right = 0.5 * (1.0 + std::cos(layer.pulse_phase));
        layer.pulse_phase += step * layer.pulse_hz;
        if (layer.pulse_phase >= two_pi) {
          layer.pulse_phase -= two_pi;
        }
      }
      left += layer.gain * std::sin(layer.phase_left) * env_left;
      right += layer.gain * std::sin(layer.phase_right) * env_right;
      layer.phase_left += step * layer.carrier_left_hz;
      if (layer.phase_left >= two_pi) {
        layer.phase_left -= two_pi;
      }
      layer.phase_right += step * layer.carrier_right_hz;
      if (layer.phase_right >= two_pi) {
        layer.phase_right -= two_pi;
      }
    }
    out_left = left;
    out_right = right;
  }

  struct Calibration {
    double rms;
    double peak;
  };

  // RMS and peak of the summed bed at unit master gain, over a window long
  // enough to cover the slow isochronic/binaural cycles. Operates on a copy so
  // the real phase state is untouched.
  Calibration measure(std::vector<EntrainmentStream::Layer> layers)
  {
    const std::size_t frames = sample_rate * 2;  // 2 seconds
    double sum_sq = 0.0;
    double peak = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
      double l = 0.0;
      double r = 0.0;
      synth_frame(layers, l, r);
      sum_sq += l * l + r * r;
      peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    return {std::sqrt(sum_sq / (2.0 * frames)), peak};
  }

  sf::Int16 to_sample(double v)
  {
    v = std::max(-1.0, std::min(1.0, v));
    return static_cast<sf::Int16>(v * 32767.0);
  }
}

void EntrainmentStream::Configure(const trance_pb::Entrainment& config)
{
  // Skip a no-op reconfigure so an unchanged bed keeps playing across program
  // changes instead of glitching on a stop/restart.
  std::string serialized = config.SerializeAsString();
  if (serialized == _last_config) {
    return;
  }
  _last_config = serialized;

  // Stop the audio thread before mutating the shared layer/gain state.
  stop();

  _layers.clear();
  for (const auto& l : config.layer()) {
    Layer layer{};
    layer.carrier_left_hz = l.center_hz() - l.binaural_hz() / 2.0;
    layer.carrier_right_hz = l.center_hz() + l.binaural_hz() / 2.0;
    layer.pulse_hz = l.pulse_hz();
    layer.gain = std::pow(10.0, l.amplitude_db() / 20.0);
    _layers.push_back(layer);
  }

  if (_layers.empty()) {
    _master_gain = 0.0;
    return;  // stays stopped: silence
  }

  const double target_db = config.master_db() != 0.f ? double(config.master_db()) : default_master_db;
  const double target = std::pow(10.0, target_db / 20.0);
  const Calibration cal = measure(_layers);
  const double gain_rms = cal.rms > 1e-9 ? target / cal.rms : 0.0;
  // Cap the gain so peaks stay below 0.8 (~2 dB headroom): prevents hard-clip
  // clicks on hot or phase-aligned configs. Inactive for the default bed, whose
  // peaks sit well under full scale at the default level.
  static const double peak_ceiling = 0.8;
  const double gain_peak = cal.peak > 1e-9 ? peak_ceiling / cal.peak : gain_rms;
  _master_gain = std::min(gain_rms, gain_peak);
  if (_master_gain <= 0.0) {
    return;  // degenerate (all-silent) bed: stay stopped rather than play silence
  }

  play();
}

bool EntrainmentStream::onGetData(Chunk& chunk)
{
  _buffer.resize(chunk_frames * 2);
  if (_layers.empty()) {
    std::fill(_buffer.begin(), _buffer.end(), sf::Int16(0));
  } else {
    for (std::size_t i = 0; i < chunk_frames; ++i) {
      double l = 0.0;
      double r = 0.0;
      synth_frame(_layers, l, r);
      _buffer[2 * i] = to_sample(l * _master_gain);
      _buffer[2 * i + 1] = to_sample(r * _master_gain);
    }
  }
  chunk.samples = _buffer.data();
  chunk.sampleCount = _buffer.size();
  return true;
}
