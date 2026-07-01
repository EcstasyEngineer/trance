#ifndef TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_KEYFRAMES_H
#define TRANCE_SRC_TRANCE_MEDIA_ENTRAINMENT_KEYFRAMES_H
#include <algorithm>
#include <vector>

// Pure interpolation core for sweepable entrainment fields (center/binaural/pulse
// Hz, amplitude dB). Deliberately schema-agnostic and dependency-free (no SFML, no
// trance headers) so it stays headless-testable; the JSON authoring format that
// populates a Track lands in a later wave.
namespace trance
{
  struct Keyframe {
    double t_seconds;
    double value;
  };

  // A sorted-by-time sequence of keyframes, piecewise-linearly interpolated.
  // Empty: caller supplies a default (Track doesn't know one). Single keyframe:
  // constant at that value for all t. Times before the first / after the last
  // keyframe clamp to the nearest endpoint value.
  class Track
  {
  public:
    Track() = default;

    // Keyframes may be given out of order; they're sorted by t_seconds here.
    explicit Track(std::vector<Keyframe> keyframes) : _keyframes{std::move(keyframes)}
    {
      std::sort(_keyframes.begin(), _keyframes.end(),
                [](const Keyframe& a, const Keyframe& b) { return a.t_seconds < b.t_seconds; });
    }

    // Single-keyframe convenience constructor: today's "static value" case,
    // used to lift existing scalar fields into a Track with no schema change.
    explicit Track(double constant_value) : _keyframes{{0.0, constant_value}}
    {
    }

    bool empty() const
    {
      return _keyframes.empty();
    }

    // Piecewise-linear evaluation, clamped at the ends. Caller must not call
    // this on an empty track (use empty() to fall back to a default value).
    double eval(double t) const
    {
      if (_keyframes.size() == 1 || t <= _keyframes.front().t_seconds) {
        return _keyframes.front().value;
      }
      if (t >= _keyframes.back().t_seconds) {
        return _keyframes.back().value;
      }
      // First keyframe with t_seconds >= t; interpolate between it and its
      // predecessor. Guaranteed to find one that isn't begin() since we've
      // already handled t <= front().t_seconds above.
      auto next = std::lower_bound(_keyframes.begin(), _keyframes.end(), t,
                                    [](const Keyframe& kf, double value) { return kf.t_seconds < value; });
      const Keyframe& hi = *next;
      const Keyframe& lo = *(next - 1);
      const double span = hi.t_seconds - lo.t_seconds;
      const double frac = span > 0.0 ? (t - lo.t_seconds) / span : 0.0;
      return lo.value + frac * (hi.value - lo.value);
    }

  private:
    std::vector<Keyframe> _keyframes;
  };
}

#endif
