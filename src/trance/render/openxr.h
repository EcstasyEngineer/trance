#ifndef TRANCE_SRC_TRANCE_RENDER_OPENXR_H
#define TRANCE_SRC_TRANCE_RENDER_OPENXR_H
#include <trance/render/render.h>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <openxr/openxr.h>
#pragma warning(pop)

class OpenXrRenderer : public Renderer
{
public:
  OpenXrRenderer(const trance_pb::System& system);
  ~OpenXrRenderer() override;
  // Compile-time availability. The graphics binding is XR_KHR_opengl_enable's Win32
  // one, so on every other platform the constructor is a stub that prints
  // "Windows-only" and fails. Callers gate on this rather than construct-and-fail:
  // a build with no XR backend in it at all must not report a VR *failure* (banner,
  // F2 panel line) on every launch.
  static constexpr bool available()
  {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
  }
  bool success() const;

  bool vr_enabled() const override;
  uint32_t view_width() const override;
  uint32_t width() const override;
  uint32_t height() const override;
  float eye_spacing_multiplier() const override;

  void init() override;
  bool update() override;
  void render(const std::function<void(State)>& render_fn) override;
  // Keep-alive frame submission when nothing was drawn: layerless while paused/hidden,
  // re-presenting the last quads otherwise (see Renderer::render_idle).
  bool render_idle(bool blank) override;

private:
  // The two head-locked quad layers, rebuilt identically by render() and render_idle().
  // Nothing in them varies per frame, which is precisely why the idle path can
  // re-present the last rendered frame without touching a swapchain.
  void fill_quads(XrCompositionLayerQuad (&quads)[2]) const;

  bool _success;
  uint32_t _width;
  uint32_t _height;

  XrInstance _instance;
  XrSystemId _system_id;
  XrSession _session;
  XrSpace _view_space;  // XR_REFERENCE_SPACE_TYPE_VIEW: quads posed here are head-locked.
  // One swapchain per eye, index 0 = left, 1 = right. Stereo parallax comes from
  // rendering the scene twice with the eye_offset shader shear, not from posing
  // the two quads apart -- they share one head-locked pose.
  XrSwapchain _swapchain[2];
  int64_t _swapchain_format;
  XrEnvironmentBlendMode _blend_mode;
  XrSessionState _session_state;
  bool _session_running;  // between xrBeginSession and xrEndSession.
  // Set when xrWaitSwapchainImage fails: the acquired image can never be released
  // (release requires a successful wait), so the swapchain is one slot shorter for
  // good -- treat it as session loss and make the next update() exit cleanly.
  bool _lost;
  // True once render() has released an image into both swapchains. Until then the
  // swapchains hold nothing, so render_idle has no last frame to re-present and must
  // submit layerlessly.
  bool _has_content;
  // One FBO per swapchain image, per eye, wrapping the runtime-owned texture. The FBOs
  // are ours to delete; the textures they wrap are not (xrDestroySwapchain frees those).
  // Indexed [eye][swapchain image index], matching _swapchain.
  std::vector<uint32_t> _fbo[2];
};

#endif
