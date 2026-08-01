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
//   - Visuals: the WHOLE visual lottery in one list, built-ins and custom patterns
//     alike, since Director::change_visual draws from both at once. Every row is the
//     same shape as a Themes row: a weight row (on/off, pin, weight, effective share)
//     followed by the name as an expander. A built-in's expander holds Force now, Copy
//     and its read-only v3 grammar source (the modding-language reference); a custom
//     pattern's holds its editable name + word-wrapped source, live patternv3::parse
//     lint, Apply/Force now/Remove. A built-in with no visual_type row at all still
//     draws (at weight 0) and materializes its entry on first touch, so a session that
//     omits one can still force it and add it back. Forcing goes through
//     Director::force_builtin_visual / force_pattern_from_source (the same plumbing
//     --visual/--pattern use); Apply fires on_program_change so Director re-parses.
//     Custom-pattern edits land in the proto's name/source_text, which Save writes out
//     as patterns/<slug>.pattern sidecars -- no extra persistence plumbing.
//   - Program: live edit of the ACTIVE program -- what is program-wide and has no
//     per-visual row of its own: global fps and the text/spiral colours. (The
//     per-visual-type weight rows used to be here too, duplicating the Visuals list of
//     the same built-ins; they moved to Visuals, next to the customs they share a
//     lottery with.) Mutates the in-memory session proto in place, then fires
//     on_program_change so ThemeBank/Director pick it up.
//   - Themes: per-theme weight rows (the program's enabled_theme entries; off = weight
//     0, entries are kept -- matching ThemeBank::set_program's semantics) + per-theme
//     image multiselect editing Theme::image_path. Content edits need a restart:
//     ThemeBank is built once at startup, no live rebuild.
//
// Weight rows (draw_weight_row, shared by Themes/Program/Visuals) are the one UI for
// "how often does this get picked": an on/off button over the same stash the old
// enable checkbox used, a 0..100 slider over the RAW random_weight, the row's
// EFFECTIVE share of its pool as a percent, and a pin toggle. Two pools, matching the
// two lotteries: themes (enabled_theme) and visuals (built-in visual_type PLUS enabled
// custom patterns -- Director::change_visual draws from both at once). Weight edits
// fire on_program_change on RELEASE, not per drag tick: set_program re-parses every
// custom pattern in the program.
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
  // `vr_failure` (#41) is empty in the normal case; non-empty when a VR renderer was
  // REQUESTED but failed to initialize and main.cpp fell back to the desktop window.
  // It is shown as a persistent banner at the top of the panel -- this UI only exists
  // in non-VR mode, which is exactly the fallback case, so it can carry the warning
  // that would otherwise be stderr-only (invisible on a Windows GUI launch).
  AppUi(trance_pb::Session& session, const std::string& session_path,
        SessionJsonSidecar& sidecar, trance_pb::System& system,
        const std::string& system_path, CommandRuntimeState& command_state,
        std::function<void()> on_program_change,
        std::function<trance_pb::Program*()> active_program,
        std::string vr_failure = {});
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
  void update(sf::RenderWindow& window, sf::Time dt, Director& director, Audio& audio,
             const ThemeBank& themes);

  // Issues ImGui's GL draw calls into `window`'s currently-bound buffer. Runs via
  // Renderer::set_ui_hook, after the frame's scene draw and before its display().
  // No-op unless a matching update() started an ImGui frame this iteration (ImGui
  // asserts on Render without a prior NewFrame).
  void render(sf::RenderWindow& window);

