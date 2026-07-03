#include <trance/ui/app_ui.h>
#include <trance/director.h>
#include <trance/media/audio.h>
#include <trance/theme_bank.h>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <common/trance.pb.h>
#include <imgui.h>
#include <imgui-SFML.h>
#include <SFML/Graphics.hpp>
#pragma warning(pop)

namespace
{
  // Program::VisualType value -> v3 built-in name. Mirrors main.cpp's
  // visual_name_table() (the --visual CLI table); kept local rather than shared
  // because main.cpp's table is deliberately private to that TU and this is a
  // different consumer (click-to-force in the UI vs. a startup flag).
  const std::vector<std::pair<uint32_t, std::string>>& builtin_visual_table()
  {
    static const std::vector<std::pair<uint32_t, std::string>> table = {
        {1, "accelerate"},
        {2, "slow_flash"},
        {3, "sub_text"},
        {4, "flash_text"},
        {5, "simple"},
        {6, "super_parallel"},
        {7, "animation"},
        {8, "super_fast"},
    };
    return table;
  }
}

AppUi::~AppUi()
{
  if (_initialized) {
    ImGui::SFML::Shutdown();
  }
}

bool AppUi::available(bool overlay_enabled)
{
  // See the class comment in app_ui.h: overlay mode's window has an empty input
  // shape (render.cpp's apply_x11_overlay_hints) and is click-through by design, so
  // it structurally cannot receive the mouse/keyboard events an interactive UI needs.
  return !overlay_enabled;
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

  // Hover-reveal corner icon (issue #24 item 1): a small always-on window sits in
  // the bottom-right corner; hovering it reveals the full panel set below. This
  // keeps the UI out of the way of the visuals when not in active use, without
  // needing persisted state (NONE this wave -- see app_ui.h).
  const auto display_size = ImGui::GetIO().DisplaySize;
  const float icon_size = 28.f;
  const float margin = 10.f;
  ImGui::SetNextWindowPos(
      ImVec2(display_size.x - icon_size - margin, display_size.y - icon_size - margin));
  ImGui::SetNextWindowSize(ImVec2(icon_size, icon_size));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 4.f));
  ImGui::Begin("##ui_icon", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoBackground);
  ImGui::TextUnformatted(_visible ? "x" : "u");
  const bool icon_hovered = ImGui::IsWindowHovered();
  ImGui::End();
  ImGui::PopStyleVar();

  if (!_visible && !icon_hovered) {
    return;
  }

  draw_entrainment_panel(audio);
  draw_visuals_panel(director);
  draw_status_panel(director, audio, themes);
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

void AppUi::draw_entrainment_panel(Audio* audio)
{
  ImGui::Begin("Entrainment");
  if (audio) {
    bool muted = audio->Muted();
    if (ImGui::Checkbox("Mute", &muted)) {
      audio->ToggleMute();
    }
    // TODO(handoff): no volume slider. Audio only exposes a global on/off mute
    // (ToggleMute -> sf::Listener::setGlobalVolume(0/100)); per-channel volume is
    // driven by session AudioEvents / fades (audio.cpp), not a public setter. Add
    // Audio::SetMasterVolume(float) (or similar) if a continuous slider is wanted,
    // then wire it here -- out of scope for this skeleton (audio.{h,cpp} isn't an
    // owned file this wave).
    ImGui::TextDisabled("(volume slider: no setter on Audio yet, see handoff)");
  } else {
    // Null in export/video-render mode (see Director::set_audio's doc comment).
    ImGui::TextDisabled("(no live audio -- export mode)");
  }
  ImGui::End();
}

void AppUi::draw_visuals_panel(Director& director)
{
  ImGui::Begin("Visuals");
  ImGui::TextUnformatted("Built-ins (click to force now):");
  for (const auto& entry : builtin_visual_table()) {
    if (ImGui::Button(entry.second.c_str())) {
      director.force_builtin_visual(entry.first);
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
  ImGui::End();
}

void AppUi::draw_status_panel(Director& director, Audio* audio, const ThemeBank& themes)
{
  ImGui::Begin("Status");

  // Reuses the same accessors draw_debug_overlay() (director.cpp, F1) reads --
  // no new Director surface added for this panel. ThemeBank is passed in directly
  // from main.cpp's play_session() (which already owns it), rather than adding a
  // new Director accessor for it.
  const auto& program = director.program();
  ImGui::Text("global fps (config): %u", program.global_fps());
  ImGui::Text("vr enabled: %s", director.vr_enabled() ? "yes" : "no");
  // TODO(handoff): "current visual name" (F1's overlay prints it via the private
  // _last_visual_selection/_custom_visual_name) has no public Director accessor.
  // Adding one is a one-line getter, but director.h/.cpp aren't owned files this
  // wave -- leaving it out rather than editing outside scope.

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

  ImGui::End();
}
