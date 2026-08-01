#include <trance/theme_bank.h>
#include <common/util.h>
#include <filesystem>
#include <iostream>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#pragma warning(pop)

namespace
{
  // Finds a usable system font so that text can still render when a theme
  // defines none. Checked once (function-local static) and then reused
  // lock-free; returns an absolute path, or empty if nothing was found.
  const std::string& find_system_font()
  {
    static const std::string result = [] {
#ifdef _WIN32
      static const char* names[] = {"arial.ttf", "segoeui.ttf", "calibri.ttf"};
      static const char* dirs[] = {"C:/Windows/Fonts"};
#else
      static const char* names[] = {"DejaVuSans.ttf", "LiberationSans-Regular.ttf", "Ubuntu-R.ttf",
                                    "FreeSans.ttf"};
      static const char* dirs[] = {"/usr/share/fonts", "/usr/share/fonts/truetype/dejavu",
                                   "/usr/share/fonts/truetype/liberation",
                                   "/usr/share/fonts/truetype/ubuntu",
                                   "/usr/share/fonts/truetype/freefont",
                                   "/usr/local/share/fonts"};
#endif
      for (const char* dir : dirs) {
        if (!std::filesystem::is_directory(dir)) {
          continue;
        }
        for (const char* name : names) {
          std::filesystem::path candidate = std::filesystem::path(dir) / name;
          if (std::filesystem::exists(candidate)) {
            std::cerr << "no font in theme; falling back to system font: " << candidate.string()
                      << std::endl;
            return candidate.string();
          }
        }
        // Also search one level of subdirectories (e.g. /usr/share/fonts/truetype/*).
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
          if (!entry.is_regular_file()) {
            continue;
          }
          for (const char* name : names) {
            if (entry.path().filename() == name) {
              std::cerr << "no font in theme; falling back to system font: "
                        << entry.path().string() << std::endl;
              return entry.path().string();
            }
          }
        }
      }
      std::cerr << "no font in theme and no system font found; text will not render"
                << std::endl;
      return std::string{};
    }();
    return result;
  }

  // Weighted pick over `weights`, skipping any entry `eligible` marks false (an EMPTY
  // `eligible` means every entry is). Returns size_t(-1) when nothing is selectable.
  //
  // Shared by the draw path and the load path deliberately: the tier scheme only works if
  // residency is built with the same distribution selection samples with, and two
  // hand-rolled copies of this loop are exactly how the two drifted apart in the first
  // place.
  std::size_t weighted_tier(const std::vector<uint32_t>& weights,
                            const std::vector<char>& eligible)
  {
    uint64_t total = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      if (eligible.empty() || eligible[i]) {
        total += weights[i];
      }
    }
    if (!total) {
      return static_cast<std::size_t>(-1);
    }
    auto r = static_cast<uint64_t>(random(static_cast<std::size_t>(total)));
    uint64_t running = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      if (!eligible.empty() && !eligible[i]) {
        continue;
      }
      running += weights[i];
      if (r < running) {
        return i;
      }
    }
    return static_cast<std::size_t>(-1);
  }
}

ThemeBank::ThemeBank(const std::string& root_path, const trance_pb::Session& session,
                     const trance_pb::System& system, const trance_pb::Program& program,
                     const std::map<std::string, std::vector<std::pair<std::string, uint32_t>>>&
                         theme_tiers)
