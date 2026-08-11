#include <trance/ui/app_ui.h>
#include <common/session.h>
#include <common/session_json.h>
#include <trance/director.h>
#include <trance/media/audio.h>
#include <trance/platform/display_info.h>
#include <trance/runtime_state.h>
#include <trance/theme_bank.h>
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/builtin_visuals.h>
#include <trance/visual/pattern_parser_v3.h>
#include <algorithm>
#include <cctype>
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

  // Longest edge of a hover thumbnail, in ImGui points. Big enough to recognize a
  // picture at a glance, small enough that the tooltip never covers the list it
  // annotates.
  const float kPreviewMaxEdge = 220.f;

  // Animation extensions, matching session.cpp's is_animation (the classifier that put
  // these files in the pool). Kept as a local copy rather than exported: this one is a
  // "can the tooltip decode it?" test, and the answer would stay no even if the
  // classifier grew a format the UI still can't scrub.
  bool preview_is_animation(const std::string& path)
  {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
      return false;
    }
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "webm" || ext == "gif";
  }
}

AppUi::AppUi(trance_pb::Session& session, const std::string& session_path,
             const std::string& root_path, SessionJsonSidecar& sidecar,
             trance_pb::System& system, const std::string& system_path,
             CommandRuntimeState& command_state, std::function<void()> on_program_change,
             std::function<trance_pb::Program*()> active_program, std::string vr_failure)
: _session{session}
, _session_path{session_path}
, _root_path{root_path}
, _sidecar{sidecar}
, _system{system}
, _system_path{system_path}
, _command_state{command_state}
, _on_program_change{std::move(on_program_change)}
, _active_program{std::move(active_program)}
, _vr_failure{std::move(vr_failure)}
{
  // Seed Export with the loaded path so "tweak the filename" is the common case.
  std::snprintf(_export_buf, sizeof(_export_buf), "%s", session_path.c_str());
}

