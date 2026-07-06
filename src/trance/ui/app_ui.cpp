#include <trance/ui/app_ui.h>
#include <common/session.h>
#include <common/session_json.h>
#include <trance/director.h>
#include <trance/media/audio.h>
#include <trance/theme_bank.h>
#include <trance/visual/builtin_visuals.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <common/trance.pb.h>
#include <imgui.h>
#include <imgui-SFML.h>
#include <SFML/Graphics.hpp>
#pragma warning(pop)

AppUi::AppUi(trance_pb::Session& session, const std::string& session_path,
             SessionJsonSidecar& sidecar, std::function<void()> on_program_change,
             std::function<trance_pb::Program*()> active_program,
             std::function<std::pair<bool, float>()> get_overlay,
             std::function<void(bool, float)> set_overlay)
: _session{session}
, _session_path{session_path}
, _sidecar{sidecar}
, _on_program_change{std::move(on_program_change)}
, _active_program{std::move(active_program)}
, _get_overlay{std::move(get_overlay)}
, _set_overlay{std::move(set_overlay)}
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

  if (!_visible) {
    return;
  }

  // One window, top-left, with collapsing sections (the old three overlapping
  // windows folded in). FirstUseEver so the user can still move/resize it.
  ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420.f, 640.f), ImGuiCond_FirstUseEver);
  ImGui::Begin("trance");
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

void AppUi::draw_visuals_section(Director& director)
{
  ImGui::TextUnformatted("Built-ins (click to force now):");
  for (const auto& visual : builtin_visuals()) {
    if (ImGui::Button(visual.name)) {
      director.force_builtin_visual(visual.type);
    }
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Custom patterns (this program; click to force now):");
  const auto& custom = director.program().custom_visual_pattern();
  if (custom.empty()) {
    ImGui::TextDisabled("(none in this program)");
  }
  for (const auto& pattern : custom) {
    if (!pattern.enabled()) {
      continue;
    }
    ImGui::PushID(pattern.name().c_str());
    if (ImGui::Button(pattern.name().c_str())) {
      // Same path force_pattern_from_source always uses (director.cpp): on a parse
      // failure it returns the parser diagnostic and leaves the current visual
      // untouched. Surfaced inline rather than only to stderr since this is an
      // interactive action.
      _last_pattern_error = director.force_pattern_from_source(pattern.source_text(), pattern.name());
    }
    ImGui::PopID();
  }
  if (!_last_pattern_error.empty()) {
    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "parse error: %s", _last_pattern_error.c_str());
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
  ImGui::TextUnformatted("Visual weights:");
  for (int i = 0; i < program->visual_type_size(); ++i) {
    auto* config = program->mutable_visual_type(i);
    const char* label = nullptr;
    for (const auto& visual : builtin_visuals()) {
      if (visual.type == static_cast<uint32_t>(config->type())) {
        label = visual.name;
        break;
      }
    }
    char fallback[32];
    if (!label) {
      std::snprintf(fallback, sizeof(fallback), "type %d", static_cast<int>(config->type()));
      label = fallback;
    }
    int weight = static_cast<int>(config->random_weight());
    ImGui::PushID(i);
    if (ImGui::SliderInt(label, &weight, 0, 10)) {
      config->set_random_weight(static_cast<uint32_t>(weight));
      changed = true;
    }
    ImGui::PopID();
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
      bool enabled = entry && entry->random_weight() > 0;
      if (ImGui::Checkbox("##enabled", &enabled)) {
        if (enabled) {
          auto it = _theme_last_weight.find(name);
          ensure_entry()->set_random_weight(it != _theme_last_weight.end() ? it->second : 1);
        } else if (entry) {
          _theme_last_weight[name] = entry->random_weight();
          entry->set_random_weight(0);
        }
        changed = true;
      }
      ImGui::SameLine();
      int weight = entry ? static_cast<int>(entry->random_weight()) : 0;
      ImGui::SetNextItemWidth(120.f);
      // Label the value INSIDE the bar -- a bare number here reads as "number of
      // themes" or similar; it's the theme's rotation-lottery weight.
      if (ImGui::SliderInt("##weight", &weight, 0, 10, "weight %d")) {
        ensure_entry()->set_random_weight(static_cast<uint32_t>(weight));
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Rotation weight: each theme swap picks the next theme with\n"
                          "chance weight/total across enabled themes. 0 = never picked.\n"
                          "(The bank still only keeps its 4-slot window loaded.)");
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
    save_session(_session, path, _sidecar);
    // save_session_json doesn't check its output stream -- verify the file landed
    // so an unwritable path reports as an error instead of a silent "saved".
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      _save_status = "error: couldn't write " + path;
      _save_error = true;
    } else {
      _save_status = "saved " + path;
      _save_error = false;
    }
  } catch (const std::exception& e) {
    _save_status = std::string("error: ") + e.what();
    _save_error = true;
  }
}

void AppUi::draw_overlay_section()
{
  // Live overlay toggle. This section only reads/writes main.cpp's
  // CommandRuntimeState (via the constructor callbacks); the main loop's apply seam
  // -- the same one the `overlay on|off|opacity` verbs go through -- pushes the
  // change onto the actual window, so this panel and the command channel can never
  // disagree about the state.
  //
  // The note renders ALWAYS (before the user turns the overlay on): engaging the
  // overlay collapses this panel (the click-through window couldn't deliver it a
  // click anyway -- see main.cpp's apply seam), so the user needs to know the way
  // back BEFORE flipping the switch.
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.3f, 1.f));
  ImGui::TextWrapped(
      "Turning the overlay on makes the window click-through and closes this panel. "
      "To come back: Shift+F11 (global safety hotkey -- overlay off + pause; press "
      "again to quit), the tray icon (Windows), `overlay off` over the command "
      "channel (--command_port), or Ctrl+C.");
  ImGui::PopStyleColor();

  auto [on, opacity] = _get_overlay();
  bool changed = ImGui::Checkbox("click-through overlay", &on);
  // Always shown; while the overlay is on, opacity changes apply live each frame.
  changed |= ImGui::SliderFloat("opacity", &opacity, 0.05f, 1.f, "%.2f");
  if (changed) {
    _set_overlay(on, opacity);
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