: _root_path{root_path}
, _image_cache_size{system.image_cache_size()}
, _swaps_to_match_theme{0}
, _updates{0}
, _cooldown{switch_cooldown}
{
  // Find all images in all themes and set up data for each.
  std::unordered_set<std::string> all_image_paths;
  std::unordered_set<std::string> all_animation_paths;
  for (const auto& pair : session.theme_map()) {
    const auto& theme = pair.second;
    all_image_paths.insert(theme.image_path().begin(), theme.image_path().end());
    all_animation_paths.insert(theme.animation_path().begin(), theme.animation_path().end());
  }
  for (const auto& path : all_image_paths) {
    _all_images.push_back({path, 0, {}});
  }
  // path -> index, built once. The tier construction below needs this lookup per image
  // OCCURRENCE across every theme's resolved pool; doing it by linear scan of _all_images
  // is quadratic and lands in the hundreds of millions of string compares on a real
  // corpus (18k images, themes whose pools repeat their ancestors' paths).
  std::unordered_map<std::string, std::size_t> image_index;
  image_index.reserve(_all_images.size());
  for (std::size_t i = 0; i < _all_images.size(); ++i) {
    image_index[_all_images[i].path] = i;
  }
  _all_animations.insert(_all_animations.begin(), all_animation_paths.begin(),
                         all_animation_paths.end());
  _animation_dead.assign(_all_animations.size(), false);

  // Set up data for each theme.
  for (const auto& pair : session.theme_map()) {
    // Index lookup.
    _theme_map[pair.first] = _themes.size();
    const auto& theme = pair.second;

    std::unordered_set<std::string> images{theme.image_path().begin(), theme.image_path().end()};
    std::unordered_set<std::string> animations{theme.animation_path().begin(),
                                               theme.animation_path().end()};
    _themes.emplace_back(new ThemeInfo{images.size(),
                                       false,
                                       {},
                                       0,
                                       {},
                                       {_all_images.size()},
                                       {_all_images.size()},
                                       {_all_animations.size()},
                                       {theme.font_path().begin(), theme.font_path().end()},
                                       {theme.audio_path().begin(), theme.audio_path().end()},
                                       {theme.text_line().begin(), theme.text_line().end()},
                                       {},
                                       {static_cast<std::size_t>(theme.text_line().size())}});
    ThemeInfo& theme_info = *_themes.back();
    theme_info.name = pair.first;
    // Fonts and audio alone put nothing on screen, so they don't count. See set_program:
    // a theme that can't draw is kept out of the rotation entirely.
    theme_info.drawable =
        !images.empty() || !animations.empty() || !theme_info.text_lines.empty();
    // Disable images not in this theme in both shufflers so that they can
    // never be chosen.
    for (std::size_t i = 0; i < _all_images.size(); ++i) {
      if (images.count(_all_images[i].path)) {
        _themes.back()->load_shuffler.modify(i, last_image_count);
        _themes.back()->image_shuffler.modify(i, last_image_count);
      }
    }

    // Split the pool into tier shufflers. The tier spans describe image_path POSITIONS,
    // which is why this walks the repeated field rather than the deduped `images` set --
    // the set has neither order nor multiplicity. Only built when a theme actually has
    // more than one tier; a single-tier theme is exactly image_shuffler and gets nothing.
    auto tiers_it = theme_tiers.find(pair.first);
    if (tiers_it != theme_tiers.end() && tiers_it->second.size() > 1) {
      // Path -> index in _all_images, so each tier's members can be enabled by index.
      std::size_t offset = 0;
      for (const auto& tier : tiers_it->second) {
        Shuffler tier_shuffler{_all_images.size()};
        // Same membership, second copy: one shuffler tracks what is RESIDENT (the draw
        // side), the other what is still worth LOADING. They move in opposite directions
        // on the same event, so they cannot be one structure.
        Shuffler tier_load_shuffler{_all_images.size()};
        std::unordered_set<std::size_t> members;
        for (uint32_t n = 0; n < tier.second && offset < static_cast<std::size_t>(
                                                     theme.image_path_size());
             ++n, ++offset) {
          auto index_it = image_index.find(theme.image_path(static_cast<int>(offset)));
          if (index_it == image_index.end()) {
            continue;
          }
          // insert(), not unconditional modify(): the same path listed twice inside one
          // tier would otherwise be bumped to a higher priority than its peers and, since
          // next() draws from the top level only, monopolize that tier until recency
          // pulled it back down.
          if (members.insert(index_it->second).second) {
            tier_shuffler.modify(index_it->second, last_image_count);
            tier_load_shuffler.modify(index_it->second, last_image_count);
          }
        }
        // An empty tier (a parent folder with no images of its own) would otherwise be
        // selectable and hand back nothing.
        if (!members.empty()) {
          theme_info.tier_shufflers.push_back(std::move(tier_shuffler));
          theme_info.tier_load_shufflers.push_back(std::move(tier_load_shuffler));
          theme_info.tier_sources.push_back(tier.first);
          theme_info.tier_weights.push_back(1);
          theme_info.tier_loaded_count.push_back(0);
          theme_info.tier_members.push_back(std::move(members));
        }
      }
      // One surviving tier is not a hierarchy -- drop back to the flat shuffle.
      if (theme_info.tier_shufflers.size() < 2) {
        theme_info.tier_shufflers.clear();
        theme_info.tier_load_shufflers.clear();
        theme_info.tier_sources.clear();
        theme_info.tier_weights.clear();
        theme_info.tier_loaded_count.clear();
        theme_info.tier_members.clear();
      }
    }
    for (std::size_t i = 0; i < _all_animations.size(); ++i) {
      if (animations.count(_all_animations[i])) {
        _themes.back()->animation_shuffler.modify(i, 1);
      }
    }
    for (std::size_t i = 0; i < _themes.back()->text_lines.size(); ++i) {
      _themes.back()->text_lookup[_themes.back()->text_lines[i]].push_back(i);
    }
  }

  // Set the initially-enabled themes.
  for (std::size_t i = 0; i < _active_themes.size(); ++i) {
    _active_themes[i] = nullptr;
  }
  set_program(program);
  // Choose the initial active themes and load them up.
  for (std::size_t i = 0; i < _active_themes.size(); ++i) {
    advance_theme();
    if (i) {
      auto& theme = *_active_themes.back().load();
      while (!all_loaded()) {
        do_reconcile(theme);
      }
    }
  }

  _streamer.reset(new AsyncStreamer{[this] { return do_load_animation(false); },
                                    system.animation_buffer_size()});
  _alt_streamer.reset(new AsyncStreamer{[this] { return do_load_animation(true); },
                                        system.animation_buffer_size()});
}

