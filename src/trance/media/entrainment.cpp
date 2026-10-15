#include <trance/media/entrainment.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <SFML/Audio/SoundChannel.hpp>
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
  // Reconfigure morph window: every Glide walks to its new target over this many
  // frames. Long enough that a frequency change reads as a bend rather than a
  // splice, short enough that a slider release still feels immediate.
  constexpr double glide_seconds = 0.3;
  constexpr double glide_frames = glide_seconds * sample_rate;

  double db_to_gain(double db)
  {
    return std::pow(10.0, db / 20.0);
  }

  // A config layer's resolved synthesis targets.
  struct LayerTarget {
    double carrier_left_hz;
    double carrier_right_hz;
    double pulse_hz;
    double depth;
    double gain;
  };

  LayerTarget resolve_target(const trance_pb::EntrainmentLayer& l)
  {
    LayerTarget t{};
    t.carrier_left_hz = double(l.center_hz()) - double(l.binaural_hz()) / 2.0;
    t.carrier_right_hz = double(l.center_hz()) + double(l.binaural_hz()) / 2.0;
    t.pulse_hz = double(l.pulse_hz());
    t.depth = l.pulse_hz() > 0.f ? 1.0 : 0.0;
    t.gain = db_to_gain(double(l.amplitude_db()));
    return t;
  }

  // A fresh layer sitting exactly at `t` (no glide in flight, phases at zero).
  EntrainmentStream::Layer snapped_layer(const LayerTarget& t)
  {
    EntrainmentStream::Layer layer{};
    layer.carrier_left_hz.snap(t.carrier_left_hz);
    layer.carrier_right_hz.snap(t.carrier_right_hz);
    layer.pulse_hz.snap(t.pulse_hz);
    layer.depth.snap(t.depth);
    layer.gain.snap(t.gain);
    return layer;
  }

  // Advance every layer by one frame and accumulate the (pre-master) stereo
  // sample. Mutates the layers' phase accumulators and in-flight glides; keeps
  // phases wrapped to avoid float drift over long sessions.
  void synth_frame(std::vector<EntrainmentStream::Layer>& layers, double& out_left,
                   double& out_right)
  {
    static const double two_pi = 2.0 * pi;
    const double step = two_pi / sample_rate;
    double left = 0.0;
    double right = 0.0;
    for (auto& layer : layers) {
      layer.carrier_left_hz.advance();
      layer.carrier_right_hz.advance();
      layer.pulse_hz.advance();
      layer.depth.advance();
      layer.gain.advance();

      double env_left = 1.0;
      double env_right = 1.0;
      const double depth = layer.depth.current;
      if (depth > 0.0) {
        // Unipolar (0..1) gate, 180 degrees out of phase between the ears,
        // mixed against the continuous tone by `depth` so the gate can melt in
        // and out instead of snapping.
        const double gate_left = 0.5 * (1.0 + std::cos(layer.pulse_phase + pi));
        const double gate_right = 0.5 * (1.0 + std::cos(layer.pulse_phase));
        env_left = 1.0 - depth + depth * gate_left;
        env_right = 1.0 - depth + depth * gate_right;
      }
      if (layer.pulse_hz.current > 0.0) {
        // Rolls even at depth 0 so a re-enabled gate resumes from live phase.
        layer.pulse_phase += step * layer.pulse_hz.current;
        if (layer.pulse_phase >= two_pi) {
          layer.pulse_phase -= two_pi;
        }
      }
      left += layer.gain.current * std::sin(layer.phase_left) * env_left;
      right += layer.gain.current * std::sin(layer.phase_right) * env_right;
      layer.phase_left += step * layer.carrier_left_hz.current;
      if (layer.phase_left >= two_pi) {
        layer.phase_left -= two_pi;
      }
      layer.phase_right += step * layer.carrier_right_hz.current;
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
  // enough to cover the slow isochronic/binaural cycles. Operates on a private
  // snapshot of the TARGET state, so in-flight glides and live phases are
  // untouched and the calibration describes where the morph lands.
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

  std::int16_t to_sample(double v)
  {
    v = std::max(-1.0, std::min(1.0, v));
    return static_cast<std::int16_t>(v * 32767.0);
  }
}

EntrainmentStream::EntrainmentStream()
{
  initialize(2, sample_rate, {sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight});
}

EntrainmentStream::~EntrainmentStream()
{
  // Halt the audio thread before our members are destroyed.
  stop();
}

void EntrainmentStream::Configure(const trance_pb::Entrainment& config)
{
  // Skip a no-op reconfigure so an unchanged bed keeps playing across program
  // changes without even a redundant (if inaudible) glide pass.
  std::string serialized = config.SerializeAsString();
  if (serialized == _last_config) {
    return;
  }
  _last_config = serialized;

  std::vector<LayerTarget> targets;
  targets.reserve(config.layer_size());
  for (const auto& l : config.layer()) {
    targets.push_back(resolve_target(l));
  }

  // Normalise the TARGET bed to the target level (RMS to master, peaks capped at
  // 0.8 for ~2 dB of headroom against hard-clip clicks on hot or phase-aligned
  // configs). Measured on a fresh snapshot; the live master then glides there.
  double master_target = 0.0;
  if (!targets.empty()) {
    std::vector<Layer> snapshot;
    snapshot.reserve(targets.size());
    for (const auto& t : targets) {
      snapshot.push_back(snapped_layer(t));
    }
    const double target_db =
        config.master_db() != 0.f ? double(config.master_db()) : default_master_db;
    const double target = db_to_gain(target_db);
    const Calibration cal = measure(std::move(snapshot));
    const double gain_rms = cal.rms > 1e-9 ? target / cal.rms : 0.0;
    static const double peak_ceiling = 0.8;
    const double gain_peak = cal.peak > 1e-9 ? peak_ceiling / cal.peak : gain_rms;
    master_target = std::min(gain_rms, gain_peak);
  }

  bool want_play = false;
  {
    std::lock_guard<std::mutex> lock{_mutex};
    if (getStatus() != sf::SoundSource::Status::Playing) {
      // Cold start (or bed re-enabled after the stream wound down): nothing is
      // audible to morph from, so snap straight into the target state. Carriers
      // start at phase zero (sin(0) = 0), so the onset itself is click-free.
      _layers.clear();
      _layers.reserve(targets.size());
      for (const auto& t : targets) {
        _layers.push_back(snapped_layer(t));
      }
      _master.snap(master_target);
      want_play = !_layers.empty() && master_target > 0.0;
    } else {
      // Live morph. Layers match POSITIONALLY: layer i glides to new-config
      // layer i, which also makes a middle-row removal a smooth morph of every
      // later layer into its successor plus a tail fade-out. Tail layers beyond
      // the new config die (fade to 0, pruned by onGetData); a config that grows
      // again mid-fade simply resurrects them toward the new targets.
      for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& t = targets[i];
        if (i < _layers.size()) {
          auto& layer = _layers[i];
          layer.dying = false;
          layer.carrier_left_hz.glide_to(t.carrier_left_hz, glide_frames);
          layer.carrier_right_hz.glide_to(t.carrier_right_hz, glide_frames);
          if (t.depth > 0.0 && layer.depth.current <= 0.0) {
            // Gate coming on from silence-of-gate: snap the rate (inaudible at
            // depth 0) and let depth melt it in.
            layer.pulse_hz.snap(t.pulse_hz);
          } else if (t.depth > 0.0) {
            layer.pulse_hz.glide_to(t.pulse_hz, glide_frames);
          }
          // Gate going off: leave the rate where it is; depth fading to 0 is
          // what actually removes the pulse (a rate gliding to zero slows
          // asymptotically and never gets there).
          layer.depth.glide_to(t.depth, glide_frames);
          layer.gain.glide_to(t.gain, glide_frames);
        } else {
          Layer layer = snapped_layer(t);
          layer.gain.current = 0.0;
          layer.gain.glide_to(t.gain, glide_frames);
          _layers.push_back(layer);
        }
      }
      for (std::size_t i = targets.size(); i < _layers.size(); ++i) {
        _layers[i].dying = true;
        _layers[i].gain.glide_to(0.0, glide_frames);
      }
      _master.glide_to(master_target, glide_frames);
      // An emptied config fades out through the loop above and the stream then
      // idles streaming zeros. Deliberate: ending the stream from the audio
      // thread races a quick re-enable (Configure would see Playing, morph into
      // a stream that is winding down, and the no-op guard would then swallow
      // the retry). An idle zero-fill every half second is the cheap option.
    }
  }
  if (want_play) {
    play();
  }
}

bool EntrainmentStream::onGetData(Chunk& chunk)
{
  std::lock_guard<std::mutex> lock{_mutex};
  _buffer.resize(chunk_frames * 2);
  if (_layers.empty()) {
    std::fill(_buffer.begin(), _buffer.end(), std::int16_t(0));
  } else {
    for (std::size_t i = 0; i < chunk_frames; ++i) {
      double l = 0.0;
      double r = 0.0;
      synth_frame(_layers, l, r);
      _buffer[2 * i] = to_sample(l * _master.current);
      _buffer[2 * i + 1] = to_sample(r * _master.current);
      _master.advance();
    }
    // Dead layers (dropped from the config, fade complete) only ever sit at the
    // tail -- see Configure's positional-match comment.
    while (!_layers.empty() && _layers.back().dying && _layers.back().gain.current == 0.0) {
      _layers.pop_back();
    }
  }
  chunk.samples = _buffer.data();
  chunk.sampleCount = _buffer.size();
  return true;
}
