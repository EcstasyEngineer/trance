#include <common/common.h>
#include <common/session.h>
#include <common/util.h>
#include <trance/director.h>
#include <trance/media/audio.h>
#include <trance/media/export.h>
#include <common/media/image.h>
#include <trance/render/openvr.h>
#include <trance/render/render.h>
#include <trance/render/video_export.h>
#include <trance/theme_bank.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <gflags/gflags.h>
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#pragma warning(pop)

std::string next_playlist_item(const std::map<std::string, std::string>& variables,
                               const trance_pb::PlaylistItem* item)
{
  uint32_t total = 0;
  for (const auto& next : item->next_item()) {
    total += (is_enabled(next, variables) ? next.random_weight() : 0);
  }
  if (!total) {
    return {};
  }
  auto r = random(total);
  total = 0;
  for (const auto& next : item->next_item()) {
    total += (is_enabled(next, variables) ? next.random_weight() : 0);
    if (r < total) {
      return next.playlist_item_name();
    }
  }
  return {};
}

static const std::string bad_alloc = "OUT OF MEMORY! TRY REDUCING USAGE IN SETTINGS...";
static const uint32_t async_millis = 10;

std::thread run_async_thread(std::atomic<bool>& running, ThemeBank& bank)
{
  // Run the asynchronous load/unload thread.
  return std::thread{[&] {
    while (running) {
      try {
        bank.async_update();
      } catch (std::bad_alloc&) {
        std::cerr << bad_alloc << std::endl;
        running = false;
        throw;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(async_millis));
    }
  }};
}

void handle_events(std::atomic<bool>& running, sf::RenderWindow& window, Director& director,
                   Audio* audio)
{
  sf::Event event;
  while (window.pollEvent(event)) {
    if (event.type == event.Closed ||
        (event.type == event.KeyPressed && event.key.code == sf::Keyboard::Escape)) {
      running = false;
    }
    if (event.type == event.KeyPressed && event.key.code == sf::Keyboard::F1) {
      director.toggle_debug_overlay();
    }
    if (event.type == event.KeyPressed && event.key.code == sf::Keyboard::M && audio) {
      audio->ToggleMute();
    }
    if (event.type == sf::Event::Resized) {
      glViewport(0, 0, event.size.width, event.size.height);
    }
  }
}

void print_info(double elapsed_seconds, uint64_t frames, uint64_t total_frames)
{
  float completion = float(frames) / total_frames;
  auto elapsed = uint64_t(elapsed_seconds + .5);
  auto eta = uint64_t(.5 + (completion ? elapsed_seconds * (1. / completion - 1.) : 0.));
  auto percentage = uint64_t(100 * completion);

  std::cout << std::endl
            << "frame: " << frames << " / " << total_frames << " [" << percentage
            << "%]; elapsed: " << format_time(elapsed, true) << "; eta: " << format_time(eta, true)
            << std::endl;
}