const std::string& ThemeBank::get_root_path() const
{
  return _root_path;
}

ThemeBank::DebugSnapshot ThemeBank::debug_snapshot() const
{
  // Thread-safety: the per-slot reads below are safe against the async loader
  // thread (active-theme pointers and loaded_size are atomic; name/size are
  // immutable after construction). The enabled-weights/pinned fields are NOT
  // atomic and are only safe because they are mutated solely on the main/render
  // thread (set_program/change_themes) -- the same thread that calls this. Do
  // not call debug_snapshot() from another thread without guarding those.
  DebugSnapshot snapshot;
  for (std::size_t i = 0; i < _active_themes.size(); ++i) {
    const auto* theme = _active_themes[i].load();
    auto& slot = snapshot.slots[i];
    slot.valid = theme != nullptr;
    if (theme) {
      slot.name = theme->name;
      slot.loaded = uint32_t(theme->loaded_size.load());
      slot.total = uint32_t(theme->size);
    } else {
      slot.loaded = 0;
      slot.total = 0;
    }
  }
  for (const auto& pair : _enabled_theme_weights) {
    snapshot.enabled_weights.emplace_back(pair.first, pair.second);
  }
  snapshot.pinned = _pinned_theme;
  snapshot.image_cache_size = _image_cache_size;
  snapshot.swaps_to_match = _swaps_to_match_theme;
  return snapshot;
}

