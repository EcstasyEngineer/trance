#ifndef TRANCE_SRC_TRANCE_UI_APP_UI_H
#define TRANCE_SRC_TRANCE_UI_APP_UI_H
// ImGui in-app UI: the creator replacement. Toggled with
// F2 (see main.cpp's handle_events -- Escape quits outright; the other quit paths are
// the panel's Quit button, the tray's Quit item, and closing the window); coexists
// with the pre-existing F1 text debug overlay (Director::toggle_debug_overlay /
// draw_debug_overlay), which is untouched.
// Exists in --overlay runs too (the overlay is runtime-toggleable now): while the
// overlay is engaged the click-through window delivers no input and the panel is
// collapsed by main.cpp's apply seam; SystemControl (Shift+F11 / tray) disengages
// the overlay and brings the panel back.
//
// One window ("trance", top-left) with collapsing sections:
//   - Status: fps / themes / bed summary, reused from the same accessors
//     draw_debug_overlay() uses (ThemeBank::debug_snapshot, Program::entrainment).
//   - Visuals: the 8 built-ins (force-now button + a read-only expander showing the
//     v3 grammar source, with Copy -- the modding-language reference) and the active
//     program's custom patterns (editable name + source, live patternv3::parse lint,
//     Apply/Force now/Remove, "+ New pattern"). Forcing goes through
//     Director::force_builtin_visual / force_pattern_from_source (the same plumbing
//     --visual/--pattern use); Apply fires on_program_change so Director re-parses.
//     Custom-pattern edits land in the proto's name/source_text, which Save writes out
//     as patterns/<slug>.pattern sidecars -- no extra persistence plumbing.
//   - Program: live edit of the ACTIVE program (global fps, per-visual-type weights,
//     text/spiral colours). Mutates the in-memory session proto in place, then fires
//     on_program_change so ThemeBank/Director pick it up.
//   - Themes: per-theme enable/weight rows (the program's enabled_theme entries;
//     disable = weight 0, entries are kept -- matching ThemeBank::set_program's
//     semantics) + per-theme image multiselect editing Theme::image_path. Content
//     edits need a restart: ThemeBank is built once at startup, no live rebuild.
//   - Session: loaded path, Save (back to that path) and Save As, via
//     save_session(session, path, sidecar) so pattern files / scan-dir themes
//     round-trip instead of being frozen inline.
//   - Overlay: live click-through overlay toggle + opacity slider. Reads/writes
//     main.cpp's CommandRuntimeState via the get_overlay/set_overlay callbacks; the
//     main loop's apply seam (shared with the `overlay ...` verbs) pushes changes
//     onto the actual window.
//   - Entrainment: mute toggle (Audio::ToggleMute). No volume slider: Audio exposes
//     only a global mute (sf::Listener::setGlobalVolume 0/100 in ToggleMute), not a
//     settable gain -- see the note in draw_entrainment_section.
//   - System: renderer selection (Monitor / SteamVR / OpenXR), windowed mode, eye
//     spacing -- edits trance_pb::System in place and persists immediately to
//     system.json via save_system. Renderer/windowed take effect on next launch
//     (the renderer/window are constructed once at startup).
// Plus a separated "Quit trance" button at the bottom (polled by main.cpp via
// quit_requested()).
//
// TODO: persist last-forced-visual / mute state / UI-open once JSON settings land.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sf
{
  class RenderWindow;
  class Event;
  class Time;
}
namespace trance_pb
{
  class Program;
  class Session;
  class System;
}
struct CommandRuntimeState;
struct SessionJsonSidecar;
class Audio;
class Director;
class ThemeBank;

class AppUi
{
public:
  // Does NOT call ImGui::SFML::Init -- that only happens if the caller decides the
  // UI is available (see available() below) so overlay mode never pays ImGui's
  // per-frame Update/Render cost or touches the click-through window.
  //
  // `session`/`sidecar`/`command_state` are play_session's live objects (outliving
  // this AppUi); the Program/Themes sections mutate `session` in place, the Session
  // section saves it back to disk, and the Overlay section reads/writes
  // `command_state`'s overlay fields (main.cpp's per-frame apply seam pushes any
  // change onto the actual window). `on_program_change` must re-push the active
  // program into ThemeBank/Director (the same pair the playlist-switch path calls)
  // so live edits apply. `active_program` resolves the mutable active program in
  // session's program_map, or nullptr when the built-in default fallback is playing
  // (the Program section disables itself in that case). `system`/`system_path` are
  // main()'s live System config + where it was loaded from: the System section edits
  // the proto in place and persists straight back to `system_path` via save_system.
  AppUi(trance_pb::Session& session, const std::string& session_path,
        SessionJsonSidecar& sidecar, trance_pb::System& system,
        const std::string& system_path, CommandRuntimeState& command_state,
        std::function<void()> on_program_change,
        std::function<trance_pb::Program*()> active_program);
  ~AppUi();

