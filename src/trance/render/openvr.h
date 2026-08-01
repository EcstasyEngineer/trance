#ifndef TRANCE_SRC_TRANCE_RENDER_OPENVR_H
#define TRANCE_SRC_TRANCE_RENDER_OPENVR_H
#include <trance/render/render.h>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <openvr.h>
#pragma warning(pop)

class OpenVrRenderer : public Renderer
{
public:
  OpenVrRenderer(const trance_pb::System& system);
  ~OpenVrRenderer() override;
  bool success() const;

  bool vr_enabled() const override;
  uint32_t view_width() const override;
  uint32_t width() const override;
  uint32_t height() const override;
  float eye_spacing_multiplier() const override;

  void init() override;
  bool update() override;
  void render(const std::function<void(State)>& render_fn) override;
  // Compositor keep-alive when nothing was drawn (see Renderer::render_idle). NOTE the
  // `blank` argument is accepted and ignored here: SteamVR has no layerless-submit
  // equivalent, so this path can only resubmit the last frame against fresh poses --
  // pause/hide freeze the image in the headset rather than removing it, unlike OpenXR.
  bool render_idle(bool blank) override;

private:
  // WaitGetPoses (the compositor's pacing + liveness call) and the two-eye Submit +
  // PostPresentHandoff, shared by render() and render_idle(); the only difference
  // between them is whether new content is drawn into the eye textures in between.
  void wait_get_poses();
  void submit_eyes();

  // False once VR_Shutdown has run -- either from the destructor or from a runtime quit
  // event in update(). Guards every later use of _system / the compositor, both of which
  // are dangling after shutdown.
  bool _initialised;
  bool _success;
  uint32_t _width;
  uint32_t _height;
  float _eye_spacing_multiplier;

  vr::IVRSystem* _system;
  vr::IVRCompositor* _compositor;
  std::vector<uint32_t> _fbo;
  std::vector<uint32_t> _fb_tex;
};

#endif