void ThemeBank::set_program(const trance_pb::Program& program)
{
  _global_fps = program.global_fps();
  _enabled_theme_weights.clear();
  _pinned_theme.clear();
  for (auto& theme : _themes) {
    theme->enabled = false;
  }
  for (const auto& theme : program.enabled_theme()) {
    // A row can name a theme that isn't in theme_map at all (a hand-edited session, or a
    // folder that went missing and left its enabled_theme row behind -- discovery keeps
    // those on purpose), and a theme that IS there can have nothing to draw: a directory
    // that turned out to be a pure container expands to an empty theme, as does one whose
    // drive is not mounted. Honouring the weight anyway hands that share of the rotation
    // to a husk that renders black, and the tier scheme makes the empty case ordinary
    // rather than exotic. Skipped at RUNTIME only -- the session keeps the row, so the
    // theme comes straight back the moment its content does.
    auto index_it = _theme_map.find(theme.theme_name());
    if (index_it == _theme_map.end() || !_themes[index_it->second]->drawable) {
      continue;
    }
    if (theme.random_weight()) {
      _enabled_theme_weights[theme.theme_name()] = theme.random_weight();
    }
    if (theme.pinned()) {
      _pinned_theme = theme.theme_name();
    }
    if (theme.random_weight() || theme.pinned()) {
      _themes[index_it->second]->enabled = true;
    }
  }
  // Refresh every tier's weight from the program's rotation weights. Deliberately the
  // SAME number the theme-rotation lottery uses: "how much do I want to see this" is one
  // idea, and a second per-theme knob would be one more slider on an already-crowded row
  // for a distinction nobody has needed yet. A tier whose source theme carries no weight
  // here (or is not enabled at all) falls back to 1 rather than 0 -- an inherited tier
  // going silent just because the parent is switched OFF as a live theme would be a
  // surprise, since inheriting it is a separate, explicit choice.
  for (auto& theme : _themes) {
    std::lock_guard<std::mutex> lock{theme->load_mutex};
    for (std::size_t i = 0; i < theme->tier_sources.size(); ++i) {
      auto weight_it = _enabled_theme_weights.find(theme->tier_sources[i]);
      theme->tier_weights[i] = weight_it != _enabled_theme_weights.end() && weight_it->second
          ? weight_it->second
          : 1;
    }
  }
  for (uint32_t i = 1; i < _active_themes.size(); ++i) {
    auto theme = _active_themes[i].load();
    if (theme && !theme->enabled) {
      _swaps_to_match_theme = std::max(_swaps_to_match_theme, i);
    }
  }
  if (!_pinned_theme.empty()) {
    auto pinned_index = _theme_map[_pinned_theme];
    uint32_t count = 0;
    for (uint32_t i = 1; i < _active_themes.size(); ++i) {
      if (_themes[pinned_index].get() == _active_themes[i]) {
        ++count;
      }
    }
    if (count < 2) {
      _swaps_to_match_theme = std::max(_swaps_to_match_theme, 3u);
    }
  }
}

void ThemeBank::advance_frames()
{
  _streamer->advance_frame(_global_fps, _change_animation, _animation_theme_changed);
  _alt_streamer->advance_frame(_global_fps, _alt_change_animation, _alt_animation_theme_changed);
  _change_animation = _alt_change_animation = false;
}

