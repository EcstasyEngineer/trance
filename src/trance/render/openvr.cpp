#include <trance/render/openvr.h>
#include <common/util.h>
#include <algorithm>
#include <iostream>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#pragma warning(pop)

namespace
{
  // Our own process id, for telling "the runtime is tearing US down" apart from "some
  // other VR process quit" in the VREvent_ProcessQuit handler -- both arrive on this
  // app's event queue and only the former should end the session.
  uint32_t current_pid()
  {
#ifdef _WIN32
    return static_cast<uint32_t>(_getpid());
#else
    return static_cast<uint32_t>(getpid());
#endif
  }
}

OpenVrRenderer::OpenVrRenderer(const trance_pb::System& system)
: _initialised{false}
, _success{false}
, _width{0}
, _height{0}
, _eye_spacing_multiplier{150.f}
, _system{nullptr}
, _compositor{nullptr}
{
  vr::HmdError error;
  _system = vr::VR_Init(&error, vr::VRApplication_Scene);
  if (!_system || error != vr::VRInitError_None) {
    std::cerr << "OpenVR initialization failed" << std::endl;
    std::cerr << vr::VR_GetVRInitErrorAsEnglishDescription(error) << std::endl;
    return;
  }
  _initialised = true;

  _window.reset(new sf::RenderWindow);
  _window->create({}, "trance", sf::Style::None);
  _window->setVerticalSyncEnabled(system.enable_vsync());
  _window->setFramerateLimit(0);
  _window->setVisible(false);
  if (!_window->setActive(true)) {
    std::cerr << "couldn't activate hidden OpenVR OpenGL context" << std::endl;
  }

  init_glew();

  _compositor = vr::VRCompositor();
  if (!_compositor) {
    std::cerr << "OpenVR compositor failed" << std::endl;
    return;
  }

  _system->GetRecommendedRenderTargetSize(&_width, &_height);

  // Compute eye_spacing_multiplier from actual HMD IPD and projection FOV.
  // eye_offset = eye_spacing_multiplier * eye_spacing_setting. At the shader's
  // near_plane=1 and nominal far_plane=129, the NDC parallax shift is
  // eye_offset / far_plane. We want that to equal half_ipd / half_fov_tangent
  // (the physical eye separation in view-space units), so:
  // eye_spacing_multiplier = 16 * nominal_far * half_ipd / half_fov_width
  {
    auto left_eye = _system->GetEyeToHeadTransform(vr::Eye_Left);
    float half_ipd = -left_eye.m[0][3];
    if (half_ipd <= 0.f) {
      half_ipd = 0.032f;
    }
    float proj_left, proj_right, proj_top, proj_bottom;
    _system->GetProjectionRaw(vr::Eye_Left, &proj_left, &proj_right, &proj_top, &proj_bottom);
    const float half_fov_width = (proj_right - proj_left) / 2.0f;
    const float nominal_far_plane = 1.f + 0.5f * 256.f;
    _eye_spacing_multiplier = 16.f * nominal_far_plane * half_ipd / half_fov_width;
  }
  for (int i = 0; i < 2; ++i) {
    GLuint fbo;
    GLuint fb_tex;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &fb_tex);
    glBindTexture(GL_TEXTURE_2D, fb_tex);
    // Mipmap completeness is NOT optional here, and its absence fails silently in the
    // worst possible way. Only level 0 is allocated below, but GL_TEXTURE_MIN_FILTER
    // defaults to GL_NEAREST_MIPMAP_LINEAR, which makes the texture SAMPLING-incomplete
    // (levels 1..n missing) -- so the compositor samples it as black. Nothing catches
    // that: glCheckFramebufferStatus below only certifies the texture as a render
    // TARGET, framebuffer completeness says nothing about sampling completeness, and
    // Submit() duly returns VRCompositorError_None. Result is a perfectly healthy-looking
    // log and a black headset. Setting GL_LINEAR (no mip lookup) plus an explicit
    // GL_TEXTURE_MAX_LEVEL of 0 (the only level that exists) makes it complete both ways.
    // Valve's own hellovr_opengl sample sets exactly these before submitting.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::cerr << "framebuffer failed" << std::endl;
      return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _fbo.push_back(fbo);
    _fb_tex.push_back(fb_tex);
  }
  _success = true;
}

OpenVrRenderer::~OpenVrRenderer()
{
  for (auto fb_tex : _fb_tex) {
    glDeleteTextures(1, &fb_tex);
  }
  for (auto fbo : _fbo) {
    glDeleteFramebuffers(1, &fbo);
  }
  if (_initialised) {
    vr::VR_Shutdown();
  }
}

bool OpenVrRenderer::success() const
{
  return _success;
}

bool OpenVrRenderer::vr_enabled() const
{
  return true;
}

uint32_t OpenVrRenderer::view_width() const
{
  return _width;
}

uint32_t OpenVrRenderer::width() const
{
  return _width;
}

uint32_t OpenVrRenderer::height() const
{
  return _height;
}

float OpenVrRenderer::eye_spacing_multiplier() const
{
  return _eye_spacing_multiplier;
}

void OpenVrRenderer::init()
{
}