void play_session(const std::string& root_path, const trance_pb::Session& session,
                  const trance_pb::System& system,
                  const std::map<std::string, std::string> variables,
                  const exporter_settings& settings,
                  const std::function<void(Director&)>& visual_override = {})
{
  struct PlayStackEntry {
    const trance_pb::PlaylistItem* item;
    int subroutine_step;
  };
  std::vector<PlayStackEntry> stack;
  stack.push_back({&session.playlist().find(session.first_playlist_item())->second, 0});

  auto program = [&]() -> const trance_pb::Program& {
    static const auto default_session = get_default_session();
    static const auto default_program = default_session.program_map().find("default")->second;
    if (!stack.back().item->has_standard()) {
      return default_program;
    }
    auto it = session.program_map().find(stack.back().item->standard().program());
    if (it == session.program_map().end()) {
      return default_program;
    }
    return it->second;
  };

  auto theme_bank = std::make_unique<ThemeBank>(root_path, session, system, program());

  std::unique_ptr<Renderer> renderer;
  bool realtime = settings.path.empty();
  if (!realtime) {
    renderer.reset(new VideoExportRenderer(settings));
  } else if (system.renderer() == trance_pb::System::OPENVR) {
    auto openvr = new OpenVrRenderer(system);
    renderer.reset(openvr);
    if (!openvr->success()) {
      renderer.reset();
    }
  }
  // System::OCULUS (LibOVR) support was removed; old sessions requesting it fall
  // through to the screen renderer below.
  if (!renderer) {
    renderer.reset(new ScreenRenderer(system));
  }

  Director director{session, system, *theme_bank, program(), *renderer};
  if (visual_override) {
    visual_override(director);
  }

  std::thread async_thread;
  std::atomic<bool> running = true;
  std::unique_ptr<Audio> audio;
  if (realtime) {
    async_thread = run_async_thread(running, *theme_bank);
    audio.reset(new Audio{root_path});
    audio->TriggerEvents(*stack.back().item);
    audio->SetEntrainment(program().entrainment());
    director.set_audio(audio.get());
  }
  std::cout << std::endl << "-> " << session.first_playlist_item() << std::endl;

  try {
    float update_time = 0.f;
    float playlist_item_time = 0.f;

    uint64_t elapsed_export_frames = 0;
    uint64_t async_update_residual = 0;
    double elapsed_frames_residual = 0;
    std::chrono::high_resolution_clock clock;
    auto true_clock_time = [&] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(clock.now().time_since_epoch())
          .count();
    };
    auto clock_time = [&] {
      if (realtime) {
        return true_clock_time();
      }
      // Match true_clock_time()'s return type exactly so the lambda's two
      // branches deduce a consistent type (chrono::milliseconds::rep differs
      // between MSVC's `long long` and gcc/Linux's `long`).
      return static_cast<decltype(true_clock_time())>(1000. * elapsed_export_frames /
                                                      double(settings.fps));
    };
    const auto true_clock_start = true_clock_time();
    auto last_clock_time = clock_time();
    auto last_playlist_switch = clock_time();

    while (running) {
      handle_events(running, renderer->window(), director, audio.get());

      // TODO: should sleep rather than spinning.
      uint32_t frames_this_loop = 0;
      auto t = clock_time();
      auto elapsed_ms = t - last_clock_time;
      last_clock_time = t;
      elapsed_frames_residual += double(program().global_fps()) * double(elapsed_ms) / 1000.;
      while (elapsed_frames_residual >= 1.) {
        --elapsed_frames_residual;
        ++frames_this_loop;
      }
      ++elapsed_export_frames;

      if (!realtime) {
        auto total_export_frames = uint64_t(settings.length) * uint64_t(settings.fps);
        if (elapsed_export_frames % 8 == 0) {
          auto elapsed_seconds = double(true_clock_time() - true_clock_start) / 1000.;
          print_info(elapsed_seconds, elapsed_export_frames, total_export_frames);
        }
        if (elapsed_export_frames >= total_export_frames) {
          running = false;
          break;
        }
      }

      async_update_residual += uint64_t(1000. * frames_this_loop / double(settings.fps));
      while (!realtime && async_update_residual >= async_millis) {
        async_update_residual -= async_millis;
        theme_bank->async_update();
      }

      while (true) {
        auto time_since_switch = clock_time() - last_playlist_switch;
        auto& entry = stack.back();
        // Continue if we're in a standard playlist item.
        if (entry.item->has_standard() &&
            time_since_switch < 1000 * entry.item->standard().play_time_seconds()) {
          break;
        }
        // Trigger the next item of a subroutine.
        if (entry.item->has_subroutine() &&
            entry.subroutine_step < entry.item->subroutine().playlist_item_name_size()) {
          if (stack.size() >= MAXIMUM_STACK) {
            std::cerr << "error: subroutine stack overflow\n";
            entry.subroutine_step = entry.item->subroutine().playlist_item_name_size();
          } else {
            last_playlist_switch = clock_time();
            auto name = entry.item->subroutine().playlist_item_name(entry.subroutine_step);
            stack.push_back({&session.playlist().find(name)->second, 0});
            if (realtime) {
              audio->TriggerEvents(*stack.back().item);
            }
            std::cout << "\n-> " << name << std::endl;
            theme_bank->set_program(program());
            director.set_program(program());
            if (realtime) {
              audio->SetEntrainment(program().entrainment());
            }
            ++stack[stack.size() - 2].subroutine_step;
            continue;
          }
        }
        auto next = next_playlist_item(variables, entry.item);
        // Finish a subroutine.
        if (next.empty() && stack.size() > 1) {
          stack.pop_back();
          continue;
        } else if (next.empty()) {
          break;
        }
        // Trigger the next item of a standard playlist item.
        last_playlist_switch = clock_time();
        stack.back().item = &session.playlist().find(next)->second;
        stack.back().subroutine_step = 0;
        if (realtime) {
          audio->TriggerEvents(*entry.item);
        }
        std::cout << "\n-> " << next << std::endl;
        theme_bank->set_program(program());
        director.set_program(program());
        if (realtime) {
          audio->SetEntrainment(program().entrainment());
        }
      }
      if (theme_bank->swaps_to_match_theme()) {
        // Fire-and-forget by design: a swap that isn't ready simply doesn't happen this
        // frame; the next matching call retries. Nothing needs the success bool (#25).
        theme_bank->change_themes();
      }

      bool update = false;
      bool continue_playing = true;
      while (frames_this_loop > 0) {
        update = true;
        --frames_this_loop;
        continue_playing &= director.update();
        theme_bank->advance_frames();
      }
      if (!continue_playing) {
        break;
      }
      if (update || !realtime) {
        director.render();
      }
      if (realtime) {
        audio->Update();
      }
    }
  } catch (std::bad_alloc&) {
    std::cerr << bad_alloc << std::endl;
    throw;
  }

  if (realtime) {
    async_thread.join();
  }
  renderer->window().close();
}

