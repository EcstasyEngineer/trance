#ifndef TRANCE_SRC_TRANCE_RENDER_RENDER_H
#define TRANCE_SRC_TRANCE_RENDER_RENDER_H
#include <functional>
#include <memory>
#include <string>

// OverlayConfig + the overlay hint functions live in platform/ -- they are native
// window management, not rendering. ScreenRenderer only takes the config and calls
// the startup variant once.
#include <trance/platform/overlay_hints.h>

#pragma warning(push, 0)
#include <GL/glew.h>
#pragma warning(pop)

namespace sf
{
  class RenderWindow;
}
namespace trance_pb
{
  class System;
}

GLuint compile(const std::string& vertex_text, const std::string& fragment_text);
void init_glew();

class Renderer
{
public:
  // TODO: could factor out actual rendering to intermediate texture(s) and add multisampling?
  enum class State {
    NONE = 0,
    VR_LEFT = 1,
    VR_RIGHT = 2,
  };

  virtual ~Renderer() = default;

  sf::RenderWindow& window();
  virtual bool vr_enabled() const = 0;
  virtual bool is_openvr() const = 0;
  virtual uint32_t view_width() const = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
  virtual float eye_spacing_multiplier() const = 0;

  virtual void init() = 0;
  virtual bool update() = 0;
  virtual void render(const std::function<void(State)>& render_fn) = 0;

  // Pre-display UI hook: runs after the scene is drawn but BEFORE the buffer swap, so a
  // 2D UI (the F2 ImGui panels) composites onto the same frame it belongs to. Calling
  // display() again outside render() instead double-swaps: the UI lands on the previous
  // frame's back buffer, strobing the UI at half rate and ping-ponging the scene one
  // frame back every other swap. Only ScreenRenderer honours it (VR renders per-eye and
  // video export has no interactive window).
  void set_ui_hook(std::function<void()> hook) { _ui_hook = std::move(hook); }

protected:
  std::unique_ptr<sf::RenderWindow> _window;
  std::function<void()> _ui_hook;
};

class ScreenRenderer : public Renderer
{
public:
  ScreenRenderer(const trance_pb::System& system, const OverlayConfig& overlay = {});

  bool vr_enabled() const override;
  bool is_openvr() const override;
  uint32_t view_width() const override;
  uint32_t width() const override;
  uint32_t height() const override;
  float eye_spacing_multiplier() const override;

  void init() override;
  bool update() override;
  void render(const std::function<void(State)>& render_fn) override;
};

#endif