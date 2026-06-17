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
  // 0.1 s of stereo audio per streamed chunk.
  constexpr std::size_t chunk_frames = sample_rate / 10;

  struct LayerSpec {
    double center_hz;
    double binaural_hz;
    double pulse_hz;
    double amplitude_db;
  };

  // Built-in presets. These specific configurations are the validated targets;
  // a session may instead supply explicit layers.
  std::vector<LayerSpec> preset_layers(const std::string& name)
  {
    if (name == "bambi") {
      return {{312.0, 3.0, 5.0, 0.0}, {60.0, 3.0, 3.25, -6.0}};
    }
    if (name == "reactor") {
      return {{202.5, 4.0, 7.0, 0.0},
              {135.0, 3.5, 4.6, -4.0},
              {90.0, 3.0, 3.3, -6.0},
              {60.0, 2.5, 2.55, -8.0}};
    }
    if (name == "descent") {
      return {{200.0, 4.0, 5.0, 0.0}, {120.0, 3.0, 3.25, -3.0}, {55.0, 2.0, 2.55, -6.0}};
    }
    return {};
  }
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

  // RMS of the summed bed at unit master gain, measured over a window long
  // enough to cover the slow isochronic/binaural cycles. Operates on a copy so
  // the real phase state is untouched.
  double measure_rms(std::vector<EntrainmentStream::Layer> layers)
  {
    const std::size_t frames = sample_rate * 2;  // 2 seconds
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
      double l = 0.0;
      double r = 0.0;
      synth_frame(layers, l, r);
      sum_sq += l * l + r * r;
    }
    return std::sqrt(sum_sq / (2.0 * frames));
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

  auto specs = config.preset().empty() ? std::vector<LayerSpec>{} : preset_layers(config.preset());
  if (config.preset().empty()) {
    for (const auto& l : config.layer()) {
      specs.push_back({l.center_hz(), l.binaural_hz(), l.pulse_hz(), l.amplitude_db()});
    }
  }

  _layers.clear();
  for (const auto& s : specs) {
    Layer layer{};
    layer.carrier_left_hz = s.center_hz - s.binaural_hz / 2.0;
    layer.carrier_right_hz = s.center_hz + s.binaural_hz / 2.0;
    layer.pulse_hz = s.pulse_hz;
    layer.gain = std::pow(10.0, s.amplitude_db / 20.0);
    _layers.push_back(layer);
  }

  if (_layers.empty()) {
    _master_gain = 0.0;
    return;  // stays stopped: silence
  }

  const double target_db = config.master_db() != 0.f ? double(config.master_db()) : default_master_db;
  const double target = std::pow(10.0, target_db / 20.0);
  const double rms = measure_rms(_layers);
  _master_gain = rms > 1e-9 ? target / rms : 0.0;
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
