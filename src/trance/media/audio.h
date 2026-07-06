#ifndef TRANCE_SRC_TRANCE_MEDIA_AUDIO_H
#define TRANCE_SRC_TRANCE_MEDIA_AUDIO_H
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace trance_pb
{
  class AudioEvent;
  class Entrainment;
  class PlaylistItem;
}
namespace sf
{
  class Music;
}
class EntrainmentStream;

class Audio
{
public:
  Audio(const std::string& root_path);
  ~Audio();
  void TriggerEvents(const trance_pb::PlaylistItem& item);
  void TriggerEvent(const trance_pb::AudioEvent& event);
  // Set the entrainment bed for the active program (preset or explicit layers).
  void SetEntrainment(const trance_pb::Entrainment& config);
  void Update();

  // Toggle a global mute over every channel and the entrainment bed (bound to
  // 'M' in main.cpp). Implemented via SFML's listener global volume so it sits
  // above the per-channel fade logic and is exactly reversible.
  void ToggleMute();
  bool Muted() const;

  // The `pause`/`resume` verbs: suspend every currently-playing music channel
  // (playlist + theme audio) and the entrainment bed, and resume exactly the ones
  // this pause suspended. Distinct from ToggleMute: sf::Music::pause() stops the
  // playback cursor, mute just silences it.
  void PauseAll();
  void ResumeAll();

  // Grammar-driven theme audio: plays a precanned mantra/cue picked
  // by ThemeBank::get_audio and phase-locked by the `beats N { audio ... }`
  // grammar. Lives on its own dedicated channel object (_theme_audio_channel),
  // entirely separate from the playlist AudioEvent channels in _channels (which
  // are small hand-authored indices grown on demand by TriggerEvent) -- the two
  // never collide and reserving the theme-audio slot costs exactly one sf::Music,
  // not a sparse-grown vector. Single-slot v0: exactly one live grammar audio at
  // a time, the same shape as VisualApiImpl's single live text slot
  // (`_current_text`) -- starting a new one stops whatever was playing. No TTS;
  // `path` is always a precanned file from a theme's audio_path pool.
  void play_theme_audio(const std::string& path, bool loop);
  void stop_theme_audio();
  // Clamped to [0, 1] and applied as SFML volume (0..100).
  void set_theme_audio_volume(float volume);

private:
  struct channel {
    std::unique_ptr<sf::Music> music;
    std::uint32_t volume;

    bool current_fade;
    std::uint32_t fade_initial_volume;
    std::uint32_t fade_target_volume;
    std::uint32_t fade_time_seconds;
    std::chrono::steady_clock::time_point fade_start;
  };
  // Grows _channels (via emplace_back of a default-constructed sf::Music
  // channel) until `index` exists, then returns a reference to it. Only used
  // for playlist AudioEvent channels, whose indices are small and hand-authored.
  channel& channel_at(std::uint32_t index);

  std::string _root_path;
  bool _muted = false;
  std::chrono::steady_clock _clock;
  std::vector<channel> _channels;
  // Dedicated single slot for grammar-driven theme audio -- see play_theme_audio.
  channel _theme_audio_channel;
  std::unique_ptr<EntrainmentStream> _entrainment;
};

#endif