std::map<std::string, std::string> parse_variables(const std::string& variables)
{
  std::map<std::string, std::string> result;
  std::vector<std::string> current;
  current.emplace_back();
  bool escaped = false;
  for (char c : variables) {
    if (c == '\\' && !escaped) {
      escaped = true;
      continue;
    }
    if (escaped) {
      if (c == '\\') {
        current.back() += '\\';
      } else if (c == ';') {
        current.back() += ';';
      } else if (c == '=') {
        current.back() += '=';
      } else {
        std::cerr << "couldn't parse variables: " << variables << std::endl;
        return {};
      }
      escaped = false;
      continue;
    }
    if (c == '=' && current.size() == 1 && !current.back().empty()) {
      current.emplace_back();
      continue;
    }
    if (c == ';' && current.size() == 2 && !current.back().empty()) {
      result[current.front()] = current.back();
      current.clear();
      current.emplace_back();
      continue;
    }
    if (c != '=' && c != ';') {
      current.back() += c;
      continue;
    }
    std::cerr << "couldn't parse variables: " << variables << std::endl;
    return {};
  }
  if (current.size() == 2 && !current.back().empty()) {
    result[current.front()] = current.back();
    current.clear();
    current.emplace_back();
  }
  if (current.size() == 1 && current.back().empty()) {
    return result;
  }
  std::cerr << "couldn't parse variables: " << variables << std::endl;
  return {};
}

