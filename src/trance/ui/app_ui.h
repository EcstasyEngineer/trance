#ifndef TRANCE_SRC_TRANCE_UI_APP_UI_H
#define TRANCE_SRC_TRANCE_UI_APP_UI_H
// ImGui in-app UI skeleton (task 18): the first brick of the creator-replacement.
// Toggled with F2 (see main.cpp's handle_events); coexists with the pre-existing F1
// text debug overlay (Director::toggle_debug_overlay / draw_debug_overlay), which is
// untouched. Unavailable in --overlay click-through mode -- see AppUi::available().
//
// v0 scope (skeleton, not pretty):
//   - Entrainment panel: mute toggle (Audio::ToggleMute) + a hover-reveal always-on
//     corner icon (issue #24 item 1). No volume slider: Audio exposes only a global
//     mute (sf::Listener::setGlobalVolume 0/100 in ToggleMute), not a settable gain --
//     see the handoff note in app_ui.cpp's draw_entrainment_panel.
//   - Visuals panel: lists the 8 built-ins + the program's custom patterns; clicking
//     forces it immediately via Director::force_builtin_visual /
//     force_pattern_from_source (the same plumbing --visual/--pattern use).
//   - Status panel: visual name, theme names, bed summary, reused from the same
//     accessors draw_debug_overlay() uses (ThemeBank::debug_snapshot,
//     Program::entrainment) -- no new Director surface added.
//
// Settings persistence: NONE this wave. TODO(settings-json chain): once JSON settings
// land, persist last-forced-visual / mute state / UI-open across runs.
#include <string>

namespace sf
{
  class RenderWindow;
  class Event;
  class Time;
}
namespace trance_pb
{
  class Program;
}
class Audio;
class Director;
class ThemeBank;

class AppUi
{
public:
  // Does NOT call ImGui::SFML::Init -- that only happens if the caller decides the
  // UI is available (see available() below) so overlay mode never pays ImGui's
  // per-frame Update/Render cost or touches the click-through window.
  AppUi() = default;
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

  // Forwarded from handle_events() so ImGui can see keyboard/mouse input while open.
  void process_event(sf::RenderWindow& window, const sf::Event& event);

  // Builds this frame's UI (ImGui calls only -- no GL draw calls happen here; those
  // are issued by render()). No-op if !visible().
  void update(sf::RenderWindow& window, sf::Time dt, Director& director, Audio* audio,
             const ThemeBank& themes);

  // Issues ImGui's GL draw calls into `window`'s currently-bound buffer. Must be
  // called after the frame's scene has been drawn and BEFORE the frame's
  // window.display() -- see the handoff note in main.cpp's play_session loop for why
  // this wave's call site can't reach that point (Director/Renderer own the
  // clear/draw/display sequence; ui/ can't inject a hook into it without touching
  // director.cpp/render.cpp, which are outside this task's owned files). Until that
  // seam exists, the UI necessarily composites one frame late (see main.cpp).
  void render(sf::RenderWindow& window);

private:
  void draw_entrainment_panel(Audio* audio);
  void draw_visuals_panel(Director& director);
  void draw_status_panel(Director& director, Audio* audio, const ThemeBank& themes);

  bool _visible = false;
  bool _initialized = false;
  bool _init_failed = false;
  // Last force_pattern_from_source() parse error, shown inline in the Visuals panel
  // until the next click. Empty when nothing failed.
  std::string _last_pattern_error;
};

#endif
