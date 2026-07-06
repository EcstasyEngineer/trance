#include <common/common.h>
#include <common/session.h>
#include <common/session_archive.h>
#include <common/session_json.h>
#include <common/session_legacy.h>
#include <common/util.h>
#include <trance/director.h>
#include <trance/media/audio.h>
#include <trance/media/export.h>
#include <common/media/image.h>
#include <trance/net/command_channel.h>
#include <trance/net/command_protocol.h>
#include <trance/platform/system_control.h>
#include <trance/render/openvr.h>
#include <trance/render/render.h>
#include <trance/render/video_export.h>
#include <trance/theme_bank.h>
#include <trance/ui/app_ui.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
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

// Overlay mode: the window is click-through by design, so it can never receive
// its own quit hotkey (Escape, handled in handle_events() below never arrives). The
// out-of-window escapes are SystemControl (global Shift+F11 safety hotkey + Windows
// tray icon), the command channel, and SIGINT/SIGTERM -- the signals handled here
// as a clean flag flip (running = false), not abort/terminate, so the async thread/
// audio/window all still tear down via the normal play_session() exit path.
std::atomic<bool> g_overlay_stop_requested = false;

extern "C" void overlay_signal_handler(int)
{
  g_overlay_stop_requested = true;
}

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
                   Audio* audio, AppUi* app_ui)
{
  while (const std::optional event = window.pollEvent()) {
    // ImGui gets first look at input so clicks/typing inside its panels don't also
    // fall through to the F-key/Escape handling below. Null in VR/export mode; in
    // overlay mode the click-through window simply never delivers events to this
    // loop while the overlay is engaged.
    if (app_ui) {
      app_ui->process_event(window, *event);
    }
    const auto* key_pressed = event->getIf<sf::Event::KeyPressed>();
    if (event->is<sf::Event::Closed>() ||
        (key_pressed && key_pressed->code == sf::Keyboard::Key::Escape)) {
      running = false;
    }
    if (key_pressed && key_pressed->code == sf::Keyboard::Key::F1) {
      director.toggle_debug_overlay();
    }
    if (key_pressed && key_pressed->code == sf::Keyboard::Key::F2 && app_ui) {
      app_ui->toggle();
    }
    if (key_pressed && key_pressed->code == sf::Keyboard::Key::M && audio) {
      audio->ToggleMute();
    }
    if (const auto* resized = event->getIf<sf::Event::Resized>()) {
      glViewport(0, 0, resized->size.x, resized->size.y);
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

// Command channel runtime state (docs/spec-mcp-ambient-daemon.md): the mailbox is
// drained and every verb dispatched from the main loop, right after handle_events(), per
// the spec's threading invariant (sec 2) -- CommandChannel's reader thread only ever pushes
// raw lines; only this function (running on the render thread) touches Director/Audio.
// `paused` gates start/stop/pause/resume (director.update()/theme_bank->advance_frames()
// simply don't run while paused -- "keep it boring" per the spec's own framing, sec 1).
// `intensity` is still stub state: the spec (sec 4) explicitly scopes its actual wiring
// as TBD, so the verb is protocol-complete and just stores the value here for a future
// consumer. `overlay_on`/`overlay_opacity` are LIVE: the verbs (and the F2 UI's
// Overlay section) only write these two fields; play_session()'s per-frame apply seam
// reconciles the real window against them via apply_overlay_hints/clear_overlay_hints.
struct CommandRuntimeState {
  bool paused = false;
  float intensity = 1.f;
  bool overlay_on = false;
  float overlay_opacity = 0.35f;
  // `screenshot PATH`: consumed by the renderer's pre-display hook (which sees the fully
  // composited back buffer) on the next rendered frame; empty = nothing pending.
  std::string screenshot_path;
};

// pause/stop suspends the audible side (music channels + entrainment bed); the
// playlist clock is frozen separately in the main loop. Null audio (export mode)
// just flips the flag. Shared by the command verbs and the SystemControl
// (tray/hotkey) requests, so every control surface gets identical pause semantics.
void set_paused(CommandRuntimeState& state, Audio* audio, bool paused)
{
  if (paused == state.paused) {
    return;
  }
  state.paused = paused;
  if (audio) {
    paused ? audio->PauseAll() : audio->ResumeAll();
  }
}

std::string execute_command(const command_protocol::ParsedCommand& cmd, Director& director,
                            Audio* audio, const ThemeBank& themes, AppUi* app_ui,
                            bool screenshot_supported, CommandRuntimeState& state,
                            const std::chrono::steady_clock::time_point& start_time)
{
  using command_protocol::Verb;
  if (!cmd.ok) {
    return command_protocol::format_err(cmd.error);
  }
  switch (cmd.verb) {
  case Verb::kStart:
    set_paused(state, audio, false);
    return command_protocol::format_ok();
  case Verb::kStop:
    set_paused(state, audio, true);
    return command_protocol::format_ok();
  case Verb::kPause:
    set_paused(state, audio, true);
    return command_protocol::format_ok();
  case Verb::kResume:
    set_paused(state, audio, false);
    return command_protocol::format_ok();
  case Verb::kOverlayOn:
    state.overlay_on = true;
    return command_protocol::format_ok();
  case Verb::kOverlayOff:
    state.overlay_on = false;
    return command_protocol::format_ok();
  case Verb::kOverlayOpacity:
    state.overlay_opacity = cmd.number;
    return command_protocol::format_ok();
  case Verb::kIntensity:
    state.intensity = cmd.number;
    return command_protocol::format_ok();
  case Verb::kSet:
    // Settings surface is mid-migration (protobuf Program -> JSON; spec sec 4/9): no
    // key is wired yet, so every key is "unknown" until that migration lands and picks
    // the key names. Never a crash, per spec sec 3.
    return command_protocol::format_err("unknown key: " + cmd.key);
  case Verb::kGet:
    return command_protocol::format_err("unknown key: " + cmd.key);
  case Verb::kLoadPattern: {
    std::ifstream f{cmd.value};
    if (!f) {
      return command_protocol::format_err("load pattern: could not open " + cmd.value);
    }
    std::string source{std::istreambuf_iterator<char>{f}, std::istreambuf_iterator<char>{}};
    auto error = director.force_pattern_from_source(source, cmd.value);
    if (!error.empty()) {
      return command_protocol::format_err("load pattern: " + error);
    }
    return command_protocol::format_ok();
  }
  case Verb::kLoadSession:
    // No live session-swap path exists yet (play_session() binds one Session at startup;
    // the F2 UI mutates it in place but nothing can replace it wholesale) -- protocol-
    // complete stub until such a path exists.
    return command_protocol::format_err("load session: not yet supported (no live session "
                                        "reload path)");
  case Verb::kStatus: {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - start_time)
                      .count();
    std::ostringstream out;
    out << "visual=" << director.status_visual_name() << " bed="
        << (director.status_bed_active() ? "on" : "off") << " overlay="
        << (state.overlay_on ? "on" : "off") << " uptime=" << uptime;
    // ThemeBank's four queue slots (prev|primary|alternate|next), so an external
    // controller -- and the test harness -- can watch theme rotation happen.
    auto snap = themes.debug_snapshot();
    out << " themes=";
    for (std::size_t i = 0; i < snap.slots.size(); ++i) {
      out << (i ? "|" : "") << (snap.slots[i].valid ? snap.slots[i].name : "(empty)");
    }
    return command_protocol::format_ok(out.str());
  }
  case Verb::kUiOn:
  case Verb::kUiOff:
    if (!app_ui) {
      return command_protocol::format_err("ui: unavailable in this mode (VR/export)");
    }
    // `ui on` implies overlay off, same as the tray's "Show control panel": a
    // click-through window can't host an interactive panel, and showing one anyway
    // recreates the stranded-unclickable-panel bug through the side door.
    if (cmd.verb == Verb::kUiOn) {
      state.overlay_on = false;
    }
    app_ui->set_visible(cmd.verb == Verb::kUiOn);
    return command_protocol::format_ok();
  case Verb::kScreenshot:
    // Only ack when a hook that will actually consume the request is installed
    // (realtime screen renderer) -- an `ok` that never writes a file is a lie.
    if (!screenshot_supported) {
      return command_protocol::format_err("screenshot: unavailable in this mode (VR/export)");
    }
    state.screenshot_path = cmd.value;
    return command_protocol::format_ok("writing " + cmd.value + " after the next frame");
  case Verb::kUnknown:
    break;
  }
  return command_protocol::format_err("unhandled verb");
}

// Drains every line CommandChannel has received since the last frame, parses + dispatches
// each one, and replies exactly once per command -- spec sec 3 ("always exactly one line
// back per command line in"). Render-thread only (see CommandRuntimeState comment above).
void handle_commands(CommandChannel& channel, Director& director, Audio* audio,
                     const ThemeBank& themes, AppUi* app_ui, bool screenshot_supported,
                     CommandRuntimeState& state,
                     const std::chrono::steady_clock::time_point& start_time)
{
  for (const auto& command : channel.drain()) {
    auto parsed = command_protocol::parse_command(command.line);
    auto reply = execute_command(parsed, director, audio, themes, app_ui, screenshot_supported,
                                 state, start_time);
    channel.reply(command.conn_id, reply);
  }
}

// `session` is non-const (and `session_path`/`sidecar` are threaded through) for the F2
// UI: its Program/Themes sections mutate the proto in place and its Session section saves
// it back to disk with the sidecar (pattern files / scan-dir themes round-trip). Director/
// ThemeBank keep their const refs -- in-place field mutation keeps map value addresses
// stable, and the UI never reorders/erases map entries.
void play_session(const std::string& root_path, trance_pb::Session& session,
                  const std::string& session_path, SessionJsonSidecar& sidecar,
                  const trance_pb::System& system,
                  const std::map<std::string, std::string> variables,
                  const exporter_settings& settings,
                  const std::function<void(Director&)>& visual_override = {},
                  const OverlayConfig& overlay = {}, uint16_t command_port = 0)
{
  // Command channel: constructed here, before ThemeBank/renderer/window, so the socket
  // is live and testable (netcat/pytest, spec sec 6) even in configurations where window
  // creation would fail headlessly (e.g. no X11/DISPLAY) -- the reader thread has no
  // dependency on SFML or Director. Verb EXECUTION against Director still only happens
  // once the main loop below starts draining it, per the spec's threading invariant.
  std::unique_ptr<CommandChannel> command_channel;
  if (command_port) {
    try {
      command_channel.reset(new CommandChannel(command_port));
      std::cout << "command channel: listening on 127.0.0.1:" << command_port << std::endl;
    } catch (const std::runtime_error& e) {
      std::cerr << "command channel: " << e.what() << std::endl;
    }
  }
  CommandRuntimeState command_state;
  // Seed the live overlay state from the startup flags so `status` reports --overlay
  // mode correctly and the first runtime toggle diffs against reality (see the apply
  // seam in the main loop below).
  command_state.overlay_on = overlay.enabled;
  command_state.overlay_opacity = overlay.opacity;
  const auto command_start_time = std::chrono::steady_clock::now();

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
    renderer.reset(new ScreenRenderer(system, overlay));
  }

  Director director{session, system, *theme_bank, program(), *renderer};
  if (visual_override) {
    visual_override(director);
  }

  // Overlay mode is click-through, so it can never receive its own quit hotkey (see
  // overlay_signal_handler above) -- the ways out all live outside the window:
  // SystemControl's global Shift+F11 / tray icon, the command channel, or signals.
  if (overlay.enabled) {
    g_overlay_stop_requested = false;
    std::signal(SIGINT, overlay_signal_handler);
    std::signal(SIGTERM, overlay_signal_handler);
    std::cout << "overlay mode: stop with Shift+F11, the tray icon (Windows), or Ctrl+C"
              << std::endl;
  }

  // ImGui in-app UI, F2-toggled. Stood up for every realtime screen window,
  // INCLUDING --overlay startup mode: while the overlay is engaged the click-through
  // window never delivers input (the panel is unreachable, and the apply seam below
  // collapses it on engage), but SystemControl's safety hotkey / tray can disengage
  // the overlay at runtime, and the panel must exist to serve as the control surface
  // afterwards. Still not wired for VR (per-eye render path, no single flat 2D pass
  // to composite onto) or video-export mode (no interactive window loop).
  std::unique_ptr<AppUi> app_ui;
  if (realtime && !director.vr_enabled()) {
    // Mutable mirror of the program() lambda above, for the UI's Program/Themes
    // sections: resolves the ACTIVE program in the session's program_map, or nullptr
    // when the built-in default fallback is playing (the panel disables itself).
    auto active_program = [&]() -> trance_pb::Program* {
      if (!stack.back().item->has_standard()) {
        return nullptr;
      }
      auto it = session.mutable_program_map()->find(stack.back().item->standard().program());
      if (it == session.mutable_program_map()->end()) {
        return nullptr;
      }
      return &it->second;
    };
    // Live apply after a UI edit: the same ThemeBank/Director pair the playlist-
    // switch path in the while-loop below calls.
    auto on_program_change = [&] {
      theme_bank->set_program(program());
      director.set_program(program());
    };
    // Overlay callbacks: the UI's Overlay section reads/writes the same two
    // CommandRuntimeState fields the `overlay ...` verbs use; the apply seam in the
    // main loop below is the single place either path touches the actual window.
    auto get_overlay = [&command_state] {
      return std::make_pair(command_state.overlay_on, command_state.overlay_opacity);
    };
    auto set_overlay = [&command_state](bool on, float opacity) {
      command_state.overlay_on = on;
      command_state.overlay_opacity = opacity;
    };
    app_ui.reset(new AppUi(session, session_path, sidecar, on_program_change, active_program,
                           get_overlay, set_overlay));
    if (!app_ui->init(renderer->window())) {
      std::cerr << "warning: ImGui-SFML init failed; F2 UI unavailable this run" << std::endl;
      app_ui.reset();
    }
  }
  // The hook also serves the `screenshot` verb: it runs after the scene (and ImGui) draw
  // but before the buffer swap, so glReadPixels sees the exact composited frame -- works
  // even when the physical display is locked or there's no compositor to grab from.
  auto save_pending_screenshot = [&] {
    if (command_state.screenshot_path.empty()) {
      return;
    }
    const auto size = renderer->window().getSize();
    std::vector<std::uint8_t> pixels(std::size_t{size.x} * size.y * 4);
    glReadPixels(0, 0, GLsizei(size.x), GLsizei(size.y), GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    // GL reads bottom-up; sf::Image wants top-down.
    std::vector<std::uint8_t> flipped(pixels.size());
    const std::size_t stride = std::size_t{size.x} * 4;
    for (std::size_t y = 0; y < size.y; ++y) {
      std::copy_n(pixels.data() + (size.y - 1 - y) * stride, stride, flipped.data() + y * stride);
    }
    sf::Image image{sf::Vector2u{size.x, size.y}, flipped.data()};
    if (!image.saveToFile(command_state.screenshot_path)) {
      std::cerr << "screenshot: couldn't write " << command_state.screenshot_path << std::endl;
    }
    command_state.screenshot_path.clear();
  };
  const bool screenshot_supported = realtime && !director.vr_enabled();
  if (screenshot_supported) {
    renderer->set_ui_hook([&] {
      if (app_ui) {
        app_ui->render(renderer->window());
      }
      save_pending_screenshot();
    });
  }
  sf::Clock ui_clock;

  // System tray icon (Windows) + global Shift+F11 safety hotkey (Win32/X11): the
  // out-of-window control surface -- the only kind that keeps working while the
  // overlay is click-through. Realtime only; export mode has no window and no need
  // for a panic switch.
  std::unique_ptr<SystemControl> system_control;
  if (realtime) {
    system_control.reset(new SystemControl);
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

  // Live overlay: what's actually applied to the window right now, seeded from the
  // startup flags. The `overlay` verbs and the F2 UI's Overlay section only write
  // command_state; the apply seam in the loop below diffs against these two and pushes
  // any change onto the native window.
  bool overlay_applied = overlay.enabled;
  float overlay_opacity_applied = overlay.opacity;

  try {
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

    while (running && !g_overlay_stop_requested) {
      // (The overlay stop path exits via g_overlay_stop_requested with `running`
      // still true; it is flipped after the loop so the async ThemeBank thread's
      // `while (running)` loop terminates and the join at the bottom of
      // play_session is bounded.)
      handle_events(running, renderer->window(), director, audio.get(), app_ui.get());
      // Command channel: parse + dispatch every line received since last frame, right
      // after handle_events (spec sec 3: "Parse + dispatch happens in the drain loop --
      // main.cpp's per-frame loop, right after handle_events").
      if (command_channel) {
        handle_commands(*command_channel, director, audio.get(), *theme_bank, app_ui.get(),
                        screenshot_supported, command_state, command_start_time);
      }
      // SystemControl requests (tray menu / global Shift+F11): drained on the render
      // thread like the command mailbox above; both funnel into the same
      // CommandRuntimeState, and the apply seam below reconciles the window once.
      if (system_control) {
        for (auto request : system_control->drain()) {
          switch (request) {
          case ControlRequest::kSafety: {
            // Panic switch: overlay off, playback paused, control panel up. A second
            // press from that safe state quits outright.
            const bool already_safe = !command_state.overlay_on && command_state.paused;
            if (already_safe) {
              running = false;
              break;
            }
            command_state.overlay_on = false;
            set_paused(command_state, audio.get(), true);
            if (app_ui) {
              app_ui->set_visible(true);
            }
            break;
          }
          case ControlRequest::kOverlayToggle:
            command_state.overlay_on = !command_state.overlay_on;
            break;
          case ControlRequest::kPlayPauseToggle:
            set_paused(command_state, audio.get(), !command_state.paused);
            break;
          case ControlRequest::kShowUi:
            // A click-through window can't host an interactive panel; showing the
            // panel implies disengaging the overlay first.
            command_state.overlay_on = false;
            if (app_ui) {
              app_ui->set_visible(true);
            }
            break;
          case ControlRequest::kQuit:
            running = false;
            break;
          }
        }
        system_control->set_status(command_state.overlay_on, command_state.paused);
      }
      // Live overlay apply seam: the single reconcile point for the
      // `overlay on|off|opacity` verbs, the F2 UI's Overlay section, and the
      // SystemControl requests above (each only writes command_state). Realtime
      // only -- export mode has no real window to overlay.
      if (realtime &&
          (command_state.overlay_on != overlay_applied ||
           (command_state.overlay_on &&
            command_state.overlay_opacity != overlay_opacity_applied))) {
        if (command_state.overlay_on) {
          apply_overlay_hints(renderer->window().getNativeHandle(),
                              command_state.overlay_opacity);
          // Engaging makes the window click-through: collapse the F2 panel on the
          // way in. Leaving it up strands it on-screen -- drawn but unclickable
          // (and on Windows, once a click falls through and focus moves away, the
          // window can never be re-focused by clicking, so not even F2-the-key can
          // close it).
          if (app_ui && !overlay_applied) {
            app_ui->set_visible(false);
          }
        } else {
          clear_overlay_hints(renderer->window().getNativeHandle());
        }
        overlay_applied = command_state.overlay_on;
        overlay_opacity_applied = command_state.overlay_opacity;
      }

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

      // Paused: freeze the playlist clock too. time_since_switch is wall-clock, so
      // without this a paused session's items keep timing out and firing audio events /
      // program switches underneath the frozen visuals.
      if (command_state.paused) {
        last_playlist_switch += elapsed_ms;
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
        // frame; the next matching call retries. Nothing needs the success bool.
        theme_bank->change_themes();
      }

      bool update = false;
      bool continue_playing = true;
      // `pause`/`stop`: freeze the current frame -- program state retained (spec sec 4)
      // -- by simply not draining frames_this_loop while paused. The window still repaints
      // the last frame every iteration below (`!realtime` / event-driven), so a paused
      // process stays visibly alive, it just stops advancing.
      while (!command_state.paused && frames_this_loop > 0) {
        update = true;
        --frames_this_loop;
        continue_playing &= director.update();
        theme_bank->advance_frames();
      }
      if (!continue_playing) {
        break;
      }
      // With a live UI the window redraws every loop iteration (the panels must stay
      // responsive even when no visual frame elapsed / playback is paused); otherwise
      // keep the only-on-update behaviour. ImGui's frame starts here and the
      // renderer's pre-display hook draws it inside director.render(), between the
      // scene draw and the buffer swap -- exactly one display() per iteration (a
      // second display() makes the UI strobe at half rate and the scene ping-pong
      // one frame back every other swap).
      // A pending screenshot forces a render even when nothing else would repaint
      // (paused with no UI, e.g. overlay mode) -- the verb already ack'd, so the
      // hook must get a frame to consume it.
      const bool do_render =
          update || !realtime || app_ui != nullptr || !command_state.screenshot_path.empty();
      if (app_ui && do_render) {
        app_ui->update(renderer->window(), ui_clock.restart(), director, audio.get(),
                       *theme_bank);
      }
      if (do_render) {
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

  // The overlay signal path exits the loop via g_overlay_stop_requested with `running`
  // still true; flip it so the async ThemeBank thread (gated on `running`) terminates
  // and the join below cannot hang.
  running = false;

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
    if (!font.openFromFile(path)) {
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

// SessionArchive: bundles the session JSON and every file it references into a
// plain zip (src/common/session_archive.{h,cpp}). `session`/`root_path` (the already-
// loaded/validated in-memory session and its media root) aren't reused here -- the
// archive needs the pattern-file sidecar too (source_text alone can't recover a pattern's
// original `file` path), so export_session_archive reloads `session_path` itself with a
// sidecar rather than duplicating that load here.
int export_archive(const std::string& session_path, const std::string& archive_path) {
  std::string error;
  if (!export_session_archive(session_path, archive_path, error)) {
    std::cerr << "export_archive failed: " << error << std::endl;
    return 1;
  }
  std::cout << "wrote archive: " << archive_path << std::endl;
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
              "slow_flash, sub_text, flash_text, simple, super_parallel, animation, super_fast).");
DEFINE_string(pattern, "",
              "force every visual selection to a single v3 pattern loaded from this source file "
              "(parsed the same way as a program's custom_visual_pattern). Themes still come from "
              "the session/program as normal -- only the visual schedule is overridden. Mutually "
              "exclusive with --visual.");
DEFINE_bool(overlay, false,
           "v0 overlay click-through mode: borderless fullscreen always-on-top "
           "window, translucent, with input passing through to the desktop beneath. "
           "Whole-window opacity only (see --overlay_opacity). The window can't receive "
           "its own quit hotkey; stop with Shift+F11 (global safety hotkey), the tray "
           "icon (Windows), or Ctrl+C.");
DEFINE_double(overlay_opacity, 0.35,
             "overlay window opacity, 0 (fully transparent) to 1 (fully opaque). Only "
             "meaningful with --overlay.");
DEFINE_int32(command_port, 0,
            "command channel (docs/spec-mcp-ambient-daemon.md): TCP port to bind on "
            "127.0.0.1 for the localhost line-protocol control socket (start/stop/pause/"
            "resume, overlay on|off|opacity, intensity, set/get, load pattern|session, "
            "status). 0 (default) disables the channel entirely -- no socket is opened. "
            "Loopback-only, no auth: binding to 127.0.0.1 is the whole trust boundary "
            "(spec sec 2/9), so only enable this on a machine you trust everyone on.");

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
  // --overlay_opacity: validated eagerly here (a typo/out-of-range value should never
  // surface as a runtime surprise), same spirit as --visual below.
  if (FLAGS_overlay_opacity < 0. || FLAGS_overlay_opacity > 1.) {
    std::cerr << "error: --overlay_opacity must be between 0 and 1" << std::endl;
    return 1;
  }
  // --command_port: validated eagerly here for the same reason -- a negative or
  // out-of-uint16_t-range value should never surface as a runtime surprise (e.g. silently
  // truncating). 0 means disabled, per the flag's own default/help text.
  if (FLAGS_command_port < 0 || FLAGS_command_port > 65535) {
    std::cerr << "error: --command_port must be between 0 and 65535" << std::endl;
    return 1;
  }
  OverlayConfig overlay;
  overlay.enabled = FLAGS_overlay;
  overlay.opacity = static_cast<float>(FLAGS_overlay_opacity);
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
  // Loaded WITH the sidecar (pattern file paths / theme scan dirs) so the F2 UI's
  // Save writes those back as references instead of freezing them inline.
  SessionJsonSidecar sidecar;
  try {
    session = load_session(session_path, sidecar);
  } catch (std::runtime_error& e) {
    // An EXPLICITLY named session that fails to load (legacy .session needing
    // trance_convert, JSON typo, missing file) must be a fatal error -- silently playing
    // default content instead of what was asked for is worse than exiting.
    // Same for an EXISTING ./default.json that fails to parse: overwriting a
    // hand-edited-but-broken file with a generated one would destroy the user's edits.
    if (argc >= 2 || std::filesystem::exists(DEFAULT_SESSION_PATH)) {
      std::cerr << "error: " << e.what() << std::endl;
      return 1;
    }
    // No-arg cold start with no ./default.json: bootstrap one, the same role
    // ./default.session played in the original trance.exe. A legacy ./default.session
    // sitting here is auto-migrated (converted in place, original left untouched);
    // otherwise generate the built-in default over whatever media the directory holds.
    if (std::filesystem::exists(LEGACY_DEFAULT_SESSION_PATH)) {
      std::cout << "migrating legacy ./" << LEGACY_DEFAULT_SESSION_PATH << " -> ./"
                << DEFAULT_SESSION_PATH << std::endl;
      session = load_legacy_session(LEGACY_DEFAULT_SESSION_PATH);
      validate_session(session);
    } else {
      std::cerr << e.what() << std::endl;
      session = get_default_session();
      search_resources(session, ".");
    }
    try {
      save_session(session, "./" + DEFAULT_SESSION_PATH);
      std::cout << "wrote ./" << DEFAULT_SESSION_PATH << std::endl;
      // Play what was WRITTEN, not the in-memory legacy proto: the JSON saver
      // normalizes Windows backslash media paths to forward slashes (spec sec 1),
      // and the legacy-authored originals don't resolve on non-Windows. Reset the
      // sidecar first -- the failed initial load may have partially filled it.
      sidecar = SessionJsonSidecar{};
      session = load_session("./" + DEFAULT_SESSION_PATH, sidecar);
    } catch (const std::runtime_error& save_error) {
      // Read-only directory: still playable this run, just not persisted.
      std::cerr << "couldn't write ./" << DEFAULT_SESSION_PATH << ": " << save_error.what()
                << std::endl;
    }
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
    return export_archive(session_path, FLAGS_export_archive);
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
  play_session(root_path, session, session_path, sidecar, system, variables, settings,
              visual_override, overlay, uint16_t(FLAGS_command_port));
  return 0;
}
