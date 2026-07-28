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
  bool success() const;

  bool vr_enabled() const override;
  bool is_openvr() const override;
  uint32_t view_width() const override;
  uint32_t width() const override;
  uint32_t height() const override;
  float eye_spacing_multiplier() const override;

  void init() override;
  bool update() override;
  void render(const std::function<void(State)>& render_fn) override;
  // Layerless frame submission while paused/hidden (see Renderer::render_idle).
  bool render_idle() override;

private:
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
  // Swapchain textures are runtime-owned; never glDeleteTextures them. The FBOs
  // wrapping them are ours. Indexed per eye, matching _swapchain.
  std::vector<uint32_t> _swapchain_tex[2];
  std::vector<uint32_t> _fbo[2];
};

#endif
