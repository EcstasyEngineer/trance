#ifndef TRANCE_SRC_TRANCE_UI_APP_UI_H
#define TRANCE_SRC_TRANCE_UI_APP_UI_H
// ImGui in-app UI: the creator-replacement (v0 creator-parity buildout). Toggled with
// F2 (see main.cpp's handle_events); coexists with the pre-existing F1 text debug
// overlay (Director::toggle_debug_overlay / draw_debug_overlay), which is untouched.
// Unavailable in --overlay click-through mode -- see AppUi::available().
//
// One window ("trance", top-left) with collapsing sections:
//   - Status: fps / themes / bed summary, reused from the same accessors
//     draw_debug_overlay() uses (ThemeBank::debug_snapshot, Program::entrainment).
//   - Visuals: the 8 built-ins + the program's custom patterns; clicking forces one
//     immediately via Director::force_builtin_visual / force_pattern_from_source
//     (the same plumbing --visual/--pattern use).
//   - Program: live edit of the ACTIVE program (global fps, per-visual-type weights,
//     text/spiral colours). Mutates the in-memory session proto in place, then fires
//     on_program_change so ThemeBank/Director pick it up -- replacing the old
//     "zero every other weight in a text editor" workflow.
//   - Themes: per-theme enable/weight rows (the program's enabled_theme entries;
//     disable = weight 0, entries are kept -- matching ThemeBank::set_program's
//     semantics) + per-theme image multiselect editing Theme::image_path. Content
//     edits need a restart: ThemeBank is built once at startup, no live rebuild.
//   - Session: loaded path, Save (back to that path) and Save As, via
//     save_session(session, path, sidecar) so pattern files / scan-dir themes
//     round-trip instead of being frozen inline.
//   - Entrainment: mute toggle (Audio::ToggleMute). No volume slider: Audio exposes
//     only a global mute (sf::Listener::setGlobalVolume 0/100 in ToggleMute), not a
//     settable gain -- see the handoff note in draw_entrainment_section.
//
// Settings persistence: NONE this wave. TODO(settings-json chain): once JSON settings
// land, persist last-forced-visual / mute state / UI-open across runs.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
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
}
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
  // `session`/`sidecar` are play_session's live objects (outliving this AppUi); the
  // Program/Themes sections mutate `session` in place and the Session section saves
  // it back to disk. `on_program_change` must re-push the active program into
  // ThemeBank/Director (the same pair the playlist-switch path calls) so live edits
  // apply. `active_program` resolves the mutable active program in session's
  // program_map, or nullptr when the built-in default fallback is playing (the
  // Program section disables itself in that case).
  AppUi(trance_pb::Session& session, const std::string& session_path,
        SessionJsonSidecar& sidecar, std::function<void()> on_program_change,
        std::function<trance_pb::Program*()> active_program);
  ~AppUi();

  AppUi(const AppUi&) = delete;
  AppUi& operator=(const AppUi&) = delete;

  // Overlay mode's window is click-through by design (render.cpp's
  // apply_x11_overlay_hints installs an empty input shape) -- it can never receive
  // mouse/keyboard events, so an interactive ImGui UI is structurally unusable there
  // (and F2 is still wired but is a silent no-op; see main.cpp's handle_events).
  static bool available(bool overlay_enabled);

  // Initializes ImGui + the SFML backend against `window`. Must be called once,
  // after the window is created, before the first process_event/update/render call.
  // Returns false (and leaves the UI permanently disabled) if ImGui::SFML::Init fails.
  bool init(sf::RenderWindow& window);

  bool visible() const { return _visible; }
  void toggle() { _visible = !_visible; }
  // Remote-controlled visibility (#21 `ui on|off`) -- same state F2 toggles.
  void set_visible(bool visible) { _visible = visible; }

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
  void draw_entrainment_section(Audio* audio);
  // Save (with sidecar) to `path`, recording a transient status line either way.
  void save_session_to(const std::string& path);

  trance_pb::Session& _session;
  const std::string _session_path;
  SessionJsonSidecar& _sidecar;
  std::function<void()> _on_program_change;
  std::function<trance_pb::Program*()> _active_program;

  bool _visible = false;
  bool _initialized = false;
  bool _init_failed = false;
  // An ImGui frame is open (update() ran, render() hasn't) -- pairs Update/Render.
  bool _frame_started = false;
  // Last force_pattern_from_source() parse error, shown inline in the Visuals section
  // until the next click. Empty when nothing failed.
  std::string _last_pattern_error;

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
};

#endif