Image ThemeBank::get_image(bool alternate)
{
  auto& theme = *_active_themes[alternate ? 2 : 1].load();
  if (!theme.size) {
    return get_animation(alternate);
  }
  std::size_t index;
  Image image;
  {
    std::lock_guard<std::mutex> lock{theme.load_mutex};
    // Tiered pool: pick the SOURCE by rotation weight first, then an image within it.
    // Without this the union is sampled by raw file count, so the mix is decided by how
    // many files each folder happens to hold instead of by the weights -- with two
    // equal-sized folders weighted 4:1 you get 50/50, and a small folder inheriting a
    // large one is drowned entirely.
    //
    // Falls through to the flat shuffle when the theme has no tiers, when every tier
    // weight is zero, and when the chosen tier has nothing RESIDENT (rather than returning
    // nothing at all).
    index = static_cast<std::size_t>(-1);
    if (!theme.tier_shufflers.empty()) {
      static const std::vector<char> all_eligible;
      auto tier = weighted_tier(theme.tier_weights, all_eligible);
      if (tier != static_cast<std::size_t>(-1)) {
        index = theme.tier_shufflers[tier].next();
      }
    }
    // The miss case is the important half. A tier whose members are all unloaded hands
    // back a perfectly valid index, so testing for -1 alone never fired: every miss became
    // a repeated frame AND skipped the _last_images bookkeeping below, which left the same
    // tier free to miss again on the very next call.
    if (index >= _all_images.size() || !_all_images[index].image) {
      index = theme.image_shuffler.next();
    }
    if (index < _all_images.size() && _all_images[index].image) {
      do_video_upload(*_all_images[index].image);
      image = *_all_images[index].image;
    }
  }
  auto& last_good = _last_good_image[alternate ? 1 : 0];
  if (!image) {
    // Nothing drawable right now (e.g. every image in the theme failed to
    // load, or the shuffler fell through to an unloaded slot). Repeat the last
    // good image rather than handing back an empty one that draws black.
    return last_good ? last_good : get_animation(alternate);
  }
  last_good = image;
  _last_images.push_back(index);
  for (auto& other_theme : _themes) {
    std::lock_guard<std::mutex> lock{other_theme->load_mutex};
    other_theme->image_shuffler.decrease(_last_images.back());
    if (_last_images.size() > last_image_count) {
      other_theme->image_shuffler.increase(_last_images.front());
    }
    // Tier shufflers need the identical treatment: they are the structures actually being
    // drawn from now, so leaving them out would drop the anti-repeat entirely for every
    // inheriting theme and let it show the same image back to back.
    for (auto& tier : other_theme->tier_shufflers) {
      tier.decrease(_last_images.back());
      if (_last_images.size() > last_image_count) {
        tier.increase(_last_images.front());
      }
    }
  }
  if (_last_images.size() > last_image_count) {
    _last_images.erase(_last_images.begin());
  }
  return image;
}

Image ThemeBank::get_animation(bool alternate)
{
  return (alternate ? _alt_streamer : _streamer)->get_frame([&](const Image& image) {
    do_video_upload(image);
  });
}

const std::string& ThemeBank::get_text(bool alternate, bool exclusive)
{
  auto& theme = *_active_themes[alternate ? 2 : 1].load();
  if (theme.text_lines.empty()) {
    const static std::string none;
    return none;
  }
  if (!exclusive) {
    return theme.text_lines[theme.text_shuffler.next()];
  }
  auto text = theme.text_lines[theme.text_shuffler.next()];
  for (auto& other_theme : _themes) {
    auto it = other_theme->text_lookup.find(text);
    if (it != other_theme->text_lookup.end()) {
      for (auto index : it->second) {
        other_theme->text_shuffler.decrease(index);
      }
    }
    it = other_theme->text_lookup.find(_last_text);
    if (!_last_text.empty() && it != other_theme->text_lookup.end()) {
      for (auto index : it->second) {
        other_theme->text_shuffler.increase(index);
      }
    }
  }
  _last_text = text;
  return _last_text;
}

const std::string& ThemeBank::get_font(bool alternate)
{
  auto& theme = *_active_themes[alternate ? 2 : 1].load();
  if (theme.font_paths.empty()) {
    return find_system_font();
  }
  auto r = random(theme.font_paths.size());
  return theme.font_paths[r];
}

const std::string& ThemeBank::get_audio(bool alternate)
{
  auto& theme = *_active_themes[alternate ? 2 : 1].load();
  if (theme.audio_paths.empty()) {
    const static std::string none;
    return none;
  }
  auto r = random(theme.audio_paths.size());
  return theme.audio_paths[r];
}

