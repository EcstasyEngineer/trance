#include <trance/ui/app_ui.h>
#include <common/session.h>
#include <common/session_json.h>
#include <trance/director.h>
#include <trance/media/audio.h>
#include <trance/runtime_state.h>
#include <trance/theme_bank.h>
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/builtin_visuals.h>
#include <trance/visual/pattern_parser_v3.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <common/trance.pb.h>
#include <imgui.h>
#include <imgui-SFML.h>
#include <imgui_stdlib.h>
#include <SFML/Graphics.hpp>
#pragma warning(pop)

namespace
{
  // How long the off button takes to walk a row's weight down to 0. Short enough to
  // read as "the row switched off", long enough to see which row it was.
  const float kWeightTweenSeconds = 0.2f;
  // Weight a row comes back on at when it has no stashed previous value.
  const uint32_t kDefaultRowWeight = 10;
  // Sliders are 0..100 so a raw weight reads directly as "percent-ish" when the pool
  // sums to 100; the EFFECTIVE share label next to it is the honest number.
  const int kMaxRowWeight = 100;

  const ImVec4 kActiveGreen{0.35f, 0.85f, 0.45f, 1.f};
  const ImVec4 kActiveGreenDim{0.20f, 0.45f, 0.25f, 1.f};
  const ImVec4 kWarnAmber{1.f, 0.75f, 0.3f, 1.f};
  const ImVec4 kPinGold{0.95f, 0.8f, 0.3f, 1.f};
}

AppUi::AppUi(trance_pb::Session& session, const std::string& session_path,
             SessionJsonSidecar& sidecar, trance_pb::System& system,
             const std::string& system_path, CommandRuntimeState& command_state,
             std::function<void()> on_program_change,
             std::function<trance_pb::Program*()> active_program, std::string vr_failure)
: _session{session}
, _session_path{session_path}
, _sidecar{sidecar}
, _system{system}
, _system_path{system_path}
, _command_state{command_state}
, _on_program_change{std::move(on_program_change)}
, _active_program{std::move(active_program)}
, _vr_failure{std::move(vr_failure)}
{
  // Seed Save As with the loaded path so "tweak the filename" is the common case.
  std::snprintf(_save_as_buf, sizeof(_save_as_buf), "%s", session_path.c_str());
}

AppUi::~AppUi()
{
  if (_initialized) {
    ImGui::SFML::Shutdown();
  }
}

bool AppUi::init(sf::RenderWindow& window)
{
  if (_initialized || _init_failed) {
    return _initialized;
  }
  _initialized = ImGui::SFML::Init(window);
  _init_failed = !_initialized;
  return _initialized;
}

void AppUi::process_event(sf::RenderWindow& window, const sf::Event& event)
{
  if (!_initialized) {
    return;
  }
  ImGui::SFML::ProcessEvent(window, event);
}

bool AppUi::wants_text_input() const
{
  // Guarded: ImGui::GetIO() needs the context ImGui::SFML::Init created.
  return _initialized && ImGui::GetIO().WantTextInput;
}

void AppUi::update(sf::RenderWindow& window, sf::Time dt, Director& director, Audio& audio,
                   const ThemeBank& themes)
{
  if (!_initialized) {
    return;
  }
  ImGui::SFML::Update(window, dt);
  _frame_started = true;
  if (_save_status_ttl > 0.f) {
    _save_status_ttl -= dt.asSeconds();
  }
  if (_system_status_ttl > 0.f) {
    _system_status_ttl -= dt.asSeconds();
  }
  // Weight stashes are keyed by visual TYPE / pattern NAME / theme NAME, none of which
  // are unique across programs: a playlist advance re-points _active_program at a
  // program whose "slow_flash" row is a different row with a different weight, and a
  // kept stash would restore the OLD program's weight onto it. Dropped here rather
  // than in a section, since a collapsed CollapsingHeader never draws to notice.
  const trance_pb::Program* current = _active_program ? _active_program() : nullptr;
  if (current != _weight_stash_program) {
    _weight_stash_program = current;
    _visual_last_weight.clear();
    _theme_last_weight.clear();
    _weight_tweens.clear();
  }

  // Advance the off-button tweens. The MODEL weight already went to 0 when the button
  // was clicked (and the section fired its on_program_change then), so this is purely
  // cosmetic: the slider grab walking down to meet a value that already changed. Run
  // here rather than in the sections because a collapsed CollapsingHeader doesn't draw
  // its section at all, and a tween left un-advanced would freeze its row's slider.
  for (auto it = _weight_tweens.begin(); it != _weight_tweens.end();) {
    it->second.elapsed += dt.asSeconds();
    if (it->second.elapsed >= kWeightTweenSeconds) {
      it = _weight_tweens.erase(it);
    } else {
      ++it;
    }
  }

  if (!_visible) {
    return;
  }

  // One window, top-left, with collapsing sections (the old three overlapping
  // windows folded in). FirstUseEver so the user can still move/resize it.
  ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420.f, 640.f), ImGuiCond_FirstUseEver);
  ImGui::Begin("trance");
  // Above every section, unmissable and never timed out: a requested VR backend failed
  // and this desktop window is the fallback (#41). Wrapped -- the reason string is
  // longer than the panel is wide.
  if (!_vr_failure.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
    ImGui::TextWrapped("VR UNAVAILABLE: %s", _vr_failure.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
  }
  if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
    draw_status_section(director, audio, themes);
  }
  if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_DefaultOpen)) {
    draw_visuals_section(director);
  }
  if (ImGui::CollapsingHeader("Program")) {
    draw_program_section();
  }
  if (ImGui::CollapsingHeader("Themes")) {
    draw_themes_section();
  }
  if (ImGui::CollapsingHeader("Session")) {
    draw_session_section();
  }
  if (ImGui::CollapsingHeader("Overlay")) {
    draw_overlay_section();
  }
  if (ImGui::CollapsingHeader("Entrainment")) {
    draw_entrainment_section(audio);
  }
  if (ImGui::CollapsingHeader("System")) {
    draw_system_section();
  }
  // Quit lives at the very bottom, clearly separated from the sections above --
  // F2 only ever toggles this panel, so this button (plus Escape, the tray Quit
  // and the window close button) is the way out.
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button("Quit trance")) {
    _quit_requested = true;
  }
  ImGui::End();
}