int validate_session(const std::string& root_path, const trance_pb::Session& session)
{
  // TODO: report unused files or incorrect extensions.
  std::set<std::string> image_paths;
  std::set<std::string> animation_paths;
  std::set<std::string> font_paths;
  for (const auto& pair : session.theme_map()) {
    for (const auto& path : pair.second.image_path()) {
      image_paths.insert(root_path + "/" + path);
    }
    for (const auto& path : pair.second.animation_path()) {
      animation_paths.insert(root_path + "/" + path);
    }
    for (const auto& path : pair.second.font_path()) {
      font_paths.insert(root_path + "/" + path);
    }
  }

  std::set<std::string> broken_paths;
  for (const auto& path : image_paths) {
    std::cout << "checking " << path << std::endl;
    Image image = load_image(path);
    if (!image) {
      broken_paths.insert(path);
      std::cerr << path << " failed to load" << std::endl;
    }
  }
  for (const auto& path : animation_paths) {
    std::cout << "checking " << path << std::endl;
    auto streamer = load_animation(path);
    while (true) {
      if (!streamer || !streamer->success()) {
        broken_paths.insert(path);
        std::cerr << path << " failed to load" << std::endl;
        break;
      }
      if (!streamer->next_frame()) {
        break;
      }
    }
  }
  for (const auto& path : font_paths) {
    std::cout << "checking " << path << std::endl;
    sf::Font font;
    if (!font.loadFromFile(path)) {
      broken_paths.insert(path);
      std::cerr << path << " failed to load" << std::endl;
    }
  }

  if (!broken_paths.empty()) {
    std::cout << std::endl;
  }
  for (const auto& path : broken_paths) {
    std::cerr << "FAILED: " << path << std::endl;
  }
  std::cout << "press any key to continue..." << std::endl;
  char c;
  std::cin >> c;
  return broken_paths.empty() ? 0 : 1;
}

int export_archive(const std::string& root_path, const trance_pb::Session& session,
                   const std::string& archive_path) {
  return 0;
}

DEFINE_bool(validate_session, false, "validate session");
DEFINE_string(export_archive, "", "export archive to this path");
DEFINE_string(variables, "", "semicolon-separated list of key=value variable assignments");
DEFINE_string(export_path, "", "export video to this path");
DEFINE_bool(export_3d, false, "export side-by-side 3D video");
DEFINE_uint64(export_width, 1280, "export video resolution width");
DEFINE_uint64(export_height, 720, "export video resolution height");
DEFINE_uint64(export_fps, 60, "export video frames per second");
DEFINE_uint64(export_length, 300, "export video length in seconds");
DEFINE_uint64(export_quality, 2, "export video quality (0 to 4, 0 is best)");
DEFINE_uint64(export_threads, 4, "export video threads");
DEFINE_string(visual, "",
              "force every visual selection to this one built-in (by its v3 name: accelerate, "
              "slow_flash, sub_text, flash_text, simple, super_parallel, animation, super_fast). "
              "Replaces the old workflow of zeroing out every other weight in default.session.");
DEFINE_string(pattern, "",
              "force every visual selection to a single v3 pattern loaded from this source file "
              "(parsed the same way as a program's custom_visual_pattern). Themes still come from "
              "the session/program as normal -- only the visual schedule is overridden. Mutually "
              "exclusive with --visual.");

namespace
{
  // Program::VisualType enum values (trance.proto) keyed by their v3 built-in name
  // (builtin_patterns_v3.cpp's `pattern NAME for ...` declarations). Kept local to
  // main.cpp: this is the CLI-facing name table for --visual, not a runtime concept
  // the rest of the program needs.
  const std::vector<std::pair<std::string, uint32_t>>& visual_name_table()
  {
    static const std::vector<std::pair<std::string, uint32_t>> table = {
        {"accelerate", 1},
        {"slow_flash", 2},
        {"sub_text", 3},
        {"flash_text", 4},
        {"simple", 5},
        {"super_parallel", 6},
        {"animation", 7},
        {"super_fast", 8},
    };
    return table;
  }

  // Fatal, startup-time error for a bad --visual name: lists every valid name so the
  // failure is immediately actionable instead of a runtime surprise later.
  [[noreturn]] void fatal_bad_visual_name(const std::string& name)
  {
    std::cerr << "error: --visual '" << name << "' is not a known built-in visual. Valid names: ";
    bool first = true;
    for (const auto& pair : visual_name_table()) {
      if (!first) {
        std::cerr << ", ";
      }
      std::cerr << pair.first;
      first = false;
    }
    std::cerr << std::endl;
    std::exit(1);
  }