AppUi::~AppUi()
{
  // Last-chance flush. update()'s seam covers every edit whose widget got to
  // deactivate, but the exit paths that bypass this panel entirely -- the window close
  // button, the tray's Quit -- can land with an edit still active and unwritten.
  // autosave_session() swallows its own failures (there is no panel left to show them
  // in), so this cannot throw out of a destructor.
  autosave_session();
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
  // One media decode per frame (see kPreviewCacheMax); the budget refills here.
  _preview_loaded_this_frame = false;
  if (_export_status_ttl > 0.f) {
    _export_status_ttl -= dt.asSeconds();
  }
  if (_system_status_ttl > 0.f) {
    _system_status_ttl -= dt.asSeconds();
  }
  if (_autosave_retry_in > 0.f) {
    _autosave_retry_in -= dt.asSeconds();
  }
  // Tweens are per-row and purely cosmetic; a playlist advance re-points
  // _active_program at a program whose "slow_flash" row is a different row, so an
  // in-flight animation on the old one is meaningless and gets dropped. Done here
  // rather than in a section, since a collapsed CollapsingHeader never draws to notice.
  // (The weight STASHES used to be dropped here too, which is what made an off/on
  // round-trip forget the row's weight and come back at kDefaultRowWeight: switching a
  // row off and back on across a program change -- a matter of seconds in a playlist --
  // lost the number. They are keyed by program now and survive.)
  const trance_pb::Program* current = _active_program ? _active_program() : nullptr;
  if (current != _weight_stash_program) {
    _weight_stash_program = current;
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
    // Closed mid-edit (F2, `ui off`, the overlay engaging): the widget that was
    // blocking the flush below will never deactivate now, so write here instead. This
    // branch runs every frame while the panel is closed, so it takes the backoff too.
    autosave_if_due();
    return;
  }

  // One window, top-left, with collapsing sections (the old three overlapping
  // windows folded in). FirstUseEver so the user can still move/resize it.
  ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
  // Wide enough for a full weight row -- on/pin/inherit, bar, number, share -- plus the
  // row's name after it. FirstUseEver, so an existing imgui.ini keeps the user's size.
  ImGui::SetNextWindowSize(ImVec2(500.f, 640.f), ImGuiCond_FirstUseEver);
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
  // Same treatment for a failed autosave, and for the same reason: every edit in this
  // panel is supposed to be on disk already, so "it silently isn't" is not something to
  // report in a status line inside a section that is collapsed by default. Persistent
  // until a save succeeds.
  if (!_autosave_error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
    ImGui::TextWrapped("NOT SAVING: %s", _autosave_error.c_str());
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
  if (ImGui::CollapsingHeader("Overlay")) {
    draw_overlay_section();
  }
  if (ImGui::CollapsingHeader("Audio")) {
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
    // Before the flag, not after: main.cpp tears the run down on the next poll, and an
    // edit committed this frame has not reached the seam below yet.
    autosave_session();
    _quit_requested = true;
  }
  ImGui::End();

  // The autosave seam. Deferred to here, and gated on nothing being ACTIVE, so a slider
  // drag or a half-typed pattern name is written once when the user lets go rather than
  // once per frame -- and so a single write covers a frame that dirtied several
  // sections. No-op unless something actually changed.
  if (!ImGui::IsAnyItemActive()) {
    autosave_if_due();
  }
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
  // "is a headset being fed alongside this window", not a mode: the panel, and the
  // desktop pass it draws on, exist either way.
  ImGui::Text("headset attached: %s", director.vr_enabled() ? "yes" : "no");
  ImGui::Text("visual: %s", director.status_visual_name().c_str());

  // Same content breakdown as the F1 overlay, and for the same reason: an all-gif theme
  // has 0 images and is perfectly healthy, which reads as broken without the anim count.
  auto snap = themes.debug_snapshot();
  auto theme_line = [](const char* label, const ThemeBank::DebugSnapshot::Slot& slot) {
    if (!slot.valid) {
      ImGui::Text("%s: (empty)", label);
      return;
    }
    ImGui::Text("%s: %s", label, slot.name.c_str());
    ImGui::TextDisabled("    %u/%u images loaded, %u animation(s)%s", slot.loaded, slot.total,
                        slot.animations,
                        !slot.total && slot.animations ? "  -- all-animation theme" : "");
  };
  theme_line("theme primary  ", snap.slots[1]);
  theme_line("theme alternate", snap.slots[2]);

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
                                              const char* slider_tooltip, bool pool_pinned,
                                              const std::function<void()>& after_pin)
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

  // Caller-owned buttons in the same run of toggles (the Themes section's `inherit`).
  // They belong here, with on/off and pin, rather than trailing the row's NAME: they
  // are the same kind of control, and a button that hangs off the end of a
  // variable-width name never lands in the same place twice down the column.
  if (after_pin) {
    after_pin();
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
  // Narrower than it was: the bar is now the coarse control and the box beside it is the
  // exact one, so pixels spent here come straight out of the row's NAME.
  ImGui::SetNextItemWidth(90.f);
  // Disabled only for input: the bar still redraws each frame, which is what makes
  // the animation visible.
  ImGui::BeginDisabled(animating);
  if (ImGui::SliderInt("##weight", &shown, 0, kMaxRowWeight, "%d")) {
    *weight = static_cast<uint32_t>(std::max(0, std::min(kMaxRowWeight, shown)));
  }
  // EndDisabled pushes no item of its own, so IsItem* here still refers to the slider.
  // Committed on release, not per tick: a drag would otherwise re-parse every custom
  // pattern in the program on every frame of the drag (Director::set_program).
  bool committed = ImGui::IsItemDeactivatedAfterEdit();
  if (slider_tooltip && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", slider_tooltip);
  }
  ImGui::SameLine();

  // Type the number in. The slider spans 0..100 in ~100 pixels, so the low end -- where
  // the interesting values are, since a weight only means anything against the pool --
  // is a one-pixel target: 1 is indistinguishable from off by dragging. This box is the
  // same value, exact. (ImGui's ctrl+click-to-type on the slider does the same thing,
  // but nothing on screen says so.) AutoSelectAll so a click-and-type replaces rather
  // than appends.
  ImGui::SetNextItemWidth(38.f);
  int typed = shown;
  if (ImGui::InputInt("##weight_num", &typed, 0, 0,
                      ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsDecimal)) {
    *weight = static_cast<uint32_t>(std::max(0, std::min(kMaxRowWeight, typed)));
  }
  ImGui::EndDisabled();
  committed = committed || ImGui::IsItemDeactivatedAfterEdit();
  if (committed) {
    if (*weight) {
      stash[key] = *weight;
    }
    result.weight_changed = true;
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
    ImGui::Text("Now playing: %s", director.status_visual_name().c_str());
    ImGui::Separator();
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
  // This program's off/on weight memory (see _visual_last_weight): one stash per
  // program, so a row switched off here still remembers its weight after the playlist
  // has been round the houses and come back.
  auto& visual_stash = _visual_last_weight[program];
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

  // The one fact this whole section was missing: which visual is on screen RIGHT NOW.
  // The lottery rows only say what CAN play; with a 3072-frame visual the current pick
  // is on for ~25 seconds and there was nowhere to read it. The playing row is also
  // marked in place below.
  const uint32_t playing_builtin = director.current_builtin_type();
  const std::string& playing_custom = director.current_custom_name();
  ImGui::Text("Now playing: %s", director.status_visual_name().c_str());
  ImGui::Separator();

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
                               visual_stash, kVisualSliderTooltip, pool_pinned);
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
    {
      const bool open = ImGui::TreeNode(label);
      if (playing_builtin == type) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "(playing)");
      }
      if (open) {
        draw_builtin_body(director, type, blurb);
        ImGui::TreePop();
      }
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
                               pool_total, visual_stash, kVisualSliderTooltip,
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
    {
      // A row-less built-in can still be the one playing (forced via its body button),
      // so the marker is drawn here too rather than letting the panel contradict the
      // screen.
      const bool open = ImGui::TreeNode(visual.name);
      if (playing_builtin == visual.type) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "(playing)");
      }
      if (open) {
        ImGui::TextDisabled("(not in this program's pool -- switch the row on to add it)");
        draw_builtin_body(director, visual.type, visual.blurb);
        ImGui::TreePop();
      }
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
    mark_session_dirty();
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
        visual_stash.emplace(key, pattern->random_weight());
      }
      bool pinned = pattern->pinned();
      // Empty label: the TreeNode below names the row (the Themes section's layout).
      auto row = draw_weight_row("", visual_row_key(true, 0, pattern->name()), &weight, &pinned,
                                 pool_total, visual_stash, kVisualSliderTooltip,
                                 pool_pinned);
      // Only ever written on a real user edit. The pass-through case (drawing a
      // disabled pattern, which the row shows at 0) must not clobber the weight the
      // pattern was saved with, and must not switch it on just by being drawn.
      if (row.weight_changed || weight != (pattern->enabled() ? pattern->random_weight() : 0)) {
        pattern->set_random_weight(weight);
        pattern->set_enabled(weight > 0);
        // The mid-drag values are written to the proto but never reach disk: the
        // autosave seam waits for the widget to go inactive (see update()).
        mark_session_dirty();
        // A pin on a disabled pattern is dead: rebuild_custom_patterns skips the
        // pattern entirely, so the panel would draw an active force that does nothing
        // and the autosave would persist a pin validate_program strips on the next load.
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
    if (!playing_custom.empty() && playing_custom == pattern->name()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "(playing)");
    }
    if (open) {
      ImGui::SetNextItemWidth(200.f);
      if (ImGui::InputText("##name", pattern->mutable_name())) {
        // Persisted like every other edit -- but only once the field goes inactive, so
        // a name is never written to disk half-typed (and the rename never lands as a
        // duplicate the loader would reject).
        mark_session_dirty();
        // Duplicate names are a load-time error (session_json.cpp), so the lint has to
        // catch them here rather than at save time. The WHOLE cache goes, not just this row:
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
        // Same deferral as the name field: the source reaches patterns/<slug>.pattern
        // when the box goes inactive, not on every keystroke.
        mark_session_dirty();
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
  if (pool_changed) {
    mark_session_dirty();
    if (_on_program_change) {
      _on_program_change();
    }
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

  // This row used to read as a render-rate knob, which it has never been -- the number
  // next to it is the whole point. global_fps is the CONTENT clock (how fast the cycler
  // tree ticks); frames reach the screen at whatever vsync / the presentation cap in
  // render.cpp allows, no matter what this says. So turning it down saves no GPU at all,
  // it just stretches every `Nf` in the grammar and runs the visuals slower.
  int fps = static_cast<int>(program->global_fps());
  if (ImGui::DragInt("global fps", &fps, 0.25f, 15, 240, "%d", ImGuiSliderFlags_AlwaysClamp)) {
    program->set_global_fps(static_cast<uint32_t>(std::max(15, std::min(240, fps))));
    changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Content tick rate -- NOT the render rate.\n"
                      "Frames are presented at the display's refresh either way, so\n"
                      "lowering this does not save GPU: it slows the visuals down,\n"
                      "because every duration in a pattern is a tick count (`every 64f`).\n"
                      "Ticks above the refresh are states the panel never gets to show.");
  }
  // Refresh comes from the OS, not SFML -- sf::VideoMode has no such field (display_info.h).
  const uint32_t refresh_hz = display_refresh_hz();
  if (!refresh_hz) {
    ImGui::TextDisabled("display refresh: unknown");
  } else {
    ImGui::TextDisabled("display refresh: %u Hz", refresh_hz);
    // +1 of slack before nagging. Display mode tables report an integer Hz truncated
    // from a fractional rate -- the common 59.94/59.95 Hz "60 Hz" panel comes back as
    // 59 (measured on this machine) -- so an exact `>` would put a warning next to the
    // perfectly sensible setting of 60 and complain about one tick per second. Anything
    // that is genuinely over-clocking the panel (the 120-on-60 case, 61 ticks/s) clears
    // this by a mile.
    if (program->global_fps() > refresh_hz + 1) {
      // Stated as the concrete cost rather than a bare "too high": the surplus ticks are
      // not wasted DRAW calls (the panel is presented at its own rate regardless), they
      // are pattern states that are computed and then overwritten before any frame
      // samples them.
      ImGui::SameLine();
      ImGui::TextColored(kWarnAmber, "-- %u tick/s never reach the panel",
                         program->global_fps() - refresh_hz);
    }
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

  if (changed) {
    // Both widgets here report per drag tick, which is what the live preview wants;
    // the write to disk is what the autosave seam holds back until release.
    mark_session_dirty();
    if (_on_program_change) {
      _on_program_change();
    }
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

void AppUi::draw_media_preview(const std::string& path)
{
  ++_preview_clock;

  // An animation is named, not decoded -- see kPreviewCacheMax's note. Cheap and
  // honest: the row still stops being an opaque hash.
  if (preview_is_animation(path)) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(path.c_str());
    ImGui::TextDisabled("(animation -- no preview)");
    ImGui::EndTooltip();
    return;
  }

  auto it = _preview_cache.find(path);
  if (it == _preview_cache.end()) {
    // Budget spent already this frame: say so rather than showing nothing, so a fast
    // drag down the list reads as "loading", not as "these rows are broken".
    if (_preview_loaded_this_frame) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(path.c_str());
      ImGui::TextDisabled("(loading...)");
      ImGui::EndTooltip();
      return;
    }
    _preview_loaded_this_frame = true;

    // Evict before inserting so the cap is a real ceiling on live textures.
    //
    // Eviction is safe only because it happens at most once per frame, and strictly
    // BEFORE this frame's single BeginTooltip/Image pair: ImGui::Image records the
    // texture's native GL handle rather than retaining the sf::Texture, so destroying a
    // texture that an already-submitted Image still references would queue a stale
    // handle for end-of-frame render. `_preview_loaded_this_frame` is what enforces the
    // "at most once" -- keep it if this ever grows a second preview call site.
    while (_preview_cache.size() >= kPreviewCacheMax) {
      auto oldest = _preview_cache.begin();
      for (auto e = _preview_cache.begin(); e != _preview_cache.end(); ++e) {
        if (e->second.used < oldest->second.used) {
          oldest = e;
        }
      }
      _preview_cache.erase(oldest);
    }

    PreviewEntry entry;
    // Load through sf::Image first, then upload: a failed decode leaves the texture
    // null (the negative-cache state) instead of throwing out of the render loop.
    // SFML 3's texture constructor throws on failure, loadFromImage returns false.
    sf::Image decoded;
    if (decoded.loadFromFile(_root_path + "/" + path)) {
      auto texture = std::make_unique<sf::Texture>();
      if (texture->loadFromImage(decoded)) {
        texture->setSmooth(true);
        entry.texture = std::move(texture);
      }
    }
    it = _preview_cache.emplace(path, std::move(entry)).first;
  }
  it->second.used = _preview_clock;

  ImGui::BeginTooltip();
  ImGui::TextUnformatted(path.c_str());
  if (!it->second.texture) {
    ImGui::TextDisabled("(no preview -- unreadable or unsupported)");
    ImGui::EndTooltip();
    return;
  }
  const sf::Vector2u size = it->second.texture->getSize();
  // Fit the longest edge to kPreviewMaxEdge, preserving aspect. Never upscale: a tiny
  // source blown up to 220px says nothing a 32px thumbnail didn't.
  const float longest = float(std::max(size.x, size.y));
  const float scale = longest > kPreviewMaxEdge ? kPreviewMaxEdge / longest : 1.f;
  ImGui::Image(*it->second.texture, ImVec2{float(size.x) * scale, float(size.y) * scale});
  ImGui::TextDisabled("%ux%u", size.x, size.y);
  ImGui::EndTooltip();
}

void AppUi::draw_themes_section()
{
  // ThemeBank is built once at startup and has no live-rebuild path; image_path edits
  // are saved immediately like everything else but only take effect on the next
  // launch. Weight/enable edits DO live-apply (they go through ThemeBank::set_program
  // like a playlist switch).
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
      // Sidecar edits change nothing in the running program (they are load-time
      // instructions), so they mark dirty directly rather than riding `changed`.
      mark_session_dirty();
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
      mark_session_dirty();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!inheriting);
    if (ImGui::Button("none")) {
      _sidecar.theme_inherit.clear();
      mark_session_dirty();
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

    // Inherit-parent toggle. Only offered for a scan theme with a parent: an explicit
    // image-list theme has no folder to inherit from, and the root theme has nothing
    // above it. Drawn INSIDE the weight row (after pin, before the bar) so it lines up
    // down the column instead of trailing each name at a different offset; the
    // pool-size consequence still reads after the name, where it describes the theme
    // rather than the button.
    const std::string parent = theme_parent(name);
    const bool inheritable = !parent.empty() && _sidecar.theme_scan.count(name) != 0;
    auto draw_inherit_button = [&]() {
      const bool inherits = _sidecar.theme_inherit.count(name) != 0;
      if (inherits) {
        ImGui::PushStyleColor(ImGuiCol_Button, kPinGold);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 0.f, 0.f, 1.f));
      }
      // Same 32px as on/off and pin -- it is a third state toggle in that run, and the
      // full word would push the theme name off a default-width panel. The tooltip
      // carries the meaning.
      if (ImGui::Button("inh", ImVec2(32.f, 0.f))) {
        if (inherits) {
          _sidecar.theme_inherit.erase(name);
        } else {
          _sidecar.theme_inherit.insert(name);
        }
        mark_session_dirty();
      }
      if (inherits) {
        ImGui::PopStyleColor(2);
      }
      if (ImGui::IsItemHovered()) {
        // The chain is the non-obvious part: this button only reaches the grandparent
        // when the parent's own button is on too.
        const bool parent_inherits = _sidecar.theme_inherit.count(parent) != 0;
        ImGui::SetTooltip("Inherit: fold '%s' into this theme's pool.\n"
                          "Chains: this reaches what '%s' itself inherits, and '%s' is\n"
                          "currently %s.\n"
                          "Takes effect on the next load (themes are built at startup).",
                          parent.c_str(), parent.c_str(), parent.c_str(),
                          parent_inherits ? "inheriting" : "NOT inheriting");
      }
    };

    if (program) {
      // Per-program off/on weight memory, same as the Visuals section's.
      auto& theme_stash = _theme_last_weight[program];
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
                                 theme_stash,
                                 "Rotation weight: each theme swap picks the next theme with\n"
                                 "chance weight/total across enabled themes. 0 = never picked.\n"
                                 "A PINNED theme stays resident even at weight 0 -- the weights\n"
                                 "then only choose the other of the two live slots.\n"
                                 "(The bank still only keeps its 4-slot window loaded.)",
                                 // Themes never report pool_pinned: a pinned theme holds one
                                 // of the two slots, it does not win the lottery for the other.
                                 false, inheritable ? std::function<void()>{draw_inherit_button}
                                                    : std::function<void()>{});
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

    // The inherit toggle's consequence, after the name where it reads as a property of
    // the theme: own count -> what the pool becomes with inheritance folded in. Sized
    // live from the OWN counts the loader recorded, so the arrow shows what the next
    // restart will actually produce -- the cost of the button is visible before it is
    // clicked. (The button itself is up in the weight row, see draw_inherit_button.)
    if (inheritable) {
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
          mark_session_dirty();
        }
        // Hover the row to see what the file actually IS (#53) -- these names are
        // hashes and timestamps on any real library, so keep/drop is otherwise a guess.
        if (ImGui::IsItemHovered()) {
          draw_media_preview(path);
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
          // Previewable too: deciding to go and exclude one on its owning theme needs
          // the same "what is this?" answer the checkboxes above give.
          if (ImGui::IsItemHovered()) {
            draw_media_preview(entry.first);
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

  if (changed) {
    mark_session_dirty();
    if (_on_program_change) {
      _on_program_change();
    }
  }
}

void AppUi::draw_session_file_controls()
{
  ImGui::TextUnformatted("Session file:");
  ImGui::TextDisabled("%s", _session_path.c_str());
  // Stated, not implied. There is no Save button here any more and the absence of one
  // is only reassuring if the panel says why.
  ImGui::TextDisabled("(edits above are saved to it as you make them)");

  ImGui::Spacing();
  ImGui::TextUnformatted("Export a copy:");
  ImGui::SetNextItemWidth(-80.f);
  ImGui::InputText("##export_path", _export_buf, sizeof(_export_buf));
  ImGui::SameLine();
  if (ImGui::Button("Export")) {
    export_session_to(_export_buf);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Write the session as it stands to another path, patterns and\n"
                      "all. The live file above keeps playing and keeps saving --\n"
                      "this is a copy, not a switch.\n"
                      "(For a copy with the MEDIA bundled in, run\n"
                      "trance --export_archive=<file>.)");
  }
  if (_export_status_ttl > 0.f && !_export_status.empty()) {
    ImGui::TextColored(_export_error ? ImVec4(1.f, 0.4f, 0.4f, 1.f) : ImVec4(0.4f, 1.f, 0.4f, 1.f),
                       "%s", _export_status.c_str());
  }
}

void AppUi::autosave_if_due()
{
  // Everything autosave_session() does, minus the retry storm a persistently failing
  // write would otherwise produce from the two per-frame call sites.
  if (_autosave_retry_in <= 0.f) {
    autosave_session();
  }
}

void AppUi::autosave_session()
{
  if (!_session_dirty) {
    return;
  }
  try {
    // The sidecar overload so pattern files / scan-dir themes round-trip instead of
    // being frozen inline (session_json.cpp handles scan themes on save itself; no
    // UI special-casing beyond the restart note in the Themes section).
    // save_session_json writes through a temp file and renames, and throws on any
    // failed write (unopenable path, read-only file, disk full), so a normal return
    // really means the file landed -- whole, not half.
    save_session(_session, _session_path, _sidecar);
    // Cleared only on success: a failed write leaves the edit pending, so the next
    // flush retries it rather than dropping it on the floor.
    _session_dirty = false;
    _autosave_error.clear();
  } catch (const std::exception& e) {
    _autosave_error = std::string(e.what()) + " (edits are live but NOT on disk)";
    // Still dirty, so the edit is not lost -- but back off before trying again.
    _autosave_retry_in = 5.f;
  }
}

void AppUi::export_session_to(const std::string& path)
{
  _export_status_ttl = 6.f;
  if (path.empty()) {
    _export_status = "error: empty path";
    _export_error = true;
    return;
  }
  try {
    // Same call the autosave makes, at a different path. The sidecar's pattern-file
    // entries are ROOT-relative, so the copy's patterns/ sidecars land next to the
    // copy and the live session's own paths are untouched.
    save_session(_session, path, _sidecar);
    _export_status = "exported " + path;
    _export_error = false;
  } catch (const std::exception& e) {
    _export_status = std::string("error: ") + e.what();
    _export_error = true;
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
  // One mute, one scope: the same Audio::ToggleMute the M key and the Shift+F11 hide
  // path drive, over ALL audio (bed + music channels), not just the bed. Reading
  // Muted() fresh each frame is what keeps the checkbox and the M key aligned.
  bool muted = audio.Muted();
  if (ImGui::Checkbox("mute all audio (M)", &muted)) {
    audio.ToggleMute();
  }
  ImGui::Separator();

  trance_pb::Program* program = _active_program();
  if (!program) {
    ImGui::TextDisabled("(bed editing needs a session program; the built-in default is playing)");
    return;
  }

  // Absent/empty entrainment block = no bed (the JSON contract, session_json.cpp).
  // "Enable bed" is what keeps that contract from locking new users out: it writes
  // the stock default bed into the program, no hand-edited JSON required.
  const bool bed_on = program->entrainment().layer_size() > 0;
  if (!bed_on) {
    ImGui::TextUnformatted("bed: off");
    ImGui::SameLine();
    if (ImGui::Button("Enable bed")) {
      trance_pb::Entrainment restored;
      if (_bed_stash_program == program && restored.ParseFromString(_bed_stash) &&
          restored.layer_size() > 0) {
        // Same-run round-trip of the bed the Disable click cleared.
        *program->mutable_entrainment() = restored;
      } else {
        *program->mutable_entrainment() = default_entrainment_bed();
      }
      mark_session_dirty();
      _on_program_change();
    }
    return;
  }

  auto* entrainment = program->mutable_entrainment();
  if (ImGui::Button("Disable bed")) {
    _bed_stash = entrainment->SerializeAsString();
    _bed_stash_program = program;
    program->clear_entrainment();
    mark_session_dirty();
    _on_program_change();
    return;
  }

  // Sliders write the proto per drag tick (no side effects) and COMMIT on release:
  // the commit reconfigures the live stream (stop, recalibrate, play) and re-pushes
  // the program for the `locked` beat clock, neither of which belongs on every tick.
  bool commit = false;

  // Display the effective master (0 in the proto means the -28 default); the first
  // edit writes an explicit value, so UI-touched beds never rely on the sentinel.
  // The range stops at -6: with RMS normalisation to master, anything hotter is a
  // hazard, and it keeps 0 dB (the sentinel) unreachable from this slider.
  float master = entrainment->master_db() != 0.f ? entrainment->master_db() : -28.f;
  if (ImGui::SliderFloat("master dB", &master, -60.f, -6.f, "%.1f dB")) {
    entrainment->set_master_db(master);
  }
  commit |= ImGui::IsItemDeactivatedAfterEdit();

  int remove_index = -1;
  for (int i = 0; i < entrainment->layer_size(); ++i) {
    auto* layer = entrainment->mutable_layer(i);
    ImGui::PushID(i);
    ImGui::Separator();
    ImGui::Text("layer %d", i);
    ImGui::SameLine();
    if (ImGui::SmallButton("remove")) {
      remove_index = i;
    }
    float center = layer->center_hz();
    if (ImGui::SliderFloat("carrier Hz", &center, 20.f, 1000.f, "%.0f Hz",
                           ImGuiSliderFlags_Logarithmic)) {
      layer->set_center_hz(center);
    }
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    float binaural = layer->binaural_hz();
    if (ImGui::SliderFloat("binaural Hz", &binaural, 0.f, 40.f,
                           binaural > 0.f ? "%.2f Hz" : "off")) {
      layer->set_binaural_hz(binaural);
    }
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    float pulse = layer->pulse_hz();
    if (ImGui::SliderFloat("pulse Hz", &pulse, 0.f, 40.f,
                           pulse > 0.f ? "%.2f Hz" : "continuous")) {
      layer->set_pulse_hz(pulse);
    }
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    // "mix", not "level": this IS dB mathematically (10^(db/20) in the synth), but
    // the whole bed is RMS-normalised to master afterwards, so per-layer dB only
    // sets the RELATIVE balance between layers -- it cannot make the bed louder.
    // dB stays the right unit for a mix control (hearing is log; equal slider
    // steps sound like equal steps, which a linear ratio slider would not give).
    float level = layer->amplitude_db();
    if (ImGui::SliderFloat("mix dB", &level, -24.f, 0.f, "%.1f dB")) {
      layer->set_amplitude_db(level);
    }
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
  }
  if (remove_index >= 0) {
    entrainment->mutable_layer()->DeleteSubrange(remove_index, 1);
    commit = true;
  }

  ImGui::Separator();
  if (ImGui::Button("+ add layer")) {
    auto* layer = entrainment->add_layer();
    layer->set_center_hz(200.f);
    layer->set_binaural_hz(3.f);
    layer->set_amplitude_db(-6.f);
    commit = true;
  }
  ImGui::TextDisabled("(mix dB balances layers against each other; overall loudness is\n"
                      "master dB. pulse drives `every locked`/`beats` pattern timing;\n"
                      "edits apply live and are saved as you make them)");

  if (commit) {
    mark_session_dirty();
    _on_program_change();
  }
}

void AppUi::draw_system_section()
{
  // Edits main()'s live trance_pb::System in place and persists IMMEDIATELY to
  // system.json (save_system_config below) -- there's no separate Apply/Save step.
  // This was the first section to work that way and is now the model for all of them
  // (see the persistence model in app_ui.h).
  // There are no renderer radios: XR output is automatic and unconfigurable
  // (docs/spec-xr-unified.md D2). The window is still constructed once at play_session
  // startup, so `windowed` only lands on the next launch; the note below makes that
  // explicit so a click that visibly does nothing isn't read as a bug.
  bool changed = false;
  bool windowed = _system.windowed();
  if (ImGui::Checkbox("windowed", &windowed)) {
    _system.set_windowed(windowed);
    changed = true;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.3f, 1.f));
  ImGui::TextWrapped("Windowed mode takes effect on next launch.");
  ImGui::PopStyleColor();

  // Eye spacing feeds the per-eye camera offset of the XR output. Always enabled: there
  // is no longer a mode it doesn't apply to -- a headset can attach to any run, and the
  // value is read live when it does. mutable_ on an unset EyeSpacing materializes it
  // zeroed -- same value the renderer reads through the const accessor, so no behaviour
  // change until the user drags.
  float eye_spacing = _system.eye_spacing().eye_spacing();
  if (ImGui::SliderFloat("eye spacing", &eye_spacing, 0.f, 1.f, "%.3f")) {
    _system.mutable_eye_spacing()->set_eye_spacing(eye_spacing);
    changed = true;
  }
  ImGui::TextDisabled("(eye spacing: only used by the headset output)");

  if (changed) {
    save_system_config();
  }
  if (_system_status_ttl > 0.f && !_system_status.empty()) {
    ImGui::TextColored(_system_error ? ImVec4(1.f, 0.4f, 0.4f, 1.f)
                                     : ImVec4(0.4f, 1.f, 0.4f, 1.f),
                       "%s", _system_status.c_str());
  }

  // The session file lives here, at the very bottom, rather than in a section of its
  // own: with the autosave there is no Save button left for such a section to hold, and
  // what remains -- which file is live, and Export -- is the same kind of thing as the
  // rest of System (config that persists on the click, not content).
  ImGui::Separator();
  draw_session_file_controls();
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