void ThemeBank::change_animation(bool alternate)
{
  if (alternate) {
    _alt_change_animation = true;
  } else {
    _change_animation = true;
  }
}

bool ThemeBank::change_themes()
{
  if (!all_loaded() || !all_unloaded()) {
    return false;
  }
  _cooldown = switch_cooldown;
  advance_theme();
  if (_swaps_to_match_theme) {
    --_swaps_to_match_theme;
  }
  return true;
}

bool ThemeBank::swaps_to_match_theme() const
{
  return _swaps_to_match_theme;
}

uint32_t ThemeBank::cache_per_theme() const
{
  std::size_t enabled_themes = 0;
  for (const auto& theme : _themes) {
    if (theme->enabled) {
      ++enabled_themes;
    }
  }
  return enabled_themes == 0
      ? 0
      : _image_cache_size / uint32_t(std::min<std::size_t>(3, enabled_themes));
}

void ThemeBank::async_update()
{
  do_purge();
  if (_cooldown) {
    --_cooldown;
    return;
  }

  ++_updates;

  auto callback = [&](const Image& image) {
    _purge_mutex.lock();
    _purgeable_images.push_back(image.get_sf_image());
    _purge_mutex.unlock();
  };
  _streamer->async_update(callback);
  _alt_streamer->async_update(callback);
  // Swap some images from the active themes in and out every so often.
  if (_updates == 128) {
    do_swap(1);
    do_swap(2);
    _updates = 0;
  } else {
    do_reconcile(*_active_themes[1].load());
    do_reconcile(*_active_themes[2].load());
    if (!all_unloaded()) {
      do_unload(*_active_themes.front().load());
    }
    if (!all_loaded()) {
      do_load(*_active_themes.back().load());
    }
  }
}

void ThemeBank::advance_theme()
{
  std::size_t random_theme_index = 0;
  uint32_t total = 0;
  for (const auto& pair : _enabled_theme_weights) {
    total += pair.second;
  }
  if (!total) {
    random_theme_index = random(_themes.size());
  } else {
    auto r = random(total);
    uint32_t t = 0;
    for (const auto& pair : _enabled_theme_weights) {
      t += pair.second;
      if (r < t) {
        random_theme_index = _theme_map[pair.first];
        break;
      };
    }
  }
  // Override with pinned theme if last theme wasn't the pinned theme,
  // or if there are no other weights.
  if (!_pinned_theme.empty()) {
    auto pinned_index = _theme_map[_pinned_theme];
    if (!total || _themes[pinned_index].get() != _active_themes.back()) {
      random_theme_index = pinned_index;
    }
  }

  for (std::size_t i = 0; 1 + i < _active_themes.size(); ++i) {
    _active_themes[i].store(_active_themes[1 + i].load());
  }
  _active_themes.back() = _themes[random_theme_index].get();
  if (_active_themes[0].load() != _active_themes[1].load()) {
    _animation_theme_changed = true;
  }
  if (_active_themes[1].load() != _active_themes[2].load()) {
    _alt_animation_theme_changed = true;
  }
}

bool ThemeBank::all_loaded() const
{
  const auto& next_theme = *_active_themes.back().load();
  return next_theme.loaded_size >= next_theme.size || next_theme.loaded_size >= cache_per_theme();
}

bool ThemeBank::all_unloaded() const
{
  const auto& prev_theme = *_active_themes.front().load();
  std::size_t count = 0;
  for (const auto& active_theme : _active_themes) {
    if (active_theme.load() == &prev_theme) {
      ++count;
    }
  }
  return !prev_theme.loaded_size || count > 1;
}

void ThemeBank::do_swap(std::size_t active_theme_index)
{
  auto& theme = *_active_themes[active_theme_index].load();
  if (!theme.loaded_size || theme.loaded_size == theme.size) {
    return;
  }
  do_unload(theme);
  do_load(theme);
}

