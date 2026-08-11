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

// glReadPixels the window's currently-bound back buffer (row-flipped for sf::Image)
// and write it to `path`. Call from a pre-display hook so it sees the fully composited
// frame -- works even when the physical display is locked or there's no compositor to
// grab from. Logs and returns false on write failure.
bool save_window_screenshot(sf::RenderWindow& window, const std::string& path);

class Renderer
{
public:
  // TODO: could factor out actual rendering to intermediate texture(s) and add multisampling?
  // NONE is the flat single-pass case; VR_LEFT/VR_RIGHT are the two passes of a stereo
  // frame, drawn into the OpenXR backend's two per-eye swapchains -- there is no mono VR
  // pass. (There was a VR_MONO state for the OpenXR path's original single-quad
  // implementation; b86c476 gave it per-eye quads and nothing has emitted VR_MONO since.)
  enum class State {
    NONE = 0,
    VR_LEFT = 1,
    VR_RIGHT = 2,
  };

  virtual ~Renderer() = default;

  sf::RenderWindow& window();
  virtual bool vr_enabled() const = 0;
  virtual uint32_t view_width() const = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
  virtual float eye_spacing_multiplier() const = 0;

  virtual void init() = 0;
  virtual bool update() = 0;
  virtual void render(const std::function<void(State)>& render_fn) = 0;

  // Frame-loop keep-alive for any iteration where the main loop drew nothing -- paused,
  // hidden, or simply between visual frames. Returns true if the renderer performed its
  // own frame pacing (so the caller must not add an anti-spin sleep); the default no-op
  // returns false. Only the XR backend overrides it.
  //
  // WHY it must run even when not paused: a VR frame loop is a handshake with the
  // runtime, not a consequence of having something new to draw. A running OpenXR session
  // REQUIRES continuous xrWaitFrame/xrBeginFrame/xrEndFrame (the spec asks applications
  // to keep the loop running "to maintain synchronisation", calling xrEndFrame with no
  // layers if need be). Stalling it -- which is what happens if this is gated on a visual
  // frame being due at global_fps -- makes the runtime flag the app unresponsive and
  // drops the headset to the grey void.
  //
  // `blank` distinguishes the two reasons for having nothing to draw, and the distinction
  // is load-bearing:
  //   true  (paused/hidden) -- the content should stop being visible. The submission
  //         becomes layerCount=0: the handshake stays alive AND the content goes away, so
  //         `hide` genuinely vanishes in the headset instead of freezing there.
  //   false (merely between visual frames) -- the content must stay exactly as it is.
  //         Submitting layerless frames here would blank the view on every gap between
  //         visual frames, strobing the headset at (runtime rate - global_fps).
  virtual bool render_idle(bool blank)
  {
    (void) blank;
    return false;
  }

  // Pre-display UI hook: runs after the scene is drawn but BEFORE the buffer swap, so a
  // 2D UI (the F2 ImGui panels) composites onto the same frame it belongs to. Calling
  // display() again outside render() instead double-swaps: the UI lands on the previous
  // frame's back buffer, strobing the UI at half rate and ping-ponging the scene one
  // frame back every other swap. Only ScreenRenderer honours it (VR renders per-eye,
  // with no single flat pass to composite onto).
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
  uint32_t view_width() const override;
  uint32_t width() const override;
  uint32_t height() const override;
  float eye_spacing_multiplier() const override;

  void init() override;
  bool update() override;
  void render(const std::function<void(State)>& render_fn) override;
};

#endif