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
#include <trance/platform/overlay_hints.h>
#include <trance/playlist_runner.h>
#include <trance/platform/system_control.h>
#include <trance/render/openvr.h>
#include <trance/render/openxr.h>
#include <trance/render/render.h>
#include <trance/render/video_export.h>
#include <trance/runtime_state.h>
#include <trance/theme_bank.h>
#include <trance/ui/app_ui.h>
#include <trance/visual/builtin_visuals.h>
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

// Overlay mode: the window is click-through by design, so it can never receive its
// own in-window input (the Escape/F2 panel toggle in handle_events() below never
// arrives, nor does the window close button). The out-of-window escapes are
// SystemControl (global Shift+F11 hide-everything toggle + Windows tray icon, whose
// Quit item is the quit path), the command channel, and SIGINT/SIGTERM -- the signals
// handled here as a clean flag flip (running = false), not abort/terminate, so the
// async thread/audio/window all still tear down via the normal play_session() exit
// path.
std::atomic<bool> g_overlay_stop_requested = false;

extern "C" void overlay_signal_handler(int)
{
  g_overlay_stop_requested = true;
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

void show_control_panel(CommandRuntimeState& state, AppUi* app_ui);

void handle_events(std::atomic<bool>& running, sf::RenderWindow& window, Director& director,
                   Audio* audio, AppUi* app_ui, CommandRuntimeState& state)
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
    if (event->is<sf::Event::Closed>()) {
      running = false;
    }
    if (key_pressed && key_pressed->code == sf::Keyboard::Key::F1) {
      director.toggle_debug_overlay();
    }
    // Escape quits, like the window close button above. Not while ImGui wants text
    // input: Escape's standard meaning inside an active InputText (e.g. the Session
    // section's Save As field) is "cancel the edit" -- ImGui already consumed it via
    // process_event above, and it must not ALSO tear down the app. With no panel at
    // all (VR/export) there is no edit to cancel, so Escape still quits.
    if (key_pressed && key_pressed->code == sf::Keyboard::Key::Escape &&
        !(app_ui && app_ui->wants_text_input())) {
      running = false;
    }
    // F2 toggles the control panel -- open if closed, close if open. Same text-input
    // carve-out: F2 is not a text key, but a panel that owns the keyboard keeps it.
    // Opening goes through show_control_panel() rather than a bare toggle so it obeys
    // the one policy every show-the-panel path obeys: disengage the overlay first, and
    // take focus (#39). A bare toggle here could open the panel over a click-through
    // window -- drawn, unclickable, and with the seam none the wiser.
    if (key_pressed && key_pressed->code == sf::Keyboard::Key::F2 && app_ui &&
        !app_ui->wants_text_input()) {
      if (app_ui->visible()) {
        app_ui->set_visible(false);
      } else {
        show_control_panel(state, app_ui);
      }
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

// Command dispatch threading (docs/spec-mcp-ambient-daemon.md sec 2): CommandChannel's
// reader thread only ever pushes raw lines; the mailbox is drained and every verb
// dispatched from the main loop right after handle_events(), so only the render thread
// touches Director/Audio. The shared state lives in trance/runtime_state.h.

// The one policy invariant shared by every "show the panel" path (`ui on` verb, the
// tray's Show-control-panel item): a click-through window can't host an
// interactive panel, so showing the panel implies disengaging the overlay first --
// showing one anyway strands it on-screen, drawn but unclickable (and on Windows, once
// a click falls through and focus moves away, the window can never be re-focused by
// clicking, so not even F2-the-key could close it). Showing the panel likewise implies
// un-hiding: a panel on an invisible window is equally unreachable.
// Showing the panel also implies TAKING FOCUS: the paths that get here left the window
// unfocused by construction (the tray menu foregrounds its hidden helper window; the
// Win32 overlay clear restores styles without activation), and imgui-SFML drops every
// mouse event while its focus latch is false -- a drawn-but-unclickable panel. Only the
// request is recorded here; the apply seam grabs focus once the window is actually
// interactive again (#39).
void show_control_panel(CommandRuntimeState& state, AppUi* app_ui)
{
  state.overlay_on = false;
  state.hidden = false;
  state.focus_requested = true;
  if (app_ui) {
    app_ui->set_visible(true);
  }
}

// pause/stop suspends the audible side (music channels + entrainment bed); the
// playlist clock is frozen separately in the main loop. Null audio (export mode)
// just flips the flag. Shared by the command verbs and the SystemControl
// (tray/hotkey) requests, so every control surface gets identical pause semantics.
// While hidden, only the INTENT flag changes: the hide seam owns the audio pause
// (hidden means idle no matter what the paused flag says -- the frame drain gates
// on paused-or-hidden), and `show` applies whatever pause intent the user last
// expressed. That's what lets a pause/resume commanded while hidden -- or batched
// in the same drain as `show` -- survive the restore instead of being clobbered
// by a hide-time stash.
void set_paused(CommandRuntimeState& state, Audio* audio, bool paused)
{
  if (paused == state.paused) {
    return;
  }
  state.paused = paused;
  if (audio && !state.hidden) {
    paused ? audio->PauseAll() : audio->ResumeAll();
  }
}

std::string execute_command(const command_protocol::ParsedCommand& cmd, Director& director,
                            Audio* audio, const ThemeBank& themes, AppUi* app_ui,
                            bool realtime, bool screenshot_supported,
                            CommandRuntimeState& state,
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
  case Verb::kHide:
  case Verb::kShow:
    // Silent-running primitive (spec sec 4): idempotent -- both verbs just write the
    // hidden intent; the apply seam in the main loop hides/shows the window, stashes/
    // restores mute, and no-ops when the state already matches. The seam is realtime-
    // only, so err in export mode rather than ack an intent nothing will ever apply
    // (and have `status` claim hidden=on forever) -- same capability gating as the
    // `ui`/`screenshot` verbs below.
    if (!realtime) {
      return command_protocol::format_err(
          std::string{cmd.verb == Verb::kHide ? "hide" : "show"} +
          ": unavailable in this mode (export)");
    }
    state.hidden = cmd.verb == Verb::kHide;
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
        << (state.overlay_on ? "on" : "off") << " hidden="
        << (state.hidden ? "on" : "off") << " uptime=" << uptime;
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
    if (cmd.verb == Verb::kUiOn) {
      show_control_panel(state, app_ui);
    } else {
      app_ui->set_visible(false);
    }
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
                     const ThemeBank& themes, AppUi* app_ui, bool realtime,
                     bool screenshot_supported, CommandRuntimeState& state,
                     const std::chrono::steady_clock::time_point& start_time)
{
  for (const auto& command : channel.drain()) {
    auto parsed = command_protocol::parse_command(command.line);
    auto reply = execute_command(parsed, director, audio, themes, app_ui, realtime,
                                 screenshot_supported, state, start_time);
    channel.reply(command.conn_id, reply);
  }
}

// `session` is non-const (and `session_path`/`sidecar` are threaded through) for the F2
// UI: its Program/Themes sections mutate the proto in place and its Session section saves
// it back to disk with the sidecar (pattern files / scan-dir themes round-trip). Director/
// ThemeBank keep their const refs -- in-place field mutation keeps map value addresses
// stable, and the UI never reorders/erases map entries. `system` is non-const (and
// `system_path` threaded through) for the same reason: the UI's System section edits
// renderer/windowed/eye-spacing in place and persists them back via save_system.
//
// `renderer_override` is --renderer (#41): null means "use system.renderer()". It is
// deliberately a separate parameter rather than a set_renderer() on `system` -- the F2
// System section persists `system` straight back to system.json, so writing the
// override in would make a one-run flag permanent.
void play_session(const std::string& root_path, trance_pb::Session& session,
                  const std::string& session_path, SessionJsonSidecar& sidecar,
                  trance_pb::System& system, const std::string& system_path,
                  const std::map<std::string, std::string> variables,
                  const exporter_settings& settings,
                  const std::function<void(Director&)>& visual_override = {},
                  const OverlayConfig& overlay = {}, uint16_t command_port = 0,
                  const trance_pb::System_Renderer* renderer_override = nullptr)
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

  PlaylistRunner playlist{session, variables};

  auto program = [&]() -> const trance_pb::Program& {
    static const auto default_session = get_default_session();
    static const auto default_program = default_session.program_map().find("default")->second;
    if (!playlist.current().has_standard()) {
      return default_program;
    }
    auto it = session.program_map().find(playlist.current().standard().program());
    if (it == session.program_map().end()) {
      return default_program;
    }
    return it->second;
  };

  auto theme_bank = std::make_unique<ThemeBank>(root_path, session, system, program());

  std::unique_ptr<Renderer> renderer;
  bool realtime = settings.path.empty();
  // --renderer wins over system.json for this run only; neither is written back.
  const auto requested_renderer = renderer_override ? *renderer_override : system.renderer();
  const char* requested_renderer_name = requested_renderer == trance_pb::System::OPENVR
      ? "openvr"
      : (requested_renderer == trance_pb::System::OPENXR ? "openxr" : "monitor");
  // Non-empty once a REQUESTED VR backend failed to initialize and we fell back to the
  // desktop window (#41). The fallback stays -- a session should still play -- but the
  // failure was previously visible only on stderr, which a Windows GUI launch never
  // shows: hence the banner below plus the persistent line in the F2 panel. That panel
  // only exists in non-VR mode, which is exactly this case.
  std::string vr_failure;
  if (!realtime) {
    renderer.reset(new VideoExportRenderer(settings));
  } else if (requested_renderer == trance_pb::System::OPENVR) {
    auto openvr = new OpenVrRenderer(system);
    renderer.reset(openvr);
    if (!openvr->success()) {
      renderer.reset();
      vr_failure = "SteamVR (OpenVR) initialization failed";
    }
  } else if (requested_renderer == trance_pb::System::OPENXR) {
    auto openxr = new OpenXrRenderer(system);
    renderer.reset(openxr);
    if (!openxr->success()) {
      renderer.reset();
      vr_failure = "OpenXR initialization failed";
    }
  }
  // Printed BEFORE the fallback window is constructed: ScreenRenderer can itself die
  // (no DISPLAY, no GL), and the reason VR was skipped must survive that.
  if (!vr_failure.empty()) {
    vr_failure += " (requested by " +
        std::string{renderer_override ? "--renderer=" : "system.json renderer="} +
        requested_renderer_name + "); playing on the desktop window instead";
    // Both streams: stdout is what a console launch scrolls past, stderr is where the
    // renderer's own diagnostics (the actual reason) already went.
    const std::string banner{"*** VR UNAVAILABLE: " + vr_failure + " ***"};
    std::cout << banner << std::endl;
    std::cerr << banner << std::endl;
    std::cerr << "see the OpenVR/OpenXR diagnostics above for the specific cause"
              << std::endl;
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

  // Overlay mode is click-through, so it can never receive its own in-window input
  // (see overlay_signal_handler above) -- the ways out all live outside the window:
  // SystemControl's tray icon (Quit / Show control panel), the global Shift+F11
  // hide-everything toggle, the command channel, or signals.
  if (overlay.enabled) {
    g_overlay_stop_requested = false;
    std::signal(SIGINT, overlay_signal_handler);
    std::signal(SIGTERM, overlay_signal_handler);
    std::cout << "overlay mode: quit with the tray icon (Windows) or Ctrl+C; Shift+F11 "
                 "hides everything (press again to restore)"
              << std::endl;
  }

  // ImGui in-app UI, F2-toggled. Stood up for every realtime screen window,
  // INCLUDING --overlay startup mode: while the overlay is engaged the click-through
  // window never delivers input (the panel is unreachable, and the apply seam below
  // collapses it on engage), but SystemControl's tray (Show control panel) or the
  // command channel can disengage the overlay at runtime, and the panel must exist
  // to serve as the control surface afterwards. Still not wired for VR (per-eye
  // render path, no single flat 2D pass to composite onto) or video-export mode (no
  // interactive window loop).
  std::unique_ptr<AppUi> app_ui;
  if (realtime && !director.vr_enabled()) {
    // Mutable mirror of the program() lambda above, for the UI's Program/Themes
    // sections: resolves the ACTIVE program in the session's program_map, or nullptr
    // when the built-in default fallback is playing (the panel disables itself).
    auto active_program = [&]() -> trance_pb::Program* {
      if (!playlist.current().has_standard()) {
        return nullptr;
      }
      auto it = session.mutable_program_map()->find(playlist.current().standard().program());
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
    app_ui.reset(new AppUi(session, session_path, sidecar, system, system_path,
                           command_state, on_program_change, active_program, vr_failure));
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
    save_window_screenshot(renderer->window(), command_state.screenshot_path);
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

  // System tray icon (Windows) + global Shift+F11 hide-everything toggle (Win32/X11):
  // the out-of-window control surface -- the only kind that keeps working while the
  // overlay is click-through. Realtime only; export mode has no window to hide or
  // control.
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
    audio->TriggerEvents(playlist.current());
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
  // One-shot "re-run apply_overlay_hints even though nothing changed", consumed by the
  // overlay seam. Seeded from --overlay because ScreenRenderer's constructor applies the
  // hints while the window is still UNMAPPED and Director's constructor then makes it
  // visible: on Win32 that map is exactly when DWM re-evaluates the window and can drop
  // the layered alpha (the window comes up opaque, and the F2 slider then no-ops because
  // the stored opacity already equals the intended one). Re-asserting once on the first
  // loop iteration closes that gap; the hide seam re-arms it for the same reason after a
  // restore (#39).
  bool overlay_reassert_pending = overlay.enabled;
  // Hide-everything: what's actually applied to the window right now, plus the mute
  // state stashed at hide time so a `show` restores what the user had (not defaults).
  // Same diff-against-intent pattern as the overlay pair above. Pause needs no stash:
  // hiding leaves command_state.paused untouched as the user's pause INTENT (the frame
  // drain below idles on paused-OR-hidden), so pause/resume commanded while hidden --
  // or batched in the same drain as `show` -- survives the restore.
  bool hidden_applied = false;
  bool pre_hide_muted = false;
  // Set by the seam's show branch: the restored window's back buffer is stale
  // (nothing rendered while hidden), so force one composited frame this iteration
  // even if playback stays paused -- un-hide must never present a black/stale frame.
  bool show_fresh_frame = false;

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
    playlist.start(clock_time());
    // Side effects per newly-entered playlist item: audio events, the program push
    // into ThemeBank/Director, entrainment refresh. The runner owns only the
    // stack/transition logic.
    auto on_playlist_enter = [&](const std::string& name, const trance_pb::PlaylistItem& item) {
      if (realtime) {
        audio->TriggerEvents(item);
      }
      std::cout << "\n-> " << name << std::endl;
      theme_bank->set_program(program());
      director.set_program(program());
      if (realtime) {
        audio->SetEntrainment(program().entrainment());
      }
    };

    while (running && !g_overlay_stop_requested) {
      // (The overlay stop path exits via g_overlay_stop_requested with `running`
      // still true; it is flipped after the loop so the async ThemeBank thread's
      // `while (running)` loop terminates and the join at the bottom of
      // play_session is bounded.)
      handle_events(running, renderer->window(), director, audio.get(), app_ui.get(),
                    command_state);
      // Command channel: parse + dispatch every line received since last frame, right
      // after handle_events (spec sec 3: "Parse + dispatch happens in the drain loop --
      // main.cpp's per-frame loop, right after handle_events").
      if (command_channel) {
        handle_commands(*command_channel, director, audio.get(), *theme_bank, app_ui.get(),
                        realtime, screenshot_supported, command_state, command_start_time);
      }
      // SystemControl requests (tray menu / global Shift+F11): drained on the render
      // thread like the command mailbox above; both funnel into the same
      // CommandRuntimeState, and the apply seam below reconciles the window once.
      if (system_control) {
        for (auto request : system_control->drain()) {
          switch (request) {
          case ControlRequest::kSafety:
            // Global hide-everything toggle (Shift+F11): only flips the intent; the
            // hidden apply seam below does the actual hide/restore work. Never
            // quits -- that's kQuit's job -- EXCEPT on hotkey-only configurations
            // (no tray Quit item and no F2 panel: Linux VR, or Linux fullscreen
            // after a failed ImGui init), where nothing else can ever push kQuit;
            // there a press while already hidden quits instead of restoring,
            // preserving the hotkey's old second-press-quits escape hatch as the
            // one orderly exit.
            if (command_state.hidden && !SystemControl::kHasTrayQuit && !app_ui) {
              running = false;
              break;
            }
            command_state.hidden = !command_state.hidden;
            break;
          case ControlRequest::kHide:
          case ControlRequest::kShow:
            // Tray Hide/Show item: explicit target state (see ControlRequest::kHide)
            // so a menu whose label went stale while it sat open can never invert
            // the user's intent; a request matching the current state is a no-op at
            // the seam, like the idempotent `hide`/`show` verbs.
            command_state.hidden = request == ControlRequest::kHide;
            break;
          case ControlRequest::kOverlayToggle:
            command_state.overlay_on = !command_state.overlay_on;
            // Requests are applied in arrival order, so the field already holds the
            // batch's NET intent -- but a batch that nets to zero (a fast on/off/on
            // burst landing in one frame) would leave the seam with nothing to diff
            // while the user watched two transitions go by. Arm the re-assert so the
            // window is resynchronized to that net intent exactly once, whatever the
            // batch did on the way there (#39).
            overlay_reassert_pending = true;
            break;
          case ControlRequest::kOverlayOpacityUp:
          case ControlRequest::kOverlayOpacityDown:
            // Tray opacity nudge: 0.1 steps through the same clamp the
            // `overlay opacity` verb uses; the overlay apply seam pushes it live.
            command_state.overlay_opacity = command_protocol::clamp01(
                command_state.overlay_opacity +
                (request == ControlRequest::kOverlayOpacityUp ? 0.1f : -0.1f));
            break;
          case ControlRequest::kPlayPauseToggle:
            set_paused(command_state, audio.get(), !command_state.paused);
            break;
          case ControlRequest::kShowUi:
            show_control_panel(command_state, app_ui.get());
            break;
          case ControlRequest::kQuit:
            running = false;
            break;
          }
        }
      }
      // Hide-everything apply seam: the single reconcile point for the `hide`/`show`
      // verbs, Shift+F11, and the tray's Hide-Show item (each only writes
      // command_state.hidden). Hiding forces the overlay off, and its click-through
      // hints are cleared HERE, while the window is still mapped -- no stuck
      // click-through state can survive a hide/restore cycle.
      if (realtime && command_state.hidden != hidden_applied) {
        if (command_state.hidden) {
          // Stash mute so `show` restores what the user had, not defaults. (Pause
          // deliberately not stashed -- see the pre_hide_muted declaration above.)
          pre_hide_muted = audio && audio->Muted();
          command_state.overlay_on = false;
          // Clear the click-through hints BEFORE setVisible(false): on X11 the
          // _NET_WM_STATE remove ClientMessage is only honoured for MAPPED windows
          // (overlay_hints.cpp), so leaving this to the overlay seam below -- which
          // runs after the window is unmapped -- would rely on the WM happening to
          // drop the state on withdrawal.
          // activate=false: the window is about to be hidden, so taking the
          // foreground here would briefly steal it from whatever the user is doing.
          if (overlay_applied) {
            clear_overlay_hints(renderer->window().getNativeHandle(), false);
            overlay_applied = false;
          }
          // The seam owns the audio pause while hidden (not set_paused, which only
          // records intent while hidden): hidden means idle regardless of the
          // paused flag.
          if (audio) {
            audio->PauseAll();
            if (!audio->Muted()) {
              audio->ToggleMute();
            }
          }
          // Same rationale as the overlay-engage collapse below: never leave the
          // panel logically open on a window that can't deliver it input.
          if (app_ui) {
            app_ui->set_visible(false);
          }
          // VR: the renderer's sf::Window is a hidden GL-context helper, not the
          // visible surface -- don't touch it (a setVisible(true) on restore would
          // pop up a blank window that was never meant to be seen). Content leaves
          // the headset via the layerless render_idle() frames below.
          if (!director.vr_enabled()) {
            renderer->window().setVisible(false);
          }
        } else {
          if (!director.vr_enabled()) {
            renderer->window().setVisible(true);
          }
          if (audio) {
            if (audio->Muted() != pre_hide_muted) {
              audio->ToggleMute();
            }
            // Resume iff the CURRENT pause intent says play: that's the pre-hide
            // state unless the user explicitly paused/resumed while hidden (or in
            // this very drain batch alongside `show`), in which case their last
            // explicit command wins.
            if (!command_state.paused) {
              audio->ResumeAll();
            }
          }
          show_fresh_frame = true;
          // The window was just re-mapped: same DWM re-evaluation risk as the startup
          // path, so make the overlay seam re-assert its hints rather than trust
          // overlay_applied. Only when the overlay is actually engaged -- re-clearing
          // an already-clear window would send the WM a pointless state-remove.
          overlay_reassert_pending = command_state.overlay_on;
          // A window that was invisible cannot have had focus; restoring it must hand
          // input back, or the panel comes up unclickable.
          command_state.focus_requested = true;
        }
        hidden_applied = command_state.hidden;
      }
      // Live overlay apply seam: the single reconcile point for the
      // `overlay on|off|opacity` verbs, the F2 UI's Overlay section, and the
      // SystemControl requests above (each only writes command_state). Realtime
      // only -- export mode has no real window to overlay.
      //
      // The trigger is a transition, an opacity change, or an explicit re-assert
      // request -- NOT overlay_applied alone. overlay_applied is a belief about the
      // window, never read back from it, so any hint write the OS ignored (all of them
      // are cerr-logged, not checked) desyncs it permanently and a "no diff" seam then
      // silently declines to fix the window (#39). Re-assert covers the two known
      // desync sources: hints applied to a not-yet-mapped window at startup, and a
      // window that has just come back from hidden. Steady state still costs zero
      // native calls -- the flag is one-shot.
      if (realtime &&
          (command_state.overlay_on != overlay_applied || overlay_reassert_pending ||
           (command_state.overlay_on &&
            command_state.overlay_opacity != overlay_opacity_applied))) {
        if (command_state.overlay_on) {
          // Unconditional on every engage/re-assert: apply_overlay_hints rewrites the
          // ex-styles AND SetLayeredWindowAttributes, so a stuck-opaque or
          // stuck-click-through window gets corrected rather than reasoned about.
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
          // An engaged overlay can't be focused (and must not be): drop any pending
          // focus request rather than leave it to fire against a click-through window.
          command_state.focus_requested = false;
        } else {
          // activate=false: the deferred focus seam below is the single activation site
          // for this path -- it does the same Win32 activation AND the requestFocus()
          // half, so activating here too would activate the window twice per toggle.
          clear_overlay_hints(renderer->window().getNativeHandle(), false);
          // Disengaging hands the window back to the user: it must also be focusable
          // again. The Win32 clear restores styles with SWP_NOACTIVATE, so without
          // this the window stays unactivated and imgui-SFML swallows every click.
          command_state.focus_requested = true;
        }
        overlay_applied = command_state.overlay_on;
        overlay_opacity_applied = command_state.overlay_opacity;
        overlay_reassert_pending = false;
      }
      // Focus grab: deferred to here so it lands AFTER both apply seams -- the window
      // must be visible and out of click-through before activation means anything.
      // One-shot; requestFocus() is the cross-platform half and focus_window() adds the
      // Win32 activation requestFocus() won't do while another process (the tray helper
      // window) holds the foreground.
      //
      // The hidden/overlay_on guards are TRANSIENT -- the request legitimately stays
      // pending across them and fires once the window becomes interactive. Everything
      // else here is INAPPLICABLE for the whole run (VR has no visible sf::Window to
      // focus at all; export has no window), so the request can never be satisfied and
      // must be dropped rather than left pending forever (#39). Same one-shot semantics
      // either way: the flag never survives a frame in which it was actionable-or-moot.
      if (command_state.focus_requested && !command_state.hidden &&
          !command_state.overlay_on) {
        if (realtime && !director.vr_enabled()) {
          renderer->window().requestFocus();
          focus_window(renderer->window().getNativeHandle());
        }
        command_state.focus_requested = false;
      }
      // Mirror live state back to the tray AFTER the apply seams, so the menu's
      // checkmarks/label never reflect a half-applied frame (the hide seam mutates
      // overlay_on above; mirroring before it would briefly report pre-seam values).
      if (system_control) {
        system_control->set_status(command_state.overlay_on, command_state.paused,
                                   command_state.hidden);
      }
      // The F2 panel's "Quit trance" button (set during last frame's app_ui->update):
      // same clean exit as the window close button and the tray's Quit item.
      if (app_ui && app_ui->quit_requested()) {
        running = false;
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

      // Paused OR hidden: freeze the playlist clock too. time_since_switch is
      // wall-clock, so without this a paused session's items keep timing out and
      // firing audio events / program switches underneath the frozen visuals.
      // Hidden counts as paused for every playback purpose (silent running: nothing
      // may advance under the invisible window) WITHOUT touching the user's explicit
      // paused intent, which the hide seam's show branch consults on restore.
      const bool playback_paused = command_state.paused || command_state.hidden;
      if (playback_paused) {
        playlist.freeze(elapsed_ms);
      }

      playlist.advance(clock_time(), on_playlist_enter);
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
      while (!playback_paused && frames_this_loop > 0) {
        update = true;
        --frames_this_loop;
        continue_playing &= director.update();
        theme_bank->advance_frames();
      }
      // The renderer's event pump must keep running even when no visual frame is
      // due (paused, or between frames): OpenXR's READY/STOPPING handshake is
      // spec-mandatory, so stalling it while paused leaves the runtime waiting
      // forever on doff / Link close. director.update() already pumps it once
      // per frame drained above, so only pump here when it didn't run.
      if (!update) {
        continue_playing &= renderer->update();
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
      // While hidden the live-UI term is dropped: silent running must be genuinely
      // idle (no per-frame scene render + swap on the invisible window -- an
      // unmapped window's swap typically isn't vsync-throttled, so it would peg a
      // core/GPU precisely while pretending to be gone). The panel was collapsed by
      // the hide seam anyway; the idle branch below takes over. show_fresh_frame
      // guarantees the first post-restore iteration repaints even when playback
      // stays paused, so the un-hidden window is never stale/black.
      const bool do_render = update || !realtime ||
          (app_ui != nullptr && !command_state.hidden) || show_fresh_frame ||
          !command_state.screenshot_path.empty();
      if (app_ui && do_render) {
        app_ui->update(renderer->window(), ui_clock.restart(), director, audio.get(),
                       *theme_bank);
      }
      if (do_render) {
        director.render();
        show_fresh_frame = false;
      } else if (playback_paused) {
        // Paused or hidden with nothing rendered: nothing above blocks, so don't
        // spin a core. OpenXR first gets render_idle() -- a running XR session must
        // keep submitting (layerless) frames or the runtime flags the app
        // unresponsive, and those blank frames are also what actually removes the
        // content from the headset; it paces via xrWaitFrame, replacing the sleep.
        // Everywhere else (screen window, OpenVR, no-UI fallback) render_idle is a
        // no-op returning false and the 10ms sleep engages. Only while paused/
        // hidden -- unpaused between-frames timing is the longstanding TODO above
        // and 10ms granularity would jitter it.
        if (!renderer->render_idle()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
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
  // No explicit window().close() here: ~OpenXrRenderer must run while the hidden
  // window's GL context is still alive -- the runtime holds the hDC/hGLRC handed
  // over in the graphics binding, and the FBO / xrDestroySwapchain / session
  // teardown needs that context current. The renderer's destructor (at scope
  // exit just below, before ~ThemeBank) closes its own window.
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
           "its own input; quit with the tray icon (Windows) or Ctrl+C, or hide "
           "everything with Shift+F11 (global hide/show toggle).");
DEFINE_double(overlay_opacity, 0.35,
             "overlay window opacity, 0 (fully transparent) to 1 (fully opaque). Only "
             "meaningful with --overlay.");
DEFINE_string(renderer, "",
             "renderer for this run: monitor, openvr (SteamVR) or openxr (head-locked "
             "quad). Overrides system.json's \"renderer\" key for this run ONLY -- it is "
             "never written back, so the F2 System radios stay the persistent setting. "
             "Empty (default) means use system.json, where a missing key is monitor "
             "mode (which SteamVR then mirrors as a flat virtual desktop).");
DEFINE_int32(command_port, 0,
            "command channel (docs/spec-mcp-ambient-daemon.md): TCP port to bind on "
            "127.0.0.1 for the localhost line-protocol control socket (start/stop/pause/"
            "resume, overlay on|off|opacity, intensity, hide/show, set/get, load "
            "pattern|session, status). 0 (default) disables the channel entirely -- no "
            "socket is opened. "
            "Loopback-only, no auth: binding to 127.0.0.1 is the whole trust boundary "
            "(spec sec 2/9), so only enable this on a machine you trust everyone on.");

namespace
{
  // Fatal, startup-time error for a bad --visual name: lists every valid name so the
  // failure is immediately actionable instead of a runtime surprise later.
  [[noreturn]] void fatal_bad_visual_name(const std::string& name)
  {
    std::cerr << "error: --visual '" << name << "' is not a known built-in visual. Valid names: ";
    bool first = true;
    for (const auto& visual : builtin_visuals()) {
      if (!first) {
        std::cerr << ", ";
      }
      std::cerr << visual.name;
      first = false;
    }
    std::cerr << std::endl;
    std::exit(1);
  }

  // --renderer, same spelling set session_json.cpp's parse_renderer accepts for the
  // "renderer" key (that one is file-local to the JSON layer, so the names are mirrored
  // here rather than the parser exported). Fatal on a bad value, like --visual above:
  // a typo'd --renderer must not silently play on the monitor.
  bool parse_renderer_flag(const std::string& s, trance_pb::System_Renderer& out)
  {
    if (s == "monitor") {
      out = trance_pb::System_Renderer_MONITOR;
    } else if (s == "openvr") {
      out = trance_pb::System_Renderer_OPENVR;
    } else if (s == "openxr") {
      out = trance_pb::System_Renderer_OPENXR;
    } else {
      return false;
    }
    return true;
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
  // --renderer: eagerly validated for the same reason, and resolved into an optional
  // override play_session applies on top of system.json (never writing it back).
  trance_pb::System_Renderer renderer_override_value = trance_pb::System_Renderer_MONITOR;
  const trance_pb::System_Renderer* renderer_override = nullptr;
  if (!FLAGS_renderer.empty()) {
    if (!parse_renderer_flag(FLAGS_renderer, renderer_override_value)) {
      std::cerr << "error: --renderer '" << FLAGS_renderer
                << "' is not a known renderer. Valid names: monitor, openvr, openxr" << std::endl;
      return 1;
    }
    renderer_override = &renderer_override_value;
    std::cout << "-> renderer overridden to '" << FLAGS_renderer
              << "' for this run (system.json unchanged)" << std::endl;
  }
  OverlayConfig overlay;
  overlay.enabled = FLAGS_overlay;
  overlay.opacity = static_cast<float>(FLAGS_overlay_opacity);
  // --visual: resolved (and fatal-errors on an unknown name) here at startup, not at
  // first selection -- a typo should never surface as a runtime surprise.
  uint32_t forced_visual_type = 0;
  if (!FLAGS_visual.empty()) {
    for (const auto& visual : builtin_visuals()) {
      if (visual.name == FLAGS_visual) {
        forced_visual_type = visual.type;
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
    sidecar = SessionJsonSidecar{};
    if (std::filesystem::exists(LEGACY_DEFAULT_SESSION_PATH)) {
      std::cout << "migrating legacy ./" << LEGACY_DEFAULT_SESSION_PATH << " -> ./"
                << DEFAULT_SESSION_PATH << std::endl;
      session = load_legacy_session(LEGACY_DEFAULT_SESSION_PATH);
      validate_session(session);
    } else {
      std::cerr << e.what() << std::endl;
      session = get_default_session();
      // #36: keep the folder-ness. search_resources reports which themes are pure
      // subdirectory references; seeding the sidecar with them makes the saver write
      // {"scan": <subdir>} per theme instead of freezing a media list that goes stale
      // the moment the user drops another image in.
      search_resources(session, ".", sidecar.theme_scan);
    }
    try {
      save_session(session, "./" + DEFAULT_SESSION_PATH, sidecar);
      std::cout << "wrote ./" << DEFAULT_SESSION_PATH << std::endl;
      // Play what was WRITTEN, not the in-memory legacy proto: the JSON saver
      // normalizes Windows backslash media paths to forward slashes (spec sec 1),
      // and the legacy-authored originals don't resolve on non-Windows. Reset the
      // sidecar first -- the reload rebuilds it from the file just written.
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
  play_session(root_path, session, session_path, sidecar, system, system_path, variables,
              settings, visual_override, overlay, uint16_t(FLAGS_command_port),
              renderer_override);
  return 0;
}