void ThemeBank::do_reconcile(ThemeInfo& theme)
{
  if (theme.loaded_size < cache_per_theme()) {
    do_load(theme);
  }
  if (theme.loaded_size > cache_per_theme()) {
    do_unload(theme);
  }
}

void ThemeBank::do_load(ThemeInfo& theme)
{
  if (theme.loaded_size >= theme.size) {
    return;
  }
  // Residency is built with the SAME weighted draw get_image selects with. Loading flat
  // over the merged pool spends cache slots in proportion to tier SIZE while selection
  // spends picks in proportion to tier WEIGHT, so the tier the weights favour is usually
  // not in RAM when it comes up: 10 images inheriting 280 at 4:1 with a 21-image cache
  // measured 0.9 of the 10 resident, 75% of picks missing, and the own tier taking 4% of
  // frames rather than 80%. Tiers with everything already resident are held out of the
  // draw so it can't spend the cache re-loading them.
  std::size_t index = static_cast<std::size_t>(-1);
  if (!theme.tier_load_shufflers.empty()) {
    // tier_weights is refreshed by set_program on the main thread under this same lock.
    std::lock_guard<std::mutex> lock{theme.load_mutex};
    std::vector<char> eligible(theme.tier_load_shufflers.size());
    for (std::size_t t = 0; t < eligible.size(); ++t) {
      eligible[t] = theme.tier_loaded_count[t] < theme.tier_members[t].size() ? 1 : 0;
    }
    auto tier = weighted_tier(theme.tier_weights, eligible);
    if (tier != static_cast<std::size_t>(-1)) {
      index = theme.tier_load_shufflers[tier].next();
    }
  }
  if (index >= _all_images.size()) {
    index = theme.load_shuffler.next();
  }
  theme.load_shuffler.decrease(index);
  for (std::size_t t = 0; t < theme.tier_load_shufflers.size(); ++t) {
    if (theme.tier_members[t].count(index)) {
      theme.tier_load_shufflers[t].decrease(index);
      ++theme.tier_loaded_count[t];
    }
  }
  theme.loaded_index.emplace_back(index);

  auto& image = _all_images[index];
  // Could store spare capacity due to duplicated images and load more. Might
  // get a bit confusing though.
  if (!image.use_count++ && !image.failed) {
    image.image.reset(new Image{load_image(_root_path + "/" + image.path)});
    if (!*image.image) {
      // Mark it dead: never retry the file, and never put the blank image in
      // the draw shuffler.
      // Also strip the base priority every theme image gets in image_shuffler
      // at construction -- otherwise the dead index still competes with
      // recently-drawn healthy images and starves rotation on small themes.
      image.failed = true;
      image.image.reset();
      for (auto& other_theme : _themes) {
        other_theme->load_shuffler.modify(index, -static_cast<int32_t>(last_image_count));
        std::lock_guard<std::mutex> lock{other_theme->load_mutex};
        other_theme->image_shuffler.modify(index, -static_cast<int32_t>(last_image_count));
        // Strip it from the tier shufflers too, and ONLY from the tiers that own it --
        // a tier left holding a dead index keeps offering an image that can never draw,
        // and one left holding it on the LOAD side keeps spending cache slots on it.
        for (std::size_t t = 0; t < other_theme->tier_shufflers.size(); ++t) {
          if (other_theme->tier_members[t].count(index)) {
            other_theme->tier_shufflers[t].modify(index, -static_cast<int32_t>(last_image_count));
            other_theme->tier_load_shufflers[t].modify(index,
                                                       -static_cast<int32_t>(last_image_count));
          }
        }
      }
    }
  }
  if (!image.failed) {
    std::lock_guard<std::mutex> lock{theme.load_mutex};
    theme.image_shuffler.increase(index);
    // Tier shufflers must track residency exactly as the flat one does. The whole scheme
    // rests on a LOADED image sitting one priority level above an unloaded one, so that
    // next() -- which draws from the top level only -- returns something that is actually
    // in RAM. Without this a tier offers its entire membership regardless of the cache,
    // and on any theme larger than the cache most picks land on an unloaded index and
    // get_image falls back to repeating the previous frame.
    for (std::size_t t = 0; t < theme.tier_shufflers.size(); ++t) {
      if (theme.tier_members[t].count(index)) {
        theme.tier_shufflers[t].increase(index);
      }
    }
  }
  // Failed loads still count towards loaded_size/loaded_index so the theme-swap
  // bookkeeping (all_loaded / do_unload) stays symmetric; they just never
  // become drawable.
  ++theme.loaded_size;
}