bool OpenVrRenderer::update()
{
  if (!_initialised) {
    // A quit event below already ran VR_Shutdown, so _system and the compositor are
    // gone. Keep answering "stop" instead of touching them: main.cpp can pump us more
    // than once between continue_playing going false and the loop actually breaking
    // (director.update() drains several visual frames per iteration).
    return false;
  }
  vr::VREvent_t event;
  bool keep_running = true;
  while (_system->PollNextEvent(&event, sizeof(event))) {
    // LIFECYCLE, not rendering -- nothing here has any bearing on what the headset
    // shows. This queue used to be drained and discarded wholesale, which meant the
    // runtime's shutdown request could never reach us: quitting SteamVR (or quitting
    // trance from the dashboard) left the process running until SteamVR force-killed it
    // a few seconds later, skipping every destructor -- session teardown, the async
    // theme thread, the config save.
    if (event.eventType == vr::VREvent_Quit) {
      // Documented contract (openvr.h, IVRSystem::AcknowledgeQuit_Exiting): acknowledge
      // that VREvent_Quit was received and that the process is exiting.
      std::cerr << "OpenVR runtime requested quit" << std::endl;
      _system->AcknowledgeQuit_Exiting();
      keep_running = false;
      break;
    }
    if (event.eventType == vr::VREvent_ProcessQuit && event.data.process.pid == current_pid()) {
      // Same shutdown path, but note the pid guard: ProcessQuit is delivered for VR
      // processes generally, so reacting to every one of them would let an unrelated
      // overlay app exiting take trance down with it. Only our own pid means us.
      std::cerr << "OpenVR runtime terminated this process's VR session" << std::endl;
      keep_running = false;
      break;
    }
    // Everything else (tracking, input, chaperone, dashboard visibility) is genuinely
    // not our business: this renderer draws a head-locked stereo pair and reads no
    // poses beyond WaitGetPoses' implicit synchronisation.
  }
  if (!keep_running) {
    // Shut the API down here rather than leaving it to the destructor: the runtime
    // expects a prompt exit after a quit event, and doing it now makes the dangling
    // state explicit (see the _initialised guard above and in the render paths).
    vr::VR_Shutdown();
    _initialised = false;
    _system = nullptr;
    _compositor = nullptr;
    return false;
  }
  return true;
}

void OpenVrRenderer::wait_get_poses()
{
  // WaitGetPoses is the compositor's frame governor AND its liveness signal: it blocks
  // until the runtime's next pacing point, so it -- not our own clock -- decides how
  // fast this backend runs.
  static vr::TrackedDevicePose_t m_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
  auto error = vr::VRCompositor()->WaitGetPoses(m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount,
                                                nullptr, 0);
  if (error != vr::VRCompositorError_None) {
    std::cerr << "compositor wait failed: " << static_cast<uint32_t>(error) << std::endl;
  }
}

void OpenVrRenderer::submit_eyes()
{
  vr::Texture_t left = {(void*) (uintptr_t) _fb_tex[0], vr::TextureType_OpenGL, vr::ColorSpace_Gamma};
  vr::Texture_t right = {(void*) (uintptr_t) _fb_tex[1], vr::TextureType_OpenGL, vr::ColorSpace_Gamma};
  auto error = vr::VRCompositor()->Submit(vr::Eye_Left, &left);
  if (error != vr::VRCompositorError_None) {
    std::cerr << "compositor submit failed: " << static_cast<uint32_t>(error) << std::endl;
  }
  error = vr::VRCompositor()->Submit(vr::Eye_Right, &right);
  if (error != vr::VRCompositorError_None) {
    std::cerr << "compositor submit failed: " << static_cast<uint32_t>(error) << std::endl;
  }
  vr::VRCompositor()->PostPresentHandoff();
}

void OpenVrRenderer::render(const std::function<void(State)>& render_fn)
{
  if (!_initialised) {
    return;
  }
  wait_get_poses();
  for (int eye = 0; eye < 2; ++eye) {
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo[eye]);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, _width, _height);
    render_fn(eye ? State::VR_RIGHT : State::VR_LEFT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  submit_eyes();
}

bool OpenVrRenderer::render_idle(bool blank)
{
  if (!_initialised || !_success) {
    return false;
  }
  // Keep-alive for every iteration that draws nothing -- paused, hidden, or simply
  // between visual frames. Without it this backend inherited Renderer::render_idle's
  // `return false` and main.cpp slept instead, so WaitGetPoses/Submit stopped entirely
  // for the duration of a pause or a Shift+F11 hide and the headset dropped to the
  // SteamVR grey void.
  //
  // `blank` decides WHAT gets submitted, exactly as it does for OpenXR:
  //   false (merely between visual frames) -- resubmit the unchanged eye textures. That
  //     buys the handshake plus correct reprojection of the held frame against the fresh
  //     poses, so the image stays locked to the world instead of smearing with head motion.
  //   true (paused/hidden) -- black eye textures. OpenVR has no layerless submit
  //     (no analogue of xrEndFrame with layerCount=0), but the EFFECT the flag asks for
  //     is achievable without one: clearing the scene layer to black and submitting that
  //     is what makes `hide` genuinely vanish in the headset. Re-presenting the held frame
  //     here instead left Shift+F11 / the tray Hide item / the `hide` verb freezing the
  //     image in the headset -- the panic button silently failing on one renderer.
  wait_get_poses();
  if (blank) {
    // Save and restore the clear colour: render()'s glClear shares this context and its
    // (default, transparent-black) clear is deliberately left alone.
    GLfloat prev_clear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    for (int eye = 0; eye < 2; ++eye) {
      glBindFramebuffer(GL_FRAMEBUFFER, _fbo[eye]);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
  }
  submit_eyes();
  // WaitGetPoses already blocked to the compositor's cadence; the caller must not sleep
  // on top of it.
  return true;
}