void AppUi::render(sf::RenderWindow& window)
{
  if (!_initialized || !_frame_started) {
    return;
  }
  _frame_started = false;
  // The scene renderer (director.cpp) leaves its GLSL program bound and drives GL
  // state imgui-sfml's fixed-function renderer never resets (it only backs up what it
  // touches). A bound program in particular bypasses fixed-function texturing entirely,
  // which garbles every glyph quad. Neutralize before handing the context to ImGui;
  // ImGui::SFML::Render's own resetGLStates handles the SFML-visible remainder.
  glUseProgram(0);
  glActiveTexture(GL_TEXTURE0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  ImGui::SFML::Render(window);

  // Repair imgui-sfml 3.0's dynamic font-atlas updates (imgui 1.92 rasterizes glyphs on
  // demand and ships them as dirty sub-rects). Its WantUpdates path uploads each rect via
  // sf::Texture::update, which expects TIGHTLY-PACKED pixels -- but ImTextureData's
  // GetPixelsAt points into the full-stride atlas, so every partial glyph upload shears
  // ("missingno" text; boxes/separators stay clean because the white texel comes from the
  // correct initial full-atlas WantCreate upload). Re-upload this frame's rects with the
  // correct row stride; imgui only clears tex->Updates at the next NewFrame, so they are
  // still valid here. Cost: a new glyph draws garbled for exactly one frame.
  for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
    if (tex->Format != ImTextureFormat_RGBA32 || tex->Status != ImTextureStatus_OK ||
        tex->Updates.empty() || !tex->GetTexID()) {
      continue;
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex->GetTexID()));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->Width);
    for (const ImTextureRect& r : tex->Updates) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE,
                      tex->GetPixelsAt(r.x, r.y));
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}

void AppUi::draw_status_section(Director& director, Audio& audio, const ThemeBank& themes)
{
  // Reuses the same accessors draw_debug_overlay() (director.cpp, F1) reads --
  // no new Director surface added for this section. ThemeBank is passed in directly
  // from main.cpp's play_session() (which already owns it), rather than adding a
  // new Director accessor for it.
  const auto& program = director.program();
  ImGui::Text("global fps (config): %u", program.global_fps());
  ImGui::Text("vr enabled: %s", director.vr_enabled() ? "yes" : "no");
  // TODO: no public Director accessor for the current visual name.

  auto snap = themes.debug_snapshot();
  ImGui::Text("theme primary  : %s", snap.slots[1].valid ? snap.slots[1].name.c_str() : "(empty)");
  ImGui::Text("theme alternate: %s", snap.slots[2].valid ? snap.slots[2].name.c_str() : "(empty)");

  const auto& entrainment = program.entrainment();
  if (entrainment.layer().empty()) {
    ImGui::TextUnformatted("bed: (none)");
  } else {
    float master_db = entrainment.master_db() != 0.f ? entrainment.master_db() : -28.f;
    ImGui::Text("bed: %d layer(s), master %.1f dB%s", entrainment.layer_size(), master_db,
               audio.Muted() ? "  [MUTED]" : "");
  }
}

uint64_t AppUi::visual_pool_total(const trance_pb::Program& program)
{
  uint64_t total = 0;
  for (const auto& type : program.visual_type()) {
    total += type.random_weight();
  }
  for (const auto& pattern : program.custom_visual_pattern()) {
    // Disabled patterns are skipped by rebuild_custom_patterns, so they contribute
    // nothing to the real lottery and must not dilute the percentages here either.
    if (pattern.enabled()) {
      total += pattern.random_weight();
    }
  }
  return total;
}

bool AppUi::any_visual_pinned(const trance_pb::Program& program)
{
  for (const auto& type : program.visual_type()) {
    if (type.pinned()) {
      return true;
    }
  }
  for (const auto& pattern : program.custom_visual_pattern()) {
    if (pattern.pinned() && pattern.enabled()) {
      return true;
    }
  }
  return false;
}

void AppUi::clear_other_visual_pins(trance_pb::Program& program, bool is_custom, int builtin_index,
                                    const std::string& custom_name)
{
  for (int i = 0; i < program.visual_type_size(); ++i) {
    auto* config = program.mutable_visual_type(i);
    // By ROW INDEX, not by type: nothing forbids two visual_type entries of the same
    // type, and identifying by type would leave both of them pinned.
    if (is_custom || i != builtin_index) {
      config->set_pinned(false);
    }
  }
  for (int i = 0; i < program.custom_visual_pattern_size(); ++i) {
    auto* pattern = program.mutable_custom_visual_pattern(i);
    if (!is_custom || pattern->name() != custom_name) {
      pattern->set_pinned(false);
    }
  }
}

std::string AppUi::visual_row_key(bool is_custom, int builtin_index, const std::string& custom_name)
{
  // Built-ins key on their ROW INDEX, not their type: duplicate visual_type entries of
  // the same type are legal, and a type-derived key would make the two rows share one
  // stash slot and one tween (switching one off would animate the other).
  return is_custom ? "c" + custom_name : "b" + std::to_string(builtin_index);
}

AppUi::WeightRowResult AppUi::draw_weight_row(const char* label, const std::string& key,
                                              uint32_t* weight, bool* pinned, uint64_t pool_total,
                                              std::map<std::string, uint32_t>& stash,
                                              const char* slider_tooltip, bool pool_pinned)
{
  WeightRowResult result;
  ImGui::PushID(key.c_str());

  // A tween in flight owns the displayed weight: the model value is already 0 (set
  // when the button was clicked, so the runtime switched the row off immediately),
  // and the animation is purely the slider grab walking down to meet it.
  auto tween = _weight_tweens.find(key);
  const bool animating = tween != _weight_tweens.end();
  int shown = static_cast<int>(*weight);
  if (animating) {
    float t = tween->second.elapsed / kWeightTweenSeconds;
    shown = static_cast<int>(static_cast<float>(tween->second.from) * (1.f - t));
  }
  const bool on = *weight > 0;

  // On/off button. Off stashes the current weight and starts the walk-to-0; on
  // restores the stash (or a sensible default for a row that has never been on).
  // "###" (not "##"): with ## the ID still hashes the WHOLE label, so a flipping
  // "on"/"off" would hand the button a new identity on every toggle and drop
  // keyboard/gamepad focus mid-interaction. ### takes the ID from the suffix alone,
  // which is the only form that actually keeps it stable.
  if (ImGui::Button(on ? "on###toggle" : "off###toggle", ImVec2(32.f, 0.f))) {
    if (on) {
      stash[key] = *weight;
      _weight_tweens[key] = WeightTween{*weight, 0.f};
      *weight = 0;
    } else {
      auto it = stash.find(key);
      *weight = it != stash.end() && it->second ? it->second : kDefaultRowWeight;
      _weight_tweens.erase(key);
    }
    result.weight_changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Toggle this row off (weight 0) and back on at its previous weight.");
  }
  ImGui::SameLine();

  // Pin. Single-pin across the pool is the CALLER's job (it owns the sibling rows);
  // this only reports the click.
  if (pinned) {
    // Latched BEFORE the button: the click flips *pinned, so testing it again for the
    // pop would pop a push that never happened (or skip one that did) and corrupt
    // ImGui's style stack on every toggle.
    const bool was_pinned = *pinned;
    if (was_pinned) {
      ImGui::PushStyleColor(ImGuiCol_Button, kPinGold);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 0.f, 0.f, 1.f));
    }
    if (ImGui::Button("pin", ImVec2(32.f, 0.f))) {
      *pinned = !*pinned;
      result.pin_changed = true;
    }
    if (was_pinned) {
      ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Pin: at most one per pool. Themes pin as always-resident (the\n"
                        "weights then only pick the OTHER slot); visuals pin as\n"
                        "force-this-one (the weight lottery is skipped).");
    }
    ImGui::SameLine();
  }

  // Green accent while the row is contributing; an animating row is on its way out,
  // so it greys with the rest.
  const bool accent = on && !animating;
  if (accent) {
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, kActiveGreen);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kActiveGreen);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kActiveGreenDim);
  }
  ImGui::SetNextItemWidth(130.f);
  // Disabled only for input: the bar still redraws each frame, which is what makes
  // the animation visible.
  ImGui::BeginDisabled(animating);
  if (ImGui::SliderInt("##weight", &shown, 0, kMaxRowWeight, "weight %d")) {
    *weight = static_cast<uint32_t>(std::max(0, std::min(kMaxRowWeight, shown)));
  }
  ImGui::EndDisabled();
  // EndDisabled pushes no item of its own, so IsItem* here still refers to the slider.
  // Committed on release, not per tick: a drag would otherwise re-parse every custom
  // pattern in the program on every frame of the drag (Director::set_program).
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    if (*weight) {
      stash[key] = *weight;
    }
    result.weight_changed = true;
  }
  if (slider_tooltip && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", slider_tooltip);
  }
  if (accent) {
    ImGui::PopStyleColor(3);
  }
  ImGui::SameLine();

  // The honest number: the raw weight above is only meaningful against the pool total.
  // pool_total 0 means the caller told us a pin owns the pool outright -- the lottery
  // is skipped, so the weights no longer describe anything and a percentage would lie.
  if (pool_pinned) {
    const bool this_row = pinned && *pinned;
    ImGui::TextColored(this_row ? kPinGold : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                       this_row ? " 100%%" : "   0%%");
  } else if (pool_total) {
    float share = 100.f * static_cast<float>(*weight) / static_cast<float>(pool_total);
    ImGui::TextColored(accent ? kActiveGreen : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled],
                       "%5.1f%%", share);
  } else {
    ImGui::TextDisabled("   --");
  }
  // Empty label = the caller draws the row's name itself (the Themes section puts a
  // TreeNode there); don't emit a zero-width text item and its trailing spacing.
  if (label && *label) {
    ImGui::SameLine();
    if (accent) {
      ImGui::TextUnformatted(label);
    } else {
      ImGui::TextDisabled("%s", label);
    }
  }

  ImGui::PopID();
  return result;
}

