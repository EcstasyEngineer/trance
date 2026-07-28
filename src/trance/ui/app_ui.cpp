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

void AppUi::update(sf::RenderWindow& window, sf::Time dt, Director& director, Audio* audio,
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

void AppUi::draw_status_section(Director& director, Audio* audio, const ThemeBank& themes)
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
               (audio && audio->Muted()) ? "  [MUTED]" : "");
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

void AppUi::draw_visuals_section(Director& director)
{
  ImGui::TextUnformatted("Built-ins (click to force now):");
  for (const auto& visual : builtin_visuals()) {
    ImGui::PushID(static_cast<int>(visual.type));
    if (ImGui::Button(visual.name)) {
      director.force_builtin_visual(visual.type);
    }
    ImGui::SameLine();
    // Built-in sources are compile-time constants (builtin_patterns_v3.cpp) -- shown
    // read-only, as the modding-language reference for writing a custom pattern. Copy
    // is the intended path to editing one: paste into a new custom pattern below.
    if (ImGui::TreeNode("source")) {
      // Returned by value; ReadOnly means InputTextMultiline never writes through the
      // pointer, so handing it this frame-local buffer is safe.
      std::string source = builtin::pattern_source_v3(visual.type);
      if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(source.c_str());
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", visual.blurb);
      ImGui::InputTextMultiline("##builtin_source", &source, ImVec2(-FLT_MIN, 180.f),
                                ImGuiInputTextFlags_ReadOnly);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Custom patterns (this program):");
  // Editing needs the MUTABLE active program. When the playlist resolves to the
  // built-in default (no program_map entry) there is nothing in the session to edit,
  // so fall back to the read-only force-now list off Director's const view.
  trance_pb::Program* program = _active_program ? _active_program() : nullptr;
  if (!program) {
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

  // Shared with the Program section's built-in rows: one denominator for the whole
  // visual pool. Sampled once for the frame so every row's percent is against the
  // same total even while one of them is being dragged.
  const uint64_t pool_total = visual_pool_total(*program);
  // A pinned visual owns the pool: change_visual returns it every time and never runs
  // the lottery, so the rows show 100%/0% rather than a share of a draw that no
  // longer happens. Sampled with pool_total, for the same stability reason.
  const bool pool_pinned = any_visual_pinned(*program);
  bool pool_changed = false;

  // Index of a row the user asked to remove, applied after the loop: erasing from the
  // repeated field mid-iteration would invalidate the row we are still drawing.
  int remove_index = -1;
  for (int i = 0; i < program->custom_visual_pattern_size(); ++i) {
    auto* pattern = program->mutable_custom_visual_pattern(i);
    // By index, not by name: the name is editable, and an ID that changes mid-edit
    // would tear down the InputText that is being typed into.
    ImGui::PushID(i);
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

    // Weight/pin row, same widget and the same pool denominator the built-ins use in
    // the Program section -- change_visual runs one lottery over both (director.cpp).
    // `enabled` (the proto's own off switch, previously not editable here) is kept in
    // step with the weight: rebuild_custom_patterns skips a disabled pattern outright,
    // which is the truest "off", and a 0-weight row would never be picked anyway.
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
      // Empty label: the editable name InputText a line above already names the row.
      auto row = draw_weight_row("", visual_row_key(true, 0, pattern->name()), &weight, &pinned,
                                 pool_total, _visual_last_weight,
                                 "Selection weight, shared with the built-in visuals in the\n"
                                 "Program section (one combined lottery). 0 = never picked.\n"
                                 "A PINNED visual is forced: the lottery is skipped entirely.",
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

    if (ImGui::InputTextMultiline("##source", pattern->mutable_source_text(),
                                  ImVec2(-FLT_MIN, 160.f))) {
      _pattern_lint.erase(i);
    }

    // Live lint: patternv3::parse is pure and cheap enough to run on edit, but not
    // per frame for every row -- cache the diagnostic and only re-parse when the row
    // changed (or was never linted). Parsed with locked_period_frames 0: the beat
    // period is Director's to supply, and a `locked` length would report a spurious
    // error here, so treat that as the one case the real compile decides.
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
    if (lint->second.empty()) {
      ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "OK");
    } else {
      ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", lint->second.c_str());
    }
    ImGui::Separator();
    ImGui::PopID();
  }

  // Mirrors draw_program_section's warning: the reload rescue keys off the BUILT-IN
  // weights (a custom can fail to parse and vanish from the lottery, so it is not
  // evidence of a playable pool -- see validate_program), not off pool_total.
  uint64_t builtin_total = 0;
  bool builtin_pinned = false;
  for (const auto& type : program->visual_type()) {
    builtin_total += type.random_weight();
    builtin_pinned = builtin_pinned || type.pinned();
  }
  if (!builtin_total && !builtin_pinned) {
    ImGui::TextColored(kWarnAmber,
                       "all built-in visual weights 0 -- resets to defaults on reload");
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

  ImGui::Separator();
  ImGui::TextUnformatted("Built-in visual weights:");
  ImGui::TextDisabled("(share is against ALL visuals -- custom patterns included)");
  // One denominator for built-ins and customs alike: change_visual runs a single
  // lottery over both (director.cpp), so a built-in's share depends on the custom
  // patterns edited over in the Visuals section too. A pin overrides the lottery
  // outright, so the rows show 100%/0% instead (see draw_weight_row).
  const uint64_t pool_total = visual_pool_total(*program);
  const bool pool_pinned = any_visual_pinned(*program);
  for (int i = 0; i < program->visual_type_size(); ++i) {
    auto* config = program->mutable_visual_type(i);
    const int type = static_cast<int>(config->type());
    const char* label = nullptr;
    for (const auto& visual : builtin_visuals()) {
      if (visual.type == static_cast<uint32_t>(type)) {
        label = visual.name;
        break;
      }
    }
    char fallback[32];
    if (!label) {
      std::snprintf(fallback, sizeof(fallback), "type %d", type);
      label = fallback;
    }
    // Nothing forbids two visual_type entries with the SAME type, and their labels are
    // identical -- scope by row index so duplicates get distinct ImGui IDs instead of
    // one row swallowing the other's clicks. (The row KEY is index-derived too, for
    // the same reason; see visual_row_key.)
    ImGui::PushID(i);
    // Written through live so the slider tracks the drag and the percent label stays
    // truthful; `changed` (the on_program_change trigger) only fires on release.
    uint32_t weight = config->random_weight();
    bool pinned = config->pinned();
    auto row = draw_weight_row(label, visual_row_key(false, i, {}), &weight, &pinned, pool_total,
                               _visual_last_weight,
                               "Selection weight, shared with this program's custom patterns\n"
                               "(one combined lottery). 0 = never picked.\n"
                               "A PINNED visual is forced: the lottery is skipped entirely.",
                               pool_pinned);
    config->set_random_weight(weight);
    if (row.weight_changed) {
      changed = true;
    }
    if (row.pin_changed) {
      // Set before the sweep so the sweep can leave this one alone; clearing is
      // unconditional the other way (unpinning just leaves the pool unpinned).
      config->set_pinned(pinned);
      if (pinned) {
        clear_other_visual_pins(*program, false, i, {});
      }
      changed = true;
    }
    ImGui::PopID();
  }
  // The rescue validate_program does on reload is BUILT-IN weights only, and a pinned
  // built-in suppresses it -- so warn on exactly that condition, not on the combined
  // pool_total the percentages use.
  uint64_t builtin_total = 0;
  bool builtin_pinned = false;
  for (const auto& type : program->visual_type()) {
    builtin_total += type.random_weight();
    builtin_pinned = builtin_pinned || type.pinned();
  }
  if (!builtin_total && !builtin_pinned) {
    ImGui::TextColored(kWarnAmber, "all built-in weights 0 -- resets to defaults on reload");
  }

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
    if (ImGui::TreeNode(name.c_str())) {
      // Scan themes: save_theme (session_json.cpp) deliberately omits their media
      // lists -- the scan directory is re-expanded on every load -- so image edits
      // here would silently vanish on Save/restart. Offer the
      // honest path instead: converting drops the theme's scan sidecar entry, making
      // the current expansion an explicit (editable, persisted) image list.
      if (_sidecar.theme_scan.count(name)) {
        ImGui::TextDisabled("(scan theme -- images follow the scanned directory)");
        if (ImGui::Button("convert to explicit image list")) {
          _sidecar.theme_scan.erase(name);
        }
        ImGui::TreePop();
        ImGui::PopID();
        continue;
      }
      // Image multiselect: checked == present in the theme's image_path. Unchecked
      // paths stay in the per-run ever-seen cache so they can be re-checked (a
      // re-check appends, so on-disk ordering may change after a save -- harmless,
      // selection is random anyway).
      auto& seen = _theme_seen_images[name];
      for (const auto& path : theme_it->second.image_path()) {
        if (std::find(seen.begin(), seen.end(), path) == seen.end()) {
          seen.push_back(path);
        }
      }
      if (seen.empty()) {
        ImGui::TextDisabled("(no images)");
      }
      auto* image_paths = theme_it->second.mutable_image_path();
      for (const auto& path : seen) {
        bool present =
            std::find(image_paths->begin(), image_paths->end(), path) != image_paths->end();
        if (ImGui::Checkbox(path.c_str(), &present)) {
          if (present) {
            theme_it->second.add_image_path(path);
          } else {
            auto it = std::find(image_paths->begin(), image_paths->end(), path);
            if (it != image_paths->end()) {
              image_paths->erase(it);
            }
          }
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

void AppUi::draw_entrainment_section(Audio* audio)
{
  if (audio) {
    bool muted = audio->Muted();
    if (ImGui::Checkbox("Mute", &muted)) {
      audio->ToggleMute();
    }
    // TODO: no volume slider. Audio only exposes a global on/off mute
    // (ToggleMute -> sf::Listener::setGlobalVolume(0/100)); per-channel volume is
    // driven by session AudioEvents / fades (audio.cpp), not a public setter. Add
    // Audio::SetMasterVolume(float) (or similar) if a continuous slider is wanted,
    // then wire it here.
    ImGui::TextDisabled("(volume slider: no setter on Audio yet)");
  } else {
    // Null in export/video-render mode (see Director::set_audio's doc comment).
    ImGui::TextDisabled("(no live audio -- export mode)");
  }
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