private:
  void draw_status_section(Director& director, Audio& audio, const ThemeBank& themes);
  void draw_visuals_section(Director& director);
  // The body of one built-in's expander: Force now, Copy, and its read-only v3 source
  // (word-wrapped). Shared by the editable path and the read-only one taken when the
  // active program is the built-in default.
  void draw_builtin_body(Director& director, uint32_t type, const char* blurb);
  void draw_program_section();
  void draw_themes_section();
  // The theme named by `name`'s parent DIRECTORY, skipping directories that have no
  // theme of their own; "" when nothing is above it. Mirrors the loader's rule
  // (resolve_theme_inheritance, session_json.cpp) so the panel and the next load agree.
  std::string theme_parent(const std::string& name) const;
  // The pool size `name` WOULD have on the next load given the inherit flags as they
  // stand. Recomputed from the sidecar's per-theme own counts rather than measured off
  // theme_map, which already has the last load's unions folded into it.
  uint32_t predicted_pool_size(const std::string& name) const;
  void draw_session_section();
  void draw_overlay_section();
  void draw_entrainment_section(Audio& audio);
  void draw_system_section();
  // First "custom_N" not already used by a custom_visual_pattern in `program`.
  // Duplicate names are a load-time error (session_json.cpp), so "+ New pattern"
  // has to seed a name that cannot collide.
  static std::string unique_pattern_name(const trance_pb::Program& program);

  // One weight row, shared by the Themes and Program/Visuals sections: an on/off
  // button (animates the weight to 0 and back via the stash), a 0..100 slider over
  // the RAW random_weight, the row's EFFECTIVE share of `pool_total` as a percent,
  // and an optional pin toggle. `key` identifies the row in the stash/tween maps
  // (see _visual_last_weight). `pinned` may be null for rows with no pin concept.
  // An EMPTY `label` means the caller draws the row's name itself (the Themes section
  // puts its TreeNode there). `slider_tooltip` may be null; it hangs off the slider
  // specifically, which is why it is a parameter rather than an IsItemHovered() at the
  // call site -- by then IsItem* refers to the row's last widget, not the slider.
  // `pool_pinned` says some row in this pool is pinned, so the lottery is skipped
  // entirely: the row then shows a flat 100%/0% instead of a weight share that would
  // describe a draw that never happens.
  //
  // Writes the new weight through `weight` / the new pin state through `pinned`, and
  // returns what changed so the caller can batch its _on_program_change(): weights
  // report on slider RELEASE (dragging re-parses every custom pattern per tick), pin
  // and on/off report immediately.
  struct WeightRowResult
  {
    bool weight_changed = false;  // committed (release / button), not per drag tick
    bool pin_changed = false;
  };
  WeightRowResult draw_weight_row(const char* label, const std::string& key, uint32_t* weight,
                                  bool* pinned, uint64_t pool_total,
                                  std::map<std::string, uint32_t>& stash,
                                  const char* slider_tooltip = nullptr, bool pool_pinned = false);

  // Sum of every weight in the visual pool: built-in visual_type entries PLUS
  // enabled custom patterns. They share one lottery (director.cpp's change_visual),
  // so they share one denominator for the percent labels.
  static uint64_t visual_pool_total(const trance_pb::Program& program);
  // True when some visual owns the pool by pin -- Director then skips the lottery, so
  // the rows' percent labels must read 100%/0% rather than a weight share.
  static bool any_visual_pinned(const trance_pb::Program& program);
  // Clear every visual pin except the one just set, whose identity is (`builtin_index`
  // for a built-in row) or (`custom_name` for a custom row) -- exactly one of the two
  // is meaningful, selected by `is_custom`. Mirrors the single-pin rule
  // validate_program enforces on load (session.cpp) so the UI and the loader agree.
  // Built-ins are identified by ROW INDEX because duplicate entries of one type are
  // legal, and clearing by type would leave both of a duplicated pair pinned.
  static void clear_other_visual_pins(trance_pb::Program& program, bool is_custom,
                                      int builtin_index, const std::string& custom_name);
  // The stash/tween key for a visual row: "b<row index>" built-in, "c<name>" custom.
  static std::string visual_row_key(bool is_custom, int builtin_index,
                                    const std::string& custom_name);
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
  // Why VR isn't running this session (#41), or empty if VR was never requested / is
  // running. Fixed at construction -- the renderer is built once at startup -- so this
  // banner is persistent for the run rather than timed out like _system_status.
  const std::string _vr_failure;

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

  // Last nonzero weight per theme, so the on/off button round-trips a theme's
  // weight instead of resetting it to 1 (off keeps the enabled_theme entry at
  // weight 0, matching ThemeBank::set_program's semantics).
  std::map<std::string, uint32_t> _theme_last_weight;
  // The same stash for visual rows, keyed by visual_row_key(): "b<row index>" for a
  // built-in visual_type entry, "c<name>" for a custom pattern. Built-ins key on the
  // ROW INDEX because duplicate entries of one type are legal and a type-derived key
  // would make the pair share a stash slot; customs key on the NAME because a Remove
  // shifts every row below it and a stale index would restore the wrong row's weight.
  // (A built-in with no row yet is drawn under a transient "bt<type>" key until its
  // entry materializes -- see the second pass in draw_visuals_section.)
  std::map<std::string, uint32_t> _visual_last_weight;

  // In-flight "animate the slider to 0" tweens from the off button, keyed the same
  // way as the stashes above ("t<name>" for themes). PURELY COSMETIC: the model
  // weight drops to 0 on the click, and the tween only walks the slider grab down to
  // meet it. `from` is the weight the row started at; update() advances `elapsed`
  // with its dt and drops the row once it lands.
  struct WeightTween
  {
    uint32_t from = 0;
    float elapsed = 0.f;
  };
  std::map<std::string, WeightTween> _weight_tweens;
  // The program the two stashes and the tween map describe. A change means the
  // playlist advanced under us and every stashed weight belongs to a different
  // program's identically-named row. Compared only for identity -- never dereferenced.
  const trance_pb::Program* _weight_stash_program = nullptr;

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