void AppUi::draw_builtin_body(Director& director, uint32_t type, const char* blurb)
{
  if (ImGui::Button("Force now")) {
    director.force_builtin_visual(type);
  }
  ImGui::SameLine();
  // Built-in sources are compile-time constants (builtin_patterns_v3.cpp) -- shown
  // read-only, as the modding-language reference for writing a custom pattern. Copy
  // is the intended path to editing one: paste into a new custom pattern below.
  // Returned by value; ReadOnly means InputTextMultiline never writes through the
  // pointer, so handing it this frame-local buffer is safe.
  std::string source = builtin::pattern_source_v3(type);
  if (ImGui::Button("Copy")) {
    ImGui::SetClipboardText(source.c_str());
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", blurb);
  // WordWrap (imgui 1.92, multiline-only): pattern sources have lines far wider than
  // this panel, and horizontal scrolling to read one is miserable.
  ImGui::InputTextMultiline("##builtin_source", &source, ImVec2(-FLT_MIN, 180.f),
                            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
}

void AppUi::draw_visuals_section(Director& director)
{
  // Editing needs the MUTABLE active program. When the playlist resolves to the
  // built-in default (no program_map entry) there is nothing in the session to edit,
  // so fall back to the read-only force-now list off Director's const view.
  trance_pb::Program* program = _active_program ? _active_program() : nullptr;
  if (!program) {
    ImGui::TextUnformatted("Built-ins (click to force now):");
    for (const auto& visual : builtin_visuals()) {
      ImGui::PushID(static_cast<int>(visual.type));
      if (ImGui::TreeNode(visual.name)) {
        draw_builtin_body(director, visual.type, visual.blurb);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Custom patterns (this program):");
    ImGui::TextDisabled("(active program is the built-in default -- not editable)");
    for (const auto& pattern : director.program().custom_visual_pattern()) {
      if (!pattern.enabled()) {
        continue;
      }
      ImGui::PushID(pattern.name().c_str());
      if (ImGui::Button(pattern.name().c_str())) {
        _last_pattern_error =
            director.force_pattern_from_source(pattern.source_text(), pattern.name());
      }
      ImGui::PopID();
    }
    if (!_last_pattern_error.empty()) {
      ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "parse error: %s",
                         _last_pattern_error.c_str());
    }
    return;
  }

  // Row indices (the lint cache key) only mean anything relative to one program: a
  // playlist switch re-points _active_program at a different program whose row 0 is a
  // different pattern, so a kept verdict would describe the wrong pattern. Drop them.
  // The rows' ImGui IDs are per-index too, so an edit active across such a switch
  // lands in the new program's row -- rare (it needs a playlist advance mid-keystroke)
  // and self-correcting on defocus, so it is not worth an imgui_internal.h dependency
  // to force-clear the active ID here.
  if (program != _pattern_lint_program) {
    _pattern_lint_program = program;
    _pattern_lint.clear();
  }

  // Built-ins and custom patterns share ONE lottery (director.cpp's change_visual), so
  // they now share one section and one denominator -- the built-in weight rows used to
  // live over in the Program section, which meant the same pool was split across two
  // menus with the source viewer in one and the weights in the other. Sampled once for
  // the frame so every row's percent is against the same total even mid-drag.
  const uint64_t pool_total = visual_pool_total(*program);
  // A pinned visual owns the pool: change_visual returns it every time and never runs
  // the lottery, so the rows show 100%/0% rather than a share of a draw that no
  // longer happens. Sampled with pool_total, for the same stability reason.
  const bool pool_pinned = any_visual_pinned(*program);
  bool pool_changed = false;
  const char* kVisualSliderTooltip =
      "Selection weight, shared across ALL visuals -- built-ins and this\n"
      "program's custom patterns run one combined lottery. 0 = never picked.\n"
      "A PINNED visual is forced: the lottery is skipped entirely.";

  ImGui::TextUnformatted("Built-ins:");
  // Two passes over two different lists, so they get two explicitly NAMED ID scopes
  // rather than sharing the outer one and relying on their index spaces not to overlap.
  ImGui::PushID("configured-builtins");
  // Pass 1: the program's visual_type ROWS, by index. Nothing forbids two entries of
  // the same type, and the row key/ImGui ID are index-derived (see visual_row_key) so
  // duplicates stay distinct rather than one row swallowing the other's clicks.
  for (int i = 0; i < program->visual_type_size(); ++i) {
    auto* config = program->mutable_visual_type(i);
    const uint32_t type = static_cast<uint32_t>(config->type());
    const char* label = nullptr;
    const char* blurb = "";
    for (const auto& visual : builtin_visuals()) {
      if (visual.type == type) {
        label = visual.name;
        blurb = visual.blurb;
        break;
      }
    }
    char fallback[32];
    if (!label) {
      std::snprintf(fallback, sizeof(fallback), "type %u", type);
      label = fallback;
    }
    ImGui::PushID(i);
    // Written through live so the slider tracks the drag and the percent label stays
    // truthful; pool_changed (the on_program_change trigger) only fires on release.
    uint32_t weight = config->random_weight();
    bool pinned = config->pinned();
    // Empty label: the TreeNode below names the row, matching the Themes section's
    // layout (weight row, then the name as an expander holding the row's content).
    auto row = draw_weight_row("", visual_row_key(false, i, {}), &weight, &pinned, pool_total,
                               _visual_last_weight, kVisualSliderTooltip, pool_pinned);
    config->set_random_weight(weight);
    if (row.weight_changed) {
      pool_changed = true;
    }
    if (row.pin_changed) {
      // Set before the sweep so the sweep can leave this one alone; clearing is
      // unconditional the other way (unpinning just leaves the pool unpinned).
      config->set_pinned(pinned);
      if (pinned) {
        clear_other_visual_pins(*program, false, i, {});
      }
      pool_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::TreeNode(label)) {
      draw_builtin_body(director, type, blurb);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::PopID();

  ImGui::PushID("missing-builtins");
  // Pass 2: built-ins with NO visual_type row at all. validate_program only rebuilds
  // the default set when the whole pool is weightless (session.cpp), so a session can
  // legitimately omit a type -- and before the sections merged, such a type was still
  // forceable from here. Draw it at weight 0 and materialize the entry on first touch
  // (the ensure_entry idiom the Themes section uses), so it stays both forceable and
  // re-addable to the lottery.
  for (const auto& visual : builtin_visuals()) {
    bool present = false;
    for (const auto& type : program->visual_type()) {
      if (static_cast<uint32_t>(type.type()) == visual.type) {
        present = true;
        break;
      }
    }
    if (present) {
      continue;
    }
    ImGui::PushID(static_cast<int>(visual.type));
    uint32_t weight = 0;
    bool pinned = false;
    // Type-derived key, not index-derived: there is no row index yet. Once the entry
    // materializes the row moves to its "b<index>" key next frame -- the orphaned
    // stash entry is cosmetic (the row is at 0 with nothing worth restoring).
    auto row = draw_weight_row("", "bt" + std::to_string(visual.type), &weight, &pinned,
                               pool_total, _visual_last_weight, kVisualSliderTooltip,
                               pool_pinned);
    if (row.weight_changed || row.pin_changed) {
      auto* added = program->add_visual_type();
      added->set_type(static_cast<trance_pb::Program::VisualType>(visual.type));
      added->set_random_weight(weight);
      if (row.pin_changed && pinned) {
        added->set_pinned(true);
        clear_other_visual_pins(*program, false, program->visual_type_size() - 1, {});
      }
      pool_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::TreeNode(visual.name)) {
      ImGui::TextDisabled("(not in this program's pool -- switch the row on to add it)");
      draw_builtin_body(director, visual.type, visual.blurb);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::PopID();

  // The rescue validate_program does on reload is BUILT-IN weights only, and a pinned
  // built-in suppresses it -- so warn on exactly that condition, not on the combined
  // pool_total the percentages use.
  {
    uint64_t builtin_total = 0;
    bool builtin_pinned = false;
    for (const auto& type : program->visual_type()) {
      builtin_total += type.random_weight();
      builtin_pinned = builtin_pinned || type.pinned();
    }
    if (!builtin_total && !builtin_pinned) {
      ImGui::TextColored(kWarnAmber, "all built-in weights 0 -- resets to defaults on reload");
    }
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Custom patterns (this program):");
  if (ImGui::Button("+ New pattern")) {
    auto* added = program->add_custom_visual_pattern();
    added->set_name(unique_pattern_name(*program));
    // Minimal valid v3 source: one pattern span with one cadence drawing a concept
    // image. Named to match so `pattern NAME` and the proto name agree on sight
    // (Director carries the proto's name as authoritative either way).
    added->set_source_text("pattern " + added->name() + " for 1024f {\n"
                           "  every 64f { image concept zoom (curve 0 -> 0.375) }\n"
                           "  spiral speed 2\n"
                           "}\n");
    added->set_random_weight(1);
    added->set_enabled(true);
    // New rows compile immediately so the lint line starts out truthful, and the
    // pattern joins the shuffle without a separate Apply click.
    if (_on_program_change) {
      _on_program_change();
    }
  }
  if (!program->custom_visual_pattern_size()) {
    ImGui::TextDisabled("(none in this program)");
  }

  // Index of a row the user asked to remove, applied after the loop: erasing from the
  // repeated field mid-iteration would invalidate the row we are still drawing.
  int remove_index = -1;
  for (int i = 0; i < program->custom_visual_pattern_size(); ++i) {
    auto* pattern = program->mutable_custom_visual_pattern(i);
    // By index, not by name: the name is editable, and an ID that changes mid-edit
    // would tear down the InputText that is being typed into.
    ImGui::PushID(i);
    // Weight/pin row, same widget and the same pool denominator the built-ins above
    // use -- change_visual runs one lottery over both (director.cpp).
    // `enabled` (the proto's own off switch) is kept in step with the weight:
    // rebuild_custom_patterns skips a disabled pattern outright, which is the truest
    // "off", and a 0-weight row would never be picked anyway.
    {
      // A pattern saved as enabled:false is off no matter what weight it carries
      // (rebuild_custom_patterns skips it), so the row must READ as off too -- show it
      // at 0 rather than green-and-weighted. Its stored weight lives on in the stash,
      // so the on button restores it rather than falling back to the default.
      uint32_t weight = pattern->enabled() ? pattern->random_weight() : 0;
      if (!pattern->enabled() && pattern->random_weight()) {
        auto key = visual_row_key(true, 0, pattern->name());
        _visual_last_weight.emplace(key, pattern->random_weight());
      }
      bool pinned = pattern->pinned();
      // Empty label: the TreeNode below names the row (the Themes section's layout).
      auto row = draw_weight_row("", visual_row_key(true, 0, pattern->name()), &weight, &pinned,
                                 pool_total, _visual_last_weight, kVisualSliderTooltip,
                                 pool_pinned);
      // Only ever written on a real user edit. The pass-through case (drawing a
      // disabled pattern, which the row shows at 0) must not clobber the weight the
      // pattern was saved with, and must not switch it on just by being drawn.
      if (row.weight_changed || weight != (pattern->enabled() ? pattern->random_weight() : 0)) {
        pattern->set_random_weight(weight);
        pattern->set_enabled(weight > 0);
        // A pin on a disabled pattern is dead: rebuild_custom_patterns skips the
        // pattern entirely, so the panel would draw an active force that does nothing
        // and Save would persist a pin validate_program strips on the next load.
        if (!pattern->enabled()) {
          pattern->set_pinned(false);
        }
      }
      if (row.weight_changed) {
        pool_changed = true;
      }
      // Pinning is only meaningful for a pattern that is actually in the lottery.
      if (row.pin_changed && pattern->enabled()) {
        pattern->set_pinned(pinned);
        if (pinned) {
          clear_other_visual_pins(*program, true, 0, pattern->name());
        }
        pool_changed = true;
      }
    }

    // Live lint: patternv3::parse is pure and cheap enough to run on edit, but not
    // per frame for every row -- cache the diagnostic and only re-parse when the row
    // changed (or was never linted). Parsed with locked_period_frames 0: the beat
    // period is Director's to supply, and a `locked` length would report a spurious
    // error here, so treat that as the one case the real compile decides.
    // Computed BEFORE the row is drawn so a collapsed row can still show its verdict.
    auto lint = _pattern_lint.find(i);
    if (lint == _pattern_lint.end()) {
      std::string message;
      for (int j = 0; j < program->custom_visual_pattern_size(); ++j) {
        if (j != i && program->custom_visual_pattern(j).name() == pattern->name()) {
          message = "duplicate name '" + pattern->name() + "' (a load-time error)";
          break;
        }
      }
      if (message.empty() && pattern->name().empty()) {
        message = "empty name (a load-time error)";
      }
      if (message.empty()) {
        auto parsed = patternv3::parse(pattern->source_text(), 0);
        if (!parsed.ok) {
          message = parsed.error;
        }
      }
      lint = _pattern_lint.emplace(i, std::move(message)).first;
    }
    // COPY, not the iterator: the widgets below invalidate it mid-frame -- a name edit
    // clears the whole cache and a source edit erases this row's entry -- and reading
    // through the dead iterator afterwards is undefined behaviour on every keystroke.
    // The copy also keeps the row's verdict self-consistent for the rest of the frame;
    // the re-parse it triggered lands on the next one.
    const std::string lint_message = lint->second;

    ImGui::SameLine();
    // "###row": the visible label is the EDITABLE name, so hashing the whole label
    // would hand the node a new identity on every keystroke and collapse it mid-edit.
    // ### takes the ID from the suffix alone, which keeps it stable.
    const std::string node_label =
        (pattern->name().empty() ? std::string("(unnamed)") : pattern->name()) + "###row";
    const bool open = ImGui::TreeNode(node_label.c_str());
    // Verdict on the header line too, so a broken pattern is visible while collapsed.
    ImGui::SameLine();
    if (lint_message.empty()) {
      ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "OK");
    } else {
      ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "!");
    }
    if (open) {
      ImGui::SetNextItemWidth(200.f);
      if (ImGui::InputText("##name", pattern->mutable_name())) {
        // Duplicate names are a load-time error (session_json.cpp), so the lint has to
        // catch them here rather than at Save. The WHOLE cache goes, not just this row:
        // duplicate-ness is a property of the entire list, so renaming a to b must also
        // re-lint the row already called b (and renaming away from a collision must
        // clear the other row's error).
        _pattern_lint.clear();
      }
      ImGui::SameLine();
      if (ImGui::Button("Apply")) {
        // Re-parse through Director (rebuild_custom_patterns) so the edit reaches the
        // live shuffle. Lint below already told the user whether this will take.
        if (_on_program_change) {
          _on_program_change();
        }
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!pattern->enabled());
      if (ImGui::Button("Force now")) {
        // Same path force_pattern_from_source always uses (director.cpp): on a parse
        // failure it returns the parser diagnostic and leaves the current visual
        // untouched. Surfaced inline rather than only to stderr since this is an
        // interactive action.
        _last_pattern_error =
            director.force_pattern_from_source(pattern->source_text(), pattern->name());
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Remove")) {
        remove_index = i;
      }

      // WordWrap: pattern lines routinely run wider than the panel, and editing one by
      // horizontal-scrolling is miserable. Multiline-only flag (imgui 1.92).
      if (ImGui::InputTextMultiline("##source", pattern->mutable_source_text(),
                                    ImVec2(-FLT_MIN, 160.f),
                                    ImGuiInputTextFlags_WordWrap)) {
        _pattern_lint.erase(i);
      }
      if (!lint_message.empty()) {
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", lint_message.c_str());
      }
      ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::PopID();
  }

  if (remove_index >= 0) {
    // No swap-and-pop: order is user-visible here, and DeleteSubrange keeps it.
    program->mutable_custom_visual_pattern()->DeleteSubrange(remove_index, 1);
    // Every cached diagnostic is keyed by row index, and the rows just shifted.
    _pattern_lint.clear();
    pool_changed = true;
  }
  // One re-push for the whole section, on release/click only (see draw_weight_row):
  // Director::set_program re-parses every custom pattern, so a per-drag-tick fire
  // would recompile the program on every frame of a slider drag.
  if (pool_changed && _on_program_change) {
    _on_program_change();
  }

  if (!_last_pattern_error.empty()) {
    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "parse error: %s", _last_pattern_error.c_str());
  }
}

std::string AppUi::unique_pattern_name(const trance_pb::Program& program)
{
  // Duplicate custom_visual_pattern names are a hard load-time error
  // (session_json.cpp), so a new row must not collide with an existing one.
  for (int n = 1;; ++n) {
    std::string candidate = "custom_" + std::to_string(n);
    bool taken = false;
    for (const auto& existing : program.custom_visual_pattern()) {
      if (existing.name() == candidate) {
        taken = true;
        break;
      }
    }
    if (!taken) {
      return candidate;
    }
  }
}

void AppUi::draw_program_section()
{
  // Live edit of the ACTIVE program: mutations land in the session proto in place
  // (map value addresses stay stable -- fields only, never map reorder/erase), then
  // on_program_change re-pushes it into ThemeBank/Director, the same pair the
  // playlist-switch path in play_session calls.
  trance_pb::Program* program = _active_program ? _active_program() : nullptr;
  if (!program) {
    // The playlist item resolves to the built-in default program (no program_map
    // entry) -- nothing in the session to edit.
    ImGui::TextDisabled("(active program is the built-in default -- not editable)");
    return;
  }
  bool changed = false;

  int fps = static_cast<int>(program->global_fps());
  if (ImGui::DragInt("global fps", &fps, 0.25f, 15, 240, "%d", ImGuiSliderFlags_AlwaysClamp)) {
    program->set_global_fps(static_cast<uint32_t>(std::max(15, std::min(240, fps))));
    changed = true;
  }

  // Built-in visual weights used to live here, duplicating the Visuals section's list
  // of the same built-ins: one menu had the weights, the other had the source viewer,
  // for one shared lottery. They are now one row apiece in Visuals, next to the custom
  // patterns they actually compete with. This section keeps what is genuinely
  // program-wide and has no per-visual row: fps and colours.

  ImGui::Separator();
  ImGui::TextUnformatted("Colours:");
  // mutable_ on an unset Colour materializes it zeroed -- same value the renderer
  // was already reading through the const accessor, so no visible change until the
  // user actually drags.
  auto edit_colour = [&](const char* label, trance_pb::Colour* colour) {
    float rgba[4] = {colour->r(), colour->g(), colour->b(), colour->a()};
    if (ImGui::ColorEdit4(label, rgba)) {
      colour->set_r(rgba[0]);
      colour->set_g(rgba[1]);
      colour->set_b(rgba[2]);
      colour->set_a(rgba[3]);
      changed = true;
    }
  };
  edit_colour("main text", program->mutable_main_text_colour());
  edit_colour("shadow text", program->mutable_shadow_text_colour());
  edit_colour("spiral a", program->mutable_spiral_colour_a());
  edit_colour("spiral b", program->mutable_spiral_colour_b());

  if (changed && _on_program_change) {
    _on_program_change();
  }
}

std::string AppUi::theme_parent(const std::string& name) const
{
  // Mirrors resolve_theme_inheritance (session_json.cpp): a theme's parent is the theme
  // named by its parent DIRECTORY, and a top-level theme's parent is the scan root's own
  // loose-file theme. Skips directories that have no theme of their own (pure containers)
  // so an intermediate container doesn't silently break the chain. Returns "" when
  // nothing above it exists -- which is always the case for kRootThemeName itself.
  if (name == kRootThemeName) {
    return {};
  }
  auto candidate = name;
  while (true) {
    auto slash = candidate.find_last_of('/');
    candidate = slash == std::string::npos ? std::string{kRootThemeName} : candidate.substr(0, slash);
    if (_session.theme_map().count(candidate)) {
      return candidate;
    }
    if (candidate == kRootThemeName) {
      return {};
    }
  }
}

uint32_t AppUi::predicted_pool_size(const std::string& name) const
{
  // What this theme's pool WOULD be on the next load, given the inherit flags as they
  // stand right now. Inheritance is resolved at load time (ThemeBank has no live rebuild
  // path), so the live theme_map already has the last load's unions folded in and can't
  // be measured directly -- this recomputes from the per-theme OWN counts the loader
  // recorded before unioning. Same transitive walk the loader does, so the number the
  // button shows is the number the next restart produces.
  uint32_t total = 0;
  auto current = name;
  while (!current.empty()) {
    auto own = _sidecar.theme_own_count.find(current);
    if (own != _sidecar.theme_own_count.end()) {
      total += own->second;
    }
    if (!_sidecar.theme_inherit.count(current)) {
      break;
    }
    current = theme_parent(current);
  }
  return total;
}

void AppUi::draw_themes_section()
{
  // ThemeBank is built once at startup and has no live-rebuild path; image_path
  // edits only take effect via Save + restart. Weight/enable edits DO live-apply
  // (they go through ThemeBank::set_program like a playlist switch).
  ImGui::TextDisabled("(content changes need restart)");
  trance_pb::Program* program = _active_program ? _active_program() : nullptr;
  if (!program) {
    ImGui::TextDisabled("(active program is the built-in default -- weights not editable)");
  }
  bool changed = false;

  // Denominator for the rows' percent labels: the sum over the program's enabled_theme
  // entries, which is exactly the total ThemeBank's rotation lottery divides by.
  // Sampled once per frame so a drag doesn't make every OTHER row's number jitter.
  uint64_t theme_pool_total = 0;
  if (program) {
    for (const auto& theme : program->enabled_theme()) {
      theme_pool_total += theme.random_weight();
    }
  }

  // Protobuf map iteration order is unspecified (and can differ run to run); sort
  // the names so rows don't jump around.
  std::vector<std::string> names;
  names.reserve(_session.theme_map().size());
  for (const auto& pair : _session.theme_map()) {
    names.push_back(pair.first);
  }
  std::sort(names.begin(), names.end());

  // Folder-liveness. There is no "frozen session" branch here any more: the loader
  // defaults every session to a live scan root and migrates legacy frozen themes on the
  // way in, so the only question left is whether NEW folders join automatically.
  {
    bool auto_rescan = _sidecar.theme_scan_root_auto;
    if (ImGui::Checkbox("auto re-scan folder for new content", &auto_rescan)) {
      _sidecar.theme_scan_root_auto = auto_rescan;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("ON: directories that appear become themes, re-derived every load.\n"
                        "OFF: the theme LIST is frozen as it is now -- but each existing\n"
                        "theme still follows its own folder, so new FILES still appear.");
    }
    if (!auto_rescan) {
      ImGui::TextColored(kWarnAmber, "new folders will not become themes while this is off");
    }
    ImGui::Separator();
  }

  // Inherit-all / inherit-none. Per-theme inheritance composes transitively (a theme
  // reaches its grandparent only if its parent inherits too), so these two buttons are
  // the ends of the range: ALL is "every theme folds in everything above it", NONE is
  // "every theme is exactly its own folder". Everything between is one checkbox at a
  // time. Only themes that HAVE a parent are touched -- the root theme can't inherit.
  {
    // Scan themes ONLY, matching the per-row button: `inherit` is a key inside the scan
    // object, so an explicit image-list theme has nowhere to persist it. Counting those
    // here would let "all" set a flag that save drops on the floor, and the predicted
    // pool sizes would promise inheritance the next load can't perform.
    auto inheritable_theme = [this](const std::string& name) {
      return _sidecar.theme_scan.count(name) != 0 && !theme_parent(name).empty();
    };
    size_t inheritable = 0;
    size_t inheriting = 0;
    for (const auto& name : names) {
      if (inheritable_theme(name)) {
        ++inheritable;
        if (_sidecar.theme_inherit.count(name)) {
          ++inheriting;
        }
      }
    }
    ImGui::TextUnformatted("Inherit parent folder:");
    ImGui::SameLine();
    ImGui::BeginDisabled(!inheritable || inheriting == inheritable);
    if (ImGui::Button("all")) {
      for (const auto& name : names) {
        if (inheritable_theme(name)) {
          _sidecar.theme_inherit.insert(name);
        }
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!inheriting);
    if (ImGui::Button("none")) {
      _sidecar.theme_inherit.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu/%zu)", inheriting, inheritable);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("How many themes with a parent folder are inheriting it.\n"
                        "Inheritance chains: a theme reaches its grandparent only if\n"
                        "its parent inherits too.");
    }
  }
  ImGui::Separator();

  for (const auto& name : names) {
    ImGui::PushID(name.c_str());
    if (program) {
      // Find this theme's enabled_theme entry. enabled == entry with weight > 0;
      // disabling keeps the entry at weight 0, matching ThemeBank::set_program.
      trance_pb::Program::EnabledTheme* entry = nullptr;
      for (int i = 0; i < program->enabled_theme_size(); ++i) {
        if (program->enabled_theme(i).theme_name() == name) {
          entry = program->mutable_enabled_theme(i);
          break;
        }
      }
      auto ensure_entry = [&]() -> trance_pb::Program::EnabledTheme* {
        if (!entry) {
          // Appending to the repeated field is safe: only the map VALUE addresses
          // must stay stable for Director/ThemeBank, and they do.
          entry = program->add_enabled_theme();
          entry->set_theme_name(name);
        }
        return entry;
      };
      // Touching either control materializes the entry: a theme with no
      // enabled_theme row is weight 0 / unpinned by definition, so the row can draw
      // from those defaults and only commit an entry once the user actually acts.
      uint32_t weight = entry ? entry->random_weight() : 0;
      bool pinned = entry && entry->pinned();
      const uint32_t before_weight = weight;
      auto row = draw_weight_row("", "t" + name, &weight, &pinned, theme_pool_total,
                                 _theme_last_weight,
                                 "Rotation weight: each theme swap picks the next theme with\n"
                                 "chance weight/total across enabled themes. 0 = never picked.\n"
                                 "A PINNED theme stays resident even at weight 0 -- the weights\n"
                                 "then only choose the other of the two live slots.\n"
                                 "(The bank still only keeps its 4-slot window loaded.)");
      if (weight != before_weight) {
        ensure_entry()->set_random_weight(weight);
      }
      if (row.weight_changed) {
        changed = true;
      }
      if (row.pin_changed) {
        ensure_entry()->set_pinned(pinned);
        if (pinned) {
          // Single pin per program, the rule validate_program enforces on load
          // (session.cpp) -- setting one here clears the rest so the UI and the
          // loader never disagree about which theme is pinned.
          for (int i = 0; i < program->enabled_theme_size(); ++i) {
            auto* other = program->mutable_enabled_theme(i);
            if (other->theme_name() != name) {
              other->set_pinned(false);
            }
          }
        }
        changed = true;
      }
      ImGui::SameLine();
    }

    auto theme_it = _session.mutable_theme_map()->find(name);
    const bool node_open = ImGui::TreeNode(name.c_str());

    // Inherit-parent toggle, with the pool-size consequence spelled out rather than
    // left to be discovered. Only offered for a scan theme with a parent: an explicit
    // image-list theme has no folder to inherit from, and the root theme has nothing
    // above it. Sized live from the OWN counts the loader recorded, so the arrow shows
    // what the next restart will actually produce.
    const std::string parent = theme_parent(name);
    if (!parent.empty() && _sidecar.theme_scan.count(name)) {
      ImGui::SameLine();
      const bool inherits = _sidecar.theme_inherit.count(name) != 0;
      if (inherits) {
        ImGui::PushStyleColor(ImGuiCol_Button, kPinGold);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 0.f, 0.f, 1.f));
      }
      if (ImGui::Button("inherit")) {
        if (inherits) {
          _sidecar.theme_inherit.erase(name);
        } else {
          _sidecar.theme_inherit.insert(name);
        }
      }
      if (inherits) {
        ImGui::PopStyleColor(2);
      }
      if (ImGui::IsItemHovered()) {
        // The chain is the non-obvious part: this button only reaches the grandparent
        // when the parent's own button is on too.
        const bool parent_inherits = _sidecar.theme_inherit.count(parent) != 0;
        ImGui::SetTooltip("Fold '%s' into this theme's pool.\n"
                          "Chains: this reaches what '%s' itself inherits, and '%s' is\n"
                          "currently %s.\n"
                          "Takes effect on the next load (themes are built at startup).",
                          parent.c_str(), parent.c_str(), parent.c_str(),
                          parent_inherits ? "inheriting" : "NOT inheriting");
      }
      // own -> effective, so the cost of the toggle is visible before clicking it.
      auto own_it = _sidecar.theme_own_count.find(name);
      const uint32_t own = own_it != _sidecar.theme_own_count.end() ? own_it->second : 0;
      const uint32_t effective = predicted_pool_size(name);
      ImGui::SameLine();
      if (effective > own) {
        ImGui::TextColored(kPinGold, "%u -> %u (+%u)", own, effective, effective - own);
      } else {
        ImGui::TextDisabled("%u", own);
      }
    }

    if (node_open) {
      // ONE theme model: a directory plus a blacklist. Unchecking an image writes an
      // EXCLUSION rather than rewriting a stored list, which is what keeps "a file added
      // to this folder shows up next launch" true. There is no convert-to-a-different-
      // -mode button any more -- there is no other mode to convert to.
      auto scan_it = _sidecar.theme_scan.find(name);
      if (scan_it == _sidecar.theme_scan.end()) {
        // No directory reproduces this theme (a hand-written list spanning unrelated
        // folders). Left exactly as authored -- nothing here can safely edit it.
        ImGui::TextDisabled("(hand-written list -- no folder backs this theme)");
        ImGui::TreePop();
        ImGui::PopID();
        continue;
      }
      ImGui::TextDisabled("folder: %s", scan_it->second.c_str());
      auto& excludes = _sidecar.theme_exclude[name];

      // Only the theme's OWN images get a checkbox. image_path here is the RESOLVED pool:
      // inheritance has already folded every ancestor's images into it, and an exclusion
      // is matched against what THIS theme's own scan produces. Writing one for an
      // inherited image therefore matches nothing on the next load -- the image comes
      // straight back through inheritance -- so the box silently un-ticks itself, which is
      // worse than not offering it. Tier 0 of the sidecar's layout is exactly the own
      // content and the later spans name the ancestor each inherited image really belongs
      // to, which is the theme to go and exclude it on.
      const auto tiers_it = _sidecar.theme_tiers.find(name);
      const auto& pool = theme_it->second.image_path();
      std::vector<std::string> listing;
      std::vector<std::pair<std::string, std::string>> inherited;  // {path, owning theme}
      if (tiers_it != _sidecar.theme_tiers.end()) {
        const auto& tiers = tiers_it->second;
        std::size_t offset = 0;
        for (std::size_t t = 0; t < tiers.size(); ++t) {
          for (uint32_t n = 0;
               n < tiers[t].second && offset < static_cast<std::size_t>(pool.size());
               ++n, ++offset) {
            const auto& path = pool[static_cast<int>(offset)];
            if (t == 0) {
              listing.push_back(path);
            } else {
              inherited.emplace_back(path, tiers[t].first);
            }
          }
        }
      } else {
        // No recorded layout (a session that never went through resolve_theme_inheritance,
        // e.g. a cold-start scrape): nothing was inherited, so the pool is all its own.
        listing.assign(pool.begin(), pool.end());
      }
      // Present = not excluded. Currently-excluded paths are appended so an unchecked
      // image can be re-checked in the same run -- including a stale exclusion the old
      // build wrote against an inherited path, which shows up here unticked and is cleared
      // by ticking it.
      for (const auto& p : excludes) {
        if (std::find(listing.begin(), listing.end(), p) == listing.end()) {
          listing.push_back(p);
        }
      }
      std::sort(listing.begin(), listing.end());
      if (listing.empty()) {
        ImGui::TextDisabled(inherited.empty() ? "(no images)" : "(no images of its own)");
      }
      for (const auto& path : listing) {
        bool present = std::find(excludes.begin(), excludes.end(), path) == excludes.end();
        if (ImGui::Checkbox(path.c_str(), &present)) {
          if (present) {
            excludes.erase(std::remove(excludes.begin(), excludes.end(), path), excludes.end());
          } else {
            excludes.push_back(path);
          }
        }
      }
      if (!excludes.empty()) {
        ImGui::TextDisabled("(%zu excluded -- takes effect on reload)", excludes.size());
      }
      if (!inherited.empty()) {
        // Listed, not hidden: the pool really does contain these, so leaving them out
        // entirely would make the count here disagree with the arrow on the row above.
        // Deliberately not widgets -- there is nothing this theme can do about them.
        ImGui::TextDisabled("inherited (%zu) -- exclude these on the theme that owns them:",
                            inherited.size());
        for (const auto& entry : inherited) {
          ImGui::BulletText("%s  [%s]", entry.first.c_str(), entry.second.c_str());
        }
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  // Allowed live (the user may be mid-rebalance), but validate_program rewrites an
  // all-zero theme pool to "every theme at weight 1" on the next load -- so say so
  // rather than letting a deliberate all-off silently come back on.
  if (program && !theme_pool_total) {
    ImGui::TextColored(kWarnAmber, "all weights 0 -- resets to defaults on reload");
  }

  if (changed && _on_program_change) {
    _on_program_change();
  }
}

void AppUi::draw_session_section()
{
  ImGui::Text("loaded: %s", _session_path.c_str());
  if (ImGui::Button("Save")) {
    save_session_to(_session_path);
  }
  ImGui::SetNextItemWidth(-80.f);
  ImGui::InputText("##save_as_path", _save_as_buf, sizeof(_save_as_buf));
  ImGui::SameLine();
  if (ImGui::Button("Save As")) {
    save_session_to(_save_as_buf);
  }
  if (_save_status_ttl > 0.f && !_save_status.empty()) {
    ImGui::TextColored(_save_error ? ImVec4(1.f, 0.4f, 0.4f, 1.f) : ImVec4(0.4f, 1.f, 0.4f, 1.f),
                       "%s", _save_status.c_str());
  }
}

void AppUi::save_session_to(const std::string& path)
{
  _save_status_ttl = 6.f;
  if (path.empty()) {
    _save_status = "error: empty path";
    _save_error = true;
    return;
  }
  try {
    // The sidecar overload so pattern files / scan-dir themes round-trip instead of
    // being frozen inline (session_json.cpp handles scan themes on save itself; no
    // UI special-casing beyond the restart note in the Themes section).
    // save_session_json checks its output stream and throws on any failed write
    // (unopenable path, read-only file, disk full), so a normal return really
    // means the file landed.
    save_session(_session, path, _sidecar);
    _save_status = "saved " + path;
    _save_error = false;
  } catch (const std::exception& e) {
    _save_status = std::string("error: ") + e.what();
    _save_error = true;
  }
}

void AppUi::draw_overlay_section()
{
  // Live overlay toggle. This section only reads/writes CommandRuntimeState's two
  // overlay fields; the main loop's apply seam -- the same one the
  // `overlay on|off|opacity` verbs go through -- pushes the change onto the actual
  // window, so this panel and the command channel can never disagree about the state.
  //
  // The note renders ALWAYS (before the user turns the overlay on): engaging the
  // overlay collapses this panel (the click-through window couldn't deliver it a
  // click anyway -- see main.cpp's apply seam), so the user needs to know the way
  // back BEFORE flipping the switch.
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.3f, 1.f));
  ImGui::TextWrapped(
      "Turning the overlay on makes the window click-through and closes this panel. "
      "To come back: the tray icon's Show control panel (Windows), `overlay off` or "
      "`show` over the command channel (--command_port), or Ctrl+C. Shift+F11 is the "
      "global hide-everything toggle (window hidden + paused + muted; press again to "
      "restore).");
  ImGui::PopStyleColor();

  bool on = _command_state.overlay_on;
  float opacity = _command_state.overlay_opacity;
  bool changed = ImGui::Checkbox("click-through overlay", &on);
  // Always shown; while the overlay is on, opacity changes apply live each frame.
  changed |= ImGui::SliderFloat("opacity", &opacity, 0.05f, 1.f, "%.2f");
  if (changed) {
    _command_state.overlay_on = on;
    _command_state.overlay_opacity = opacity;
  }
}

void AppUi::draw_entrainment_section(Audio& audio)
{
  bool muted = audio.Muted();
  if (ImGui::Checkbox("Mute", &muted)) {
    audio.ToggleMute();
  }
  // TODO: no volume slider. Audio only exposes a global on/off mute
  // (ToggleMute -> sf::Listener::setGlobalVolume(0/100)); per-channel volume is
  // driven by session AudioEvents / fades (audio.cpp), not a public setter. Add
  // Audio::SetMasterVolume(float) (or similar) if a continuous slider is wanted,
  // then wire it here.
  ImGui::TextDisabled("(volume slider: no setter on Audio yet)");
}

void AppUi::draw_system_section()
{
  // Edits main()'s live trance_pb::System in place and persists IMMEDIATELY to
  // system.json (save_system_config below) -- there's no separate Apply/Save step.
  // The renderer and window are constructed once at play_session startup, so
  // renderer/windowed changes only land on the next launch; the note below makes
  // that explicit so a radio click that visibly does nothing isn't read as a bug.
  ImGui::TextUnformatted("Renderer:");
  bool changed = false;
  auto renderer_radio = [&](const char* label, trance_pb::System::Renderer value) {
    bool selected = _system.renderer() == value;
    if (ImGui::RadioButton(label, selected) && !selected) {
      _system.set_renderer(value);
      changed = true;
    }
  };
  renderer_radio("Monitor", trance_pb::System::MONITOR);
  renderer_radio("SteamVR (OpenVR)", trance_pb::System::OPENVR);
  renderer_radio("OpenXR (Quest Link, any runtime)", trance_pb::System::OPENXR);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.3f, 1.f));
  ImGui::TextWrapped("Renderer and windowed mode take effect on next launch.");
  ImGui::PopStyleColor();

  bool windowed = _system.windowed();
  if (ImGui::Checkbox("windowed", &windowed)) {
    _system.set_windowed(windowed);
    changed = true;
  }

  // Eye spacing feeds the per-eye camera offset in both VR renderers; grey it out
  // elsewhere rather than hiding it, so the setting stays discoverable. mutable_ on
  // an unset EyeSpacing materializes it zeroed -- same value the renderer reads
  // through the const accessor, so no behaviour change until the user drags.
  const bool stereo = _system.renderer() == trance_pb::System::OPENVR ||
      _system.renderer() == trance_pb::System::OPENXR;
  ImGui::BeginDisabled(!stereo);
  float eye_spacing = _system.eye_spacing().eye_spacing();
  if (ImGui::SliderFloat("eye spacing", &eye_spacing, 0.f, 1.f, "%.3f")) {
    _system.mutable_eye_spacing()->set_eye_spacing(eye_spacing);
    changed = true;
  }
  ImGui::EndDisabled();
  if (!stereo) {
    ImGui::TextDisabled("(eye spacing: only used by the VR renderers)");
  }

  if (changed) {
    save_system_config();
  }
  if (_system_status_ttl > 0.f && !_system_status.empty()) {
    ImGui::TextColored(_system_error ? ImVec4(1.f, 0.4f, 0.4f, 1.f)
                                     : ImVec4(0.4f, 1.f, 0.4f, 1.f),
                       "%s", _system_status.c_str());
  }
}

void AppUi::save_system_config()
{
  _system_status_ttl = 6.f;
  try {
    // save_system_json checks its output stream and throws on any failed write
    // (same as save_session_json -- see save_session_to above), so a normal
    // return really means the file landed; the catch is the single failure path.
    save_system(_system, _system_path);
    _system_status = "saved " + _system_path;
    _system_error = false;
  } catch (const std::exception& e) {
    _system_status = std::string("error: ") + e.what();
    _system_error = true;
  }
}
