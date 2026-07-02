#include <trance/media/audio.h>
#include <trance/media/entrainment.h>
#include <iostream>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <SFML/Audio/Listener.hpp>
#include <SFML/Audio/Music.hpp>
#pragma warning(pop)

Audio::Audio(const std::string& root_path)
: _root_path{root_path}
// Grammar audio starts at FULL volume: a bare `audio concept` with no volume
// modulator must be audible (the advertised `every beats N { audio mantra }`
// shape). Patterns that want quieter starts write `volume`.
, _theme_audio_channel{{}, 100, false, 0, 0, 0, {}}
, _entrainment{new EntrainmentStream}
{
  _theme_audio_channel.music.reset(new sf::Music);
  sf::Listener::setGlobalVolume(100.f);
}

Audio::~Audio()
{
  sf::Listener::setGlobalVolume(100.f);
}

void Audio::SetEntrainment(const trance_pb::Entrainment& config)
{
  _entrainment->Configure(config);
}

void Audio::TriggerEvents(const trance_pb::PlaylistItem& item)
{
  for (const auto& event : item.audio_event()) {
    TriggerEvent(event);
  }
}

Audio::channel& Audio::channel_at(std::uint32_t index)
{
  while (index >= _channels.size()) {
    _channels.emplace_back(channel{{}, 0, false, 0, 0, 0, {}});
    _channels.back().music.reset(new sf::Music);
  }
  return _channels[index];
}

void Audio::TriggerEvent(const trance_pb::AudioEvent& event)
{
  channel_at(event.channel());
  uint32_t i = event.channel();
  while (event.next_unused_channel() && event.type() == trance_pb::AudioEvent::AUDIO_PLAY) {
    if (channel_at(i).music->getStatus() != sf::SoundSource::Status::Playing) {
      break;
    }
    ++i;
  }
  auto& channel = channel_at(i);

  if (event.type() == trance_pb::AudioEvent::AUDIO_PLAY) {
    if (!channel.music->openFromFile(_root_path + "/" + event.path())) {
      std::cerr << "\ncouldn't load " << event.path() << std::endl;
      return;
    }
    channel.music->setLooping(event.loop());
    channel.music->setVolume(float(event.volume()));
    channel.music->play();
    channel.volume = event.volume();
  } else if (event.type() == trance_pb::AudioEvent::AUDIO_STOP) {
    channel.music->stop();
  } else if (event.type() == trance_pb::AudioEvent::AUDIO_FADE) {
    channel.current_fade = true;
    channel.fade_initial_volume = channel.volume;
    channel.fade_target_volume = event.volume();
    channel.fade_time_seconds = event.time_seconds();
    channel.fade_start = _clock.now();
  }
}

void Audio::play_theme_audio(const std::string& path, bool loop)
{
  auto& channel = _theme_audio_channel;
  if (!channel.music->openFromFile(_root_path + "/" + path)) {
    std::cerr << "\ncouldn't load " << path << std::endl;
    return;
  }
  channel.music->setLooping(loop);
  channel.music->setVolume(float(channel.volume));
  channel.music->play();
}

void Audio::stop_theme_audio()
{
  _theme_audio_channel.music->stop();
}

void Audio::set_theme_audio_volume(float volume)
{
  volume = std::max(0.f, std::min(1.f, volume));
  auto& channel = _theme_audio_channel;
  channel.volume = std::uint32_t(volume * 100.f + .5f);
  channel.music->setVolume(float(channel.volume));
}

void Audio::ToggleMute()
{
  _muted = !_muted;
  sf::Listener::setGlobalVolume(_muted ? 0.f : 100.f);
}

bool Audio::Muted() const
{
  return _muted;
}

void Audio::Update()
{
  for (auto& channel : _channels) {
    if (!channel.current_fade) {
      continue;
    }
    auto seconds = std::chrono::seconds(channel.fade_time_seconds);
    if (channel.fade_start + seconds < _clock.now()) {
      channel.volume = channel.fade_target_volume;
      channel.music->setVolume(float(channel.volume));
      channel.current_fade = false;
      continue;
    }
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(_clock.now() - channel.fade_start);
    auto r = float(elapsed_ms.count()) / (1000.f * channel.fade_time_seconds);
    r = std::max(0.f, std::min(1.f, r));
    auto volume = r * channel.fade_target_volume + (1 - r) * channel.fade_initial_volume;
    channel.volume = std::uint32_t(volume + .5f);
    channel.music->setVolume(volume);
  }
}