  AppUi(const AppUi&) = delete;
  AppUi& operator=(const AppUi&) = delete;

  // Initializes ImGui + the SFML backend against `window`. Must be called once,
  // after the window is created, before the first process_event/update/render call.
  // Returns false (and leaves the UI permanently disabled) if ImGui::SFML::Init fails.
  bool init(sf::RenderWindow& window);

  bool visible() const { return _visible; }
  void toggle() { _visible = !_visible; }
  // Remote-controlled visibility (the `ui on|off` verbs) -- same state F2 toggles.
  void set_visible(bool visible) { _visible = visible; }

  // True while an ImGui text field is active (io.WantTextInput). handle_events()
  // checks this before the Escape/F2 panel toggle: Escape's standard ImGui meaning
  // inside an active InputText (e.g. the Session section's Save As field) is
  // "cancel the edit", and it must not also close the whole panel.
  bool wants_text_input() const;

  // Set (sticky) by the panel's "Quit trance" button; polled once per frame by
  // main.cpp's loop, which flips `running` -- the same clean exit path as the tray
  // Quit / window close.
  bool quit_requested() const { return _quit_requested; }

  // Forwarded from handle_events() so ImGui can see keyboard/mouse input while open.
  void process_event(sf::RenderWindow& window, const sf::Event& event);

  // Builds this frame's UI (ImGui calls only -- no GL draw calls happen here; those
  // are issued by render()). No-op if !visible().
  void update(sf::RenderWindow& window, sf::Time dt, Director& director, Audio* audio,
             const ThemeBank& themes);

  // Issues ImGui's GL draw calls into `window`'s currently-bound buffer. Runs via
  // Renderer::set_ui_hook, after the frame's scene draw and before its display().
  // No-op unless a matching update() started an ImGui frame this iteration (ImGui
  // asserts on Render without a prior NewFrame).
  void render(sf::RenderWindow& window);

private:
  void draw_status_section(Director& director, Audio* audio, const ThemeBank& themes);
  void draw_visuals_section(Director& director);
  void draw_program_section();
  void draw_themes_section();
  void draw_session_section();
  void draw_overlay_section();
  void draw_entrainment_section(Audio* audio);
  void draw_system_section();
  // First "custom_N" not already used by a custom_visual_pattern in `program`.
  // Duplicate names are a load-time error (session_json.cpp), so "+ New pattern"
  // has to seed a name that cannot collide.
  static std::string unique_pattern_name(const trance_pb::Program& program);
  // Save (with sidecar) to `path`, recording a transient status line either way.
  void save_session_to(const std::string& path);
  // Persist the System proto back to _system_path, recording a transient status line.
  void save_system_config();

  trance_pb::Session& _session;
  const std::string _session_path;
  SessionJsonSidecar& _sidecar;
  trance_pb::System& _system;
  const std::string _system_path;
  CommandRuntimeState& _command_state;
  std::function<void()> _on_program_change;
  std::function<trance_pb::Program*()> _active_program;

  bool _visible = false;
  bool _initialized = false;
  bool _init_failed = false;
  bool _quit_requested = false;
  // An ImGui frame is open (update() ran, render() hasn't) -- pairs Update/Render.
  bool _frame_started = false;
  // Last force_pattern_from_source() parse error, shown inline in the Visuals section
  // until the next click. Empty when nothing failed.
  std::string _last_pattern_error;
  // Per-custom-pattern-row lint, keyed by row index: "" = OK, else the patternv3
  // "line:col: message" (or a duplicate/empty-name complaint). Cached so parse only
  // runs when a row's name/source actually changed, not every frame; invalidated by
  // erasing the row's entry, and cleared wholesale when row indices shift.
  std::map<int, std::string> _pattern_lint;
  // The program _pattern_lint's row indices refer to; a change means the active
  // program switched under us and every cached verdict describes the wrong pattern.
  // Compared only for identity -- never dereferenced.
  const trance_pb::Program* _pattern_lint_program = nullptr;

  // Every image path ever seen per theme this run, in first-seen order, so an
  // unchecked image (removed from Theme::image_path) can be re-checked within the
  // session. Not persisted -- unchecked paths saved out are gone from the file.
  std::map<std::string, std::vector<std::string>> _theme_seen_images;
  // Last nonzero weight per theme, so the enable checkbox round-trips a theme's
  // weight instead of resetting it to 1 (disable keeps the enabled_theme entry at
  // weight 0, matching ThemeBank::set_program's semantics).
  std::map<std::string, uint32_t> _theme_last_weight;

  // Session section state: Save As target (seeded with the loaded path) + a
  // transient "saved"/error status line with a countdown.
  char _save_as_buf[512] = {};
  std::string _save_status;
  bool _save_error = false;
  float _save_status_ttl = 0.f;

  // System section state: transient "saved system.json"/error status line.
  std::string _system_status;
  bool _system_error = false;
  float _system_status_ttl = 0.f;
};

#endif