void ThemeBank::do_unload(ThemeInfo& theme)
{
  if (!theme.loaded_size) {
    return;
  }
  auto index = theme.loaded_index.front();
  theme.load_shuffler.increase(index);
  // Mirror of do_load's load-side bookkeeping: the image is a candidate for loading again,
  // and its tier has one fewer resident member (so it becomes eligible for the weighted
  // load draw again once it was full).
  for (std::size_t t = 0; t < theme.tier_load_shufflers.size(); ++t) {
    if (theme.tier_members[t].count(index)) {
      theme.tier_load_shufflers[t].increase(index);
      if (theme.tier_loaded_count[t]) {
        --theme.tier_loaded_count[t];
      }
    }
  }
  theme.loaded_index.erase(theme.loaded_index.begin());

  auto& image = _all_images[index];
  if (!image.failed) {
    std::lock_guard<std::mutex> lock{theme.load_mutex};
    theme.image_shuffler.decrease(index);
    // Mirror of the increase in do_load -- an unloaded image drops back below the loaded
    // ones so the tier stops offering it.
    for (std::size_t t = 0; t < theme.tier_shufflers.size(); ++t) {
      if (theme.tier_members[t].count(index)) {
        theme.tier_shufflers[t].decrease(index);
      }
    }
  }
  if (!--image.use_count && image.image) {
    _purge_mutex.lock();
    _purgeable_images.push_back(image.image->get_sf_image());
    _purge_mutex.unlock();
    image.image.reset();
  }
  --theme.loaded_size;
}

std::unique_ptr<Streamer> ThemeBank::do_load_animation(bool alternate)
{
  auto& theme = *_active_themes[alternate ? 2 : 1].load();
  auto index = theme.animation_shuffler.next();
  if (index >= _all_animations.size() || _animation_dead[index]) {
    return {};
  }

  auto streamer = load_animation(_root_path + "/" + _all_animations[index]);
  if (!streamer || !streamer->success()) {
    // Mark it dead and don't try to load again: AsyncStreamer::async_update
    // requests a replacement streamer every 10ms tick, so a retryable failure
    // means reopening/reparsing the bad file ~100x/second (CPU spikes). Also
    // never hand back a failed streamer -- advance_frame would immediately
    // churn it out again, repeating the reload cycle.
    _animation_dead[index] = true;
    streamer.reset();
    for (auto& other_theme : _themes) {
      other_theme->animation_shuffler.modify(index, -5);
    }
  }
  if (alternate) {
    _alt_animation_theme_changed = false;
  } else {
    _animation_theme_changed = false;
  }
  return streamer;
}

void ThemeBank::do_video_upload(const Image& image) const
{
  if (image.ensure_texture_uploaded()) {
    // Swap the sf::Image pointer so we can delete it on the async thread (see
    // do_purge() below).
    _purge_mutex.lock();
    _purgeable_images.push_back(image.get_sf_image());
    _purge_mutex.unlock();
    image.clear_sf_image();
  }
}

void ThemeBank::do_purge()
{
  _purge_mutex.lock();
  _purgeable_images.clear();
  _purge_mutex.unlock();
}
