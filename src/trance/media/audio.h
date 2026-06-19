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

private:
  std::string _root_path;
  bool _muted = false;
  std::chrono::steady_clock _clock;
  struct channel {
    std::unique_ptr<sf::Music> music;
    std::uint32_t volume;

    bool current_fade;
    std::uint32_t fade_initial_volume;
    std::uint32_t fade_target_volume;
    std::uint32_t fade_time_seconds;
    std::chrono::steady_clock::time_point fade_start;
  };
  std::vector<channel> _channels;
  std::unique_ptr<EntrainmentStream> _entrainment;
};

#endif