  std::string read_file(const std::string& path)
  {
    std::ifstream f{path};
    if (!f) {
      std::cerr << "error: --pattern '" << path << "' could not be opened" << std::endl;
      std::exit(1);
    }
    return std::string{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
  }
}

int main(int argc, char** argv)
{
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc > 3) {
    std::cerr << "usage: " << argv[0] << " [session.cfg [system.cfg]]" << std::endl;
    return 1;
  }
  if (!FLAGS_visual.empty() && !FLAGS_pattern.empty()) {
    std::cerr << "error: --visual and --pattern are mutually exclusive" << std::endl;
    return 1;
  }
  // --visual: resolved (and fatal-errors on an unknown name) here at startup, not at
  // first selection -- a typo should never surface as a runtime surprise.
  uint32_t forced_visual_type = 0;
  if (!FLAGS_visual.empty()) {
    for (const auto& pair : visual_name_table()) {
      if (pair.first == FLAGS_visual) {
        forced_visual_type = pair.second;
        break;
      }
    }
    if (!forced_visual_type) {
      fatal_bad_visual_name(FLAGS_visual);
    }
  }
  // --pattern: the file is read eagerly (a missing file is also a startup-time fatal
  // error); the actual v3 parse happens once the Director exists, since it needs the
  // program's locked entrainment period the same way a custom_visual_pattern does.
  std::string forced_pattern_source;
  if (!FLAGS_pattern.empty()) {
    forced_pattern_source = read_file(FLAGS_pattern);
  }

  auto variables = parse_variables(FLAGS_variables);
  for (const auto& pair : variables) {
    std::cout << "variable " << pair.first << " = " << pair.second << std::endl;
  }

  exporter_settings settings{FLAGS_export_path,
                             FLAGS_export_3d,
                             uint32_t(FLAGS_export_width),
                             uint32_t(FLAGS_export_height),
                             uint32_t(FLAGS_export_fps),
                             uint32_t(FLAGS_export_length),
                             std::min(uint32_t(4), uint32_t(FLAGS_export_quality)),
                             uint32_t(FLAGS_export_threads)};

  std::string session_path{argc >= 2 ? argv[1] : "./" + DEFAULT_SESSION_PATH};
  trance_pb::Session session;
  try {
    session = load_session(session_path);
  } catch (std::runtime_error& e) {
    std::cerr << e.what() << std::endl;
    session = get_default_session();
    search_resources(session, ".");
  }

  std::string system_path{argc >= 3 ? argv[2] : "./" + SYSTEM_CONFIG_PATH};
  trance_pb::System system;
  try {
    system = load_system(system_path);
  } catch (std::runtime_error& e) {
    std::cerr << e.what() << std::endl;
    system = get_default_system();
    save_system(system, system_path);
  }

  auto root_path = std::filesystem::path{session_path}.parent_path().string();
  if (FLAGS_validate_session) {
    return validate_session(root_path, session);
  }
  if (!FLAGS_export_archive.empty()) {
    return export_archive(root_path, session, FLAGS_export_archive);
  }

  std::function<void(Director&)> visual_override;
  if (forced_visual_type) {
    std::cout << "-> forcing every visual to built-in '" << FLAGS_visual << "'" << std::endl;
    visual_override = [forced_visual_type](Director& director) {
      director.force_builtin_visual(forced_visual_type);
    };
  } else if (!FLAGS_pattern.empty()) {
    std::cout << "-> forcing every visual to --pattern '" << FLAGS_pattern << "'" << std::endl;
    visual_override = [&forced_pattern_source](Director& director) {
      auto error = director.force_pattern_from_source(forced_pattern_source, FLAGS_pattern);
      if (!error.empty()) {
        std::cerr << "error: --pattern '" << FLAGS_pattern << "' " << error << std::endl;
        std::exit(1);
      }
    };
  }
  play_session(root_path, session, system, variables, settings, visual_override);
  return 0;
}
