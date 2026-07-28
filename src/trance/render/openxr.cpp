#include <trance/render/openxr.h>
#include <common/util.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// openxr_platform.h's Win32 section mentions IUnknown, which lean-and-mean
// windows.h leaves undefined.
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>
#endif
#pragma warning(pop)

// Pin the 1.0 API: current loader headers default XR_CURRENT_API_VERSION to
// 1.1.x, which older SteamVR builds reject with API_VERSION_UNSUPPORTED, and
// we use no 1.1 features.
#ifndef XR_API_VERSION_1_0
#define XR_API_VERSION_1_0 XR_MAKE_VERSION(1, 0, 0)
#endif

namespace
{
  // Head-locked quad placement: identity orientation in VIEW space, 1m ahead.
  // A 2-metre-wide quad at 1m subtends ~90 degrees horizontally -- fills most of
  // the FOV on Quest 3 (~104 degrees) and the original Vive without pushing
  // content into the distorted lens periphery. Height derives from the swapchain
  // aspect so the flat frame isn't stretched. Tune these two only.
  const float kQuadDistanceMetres = 1.f;
  const float kQuadWidthMetres = 2.f;

  // Log an XrResult with its symbolic name. xrResultToString needs a live
  // instance; before one exists, fall back to a static switch over the common
  // pre-instance codes, then the raw number.
  bool xr_check(XrInstance instance, XrResult result, const char* what)
  {
    if (XR_SUCCEEDED(result)) {
      return true;
    }
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, result, name))) {
      std::cerr << what << " failed: " << name << std::endl;
      return false;
    }
    switch (result) {
    case XR_ERROR_RUNTIME_UNAVAILABLE:
      std::cerr << what << " failed: XR_ERROR_RUNTIME_UNAVAILABLE (no active OpenXR runtime)"
                << std::endl;
      break;
    case XR_ERROR_API_VERSION_UNSUPPORTED:
      std::cerr << what << " failed: XR_ERROR_API_VERSION_UNSUPPORTED" << std::endl;
      break;
    case XR_ERROR_EXTENSION_NOT_PRESENT:
      std::cerr << what << " failed: XR_ERROR_EXTENSION_NOT_PRESENT" << std::endl;
      break;
    case XR_ERROR_RUNTIME_FAILURE:
      std::cerr << what << " failed: XR_ERROR_RUNTIME_FAILURE" << std::endl;
      break;
    default:
      std::cerr << what << " failed: " << static_cast<int32_t>(result) << std::endl;
      break;
    }
    return false;
  }
}

OpenXrRenderer::OpenXrRenderer(const trance_pb::System& system)
: _success{false}
, _width{0}
, _height{0}
, _instance{XR_NULL_HANDLE}
, _system_id{XR_NULL_SYSTEM_ID}
, _session{XR_NULL_HANDLE}
, _view_space{XR_NULL_HANDLE}
, _swapchain{XR_NULL_HANDLE, XR_NULL_HANDLE}
, _swapchain_format{0}
, _blend_mode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE}
, _session_state{XR_SESSION_STATE_UNKNOWN}
, _session_running{false}
, _lost{false}
{
#ifdef _WIN32
  (void) system;
  // Require XR_KHR_opengl_enable before creating anything.
  uint32_t extension_count = 0;
  if (!xr_check(XR_NULL_HANDLE,
                xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr),
                "xrEnumerateInstanceExtensionProperties")) {
    return;
  }
  std::vector<XrExtensionProperties> extensions(extension_count,
                                                XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
  if (!xr_check(XR_NULL_HANDLE,
                xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count,
                                                       extensions.data()),
                "xrEnumerateInstanceExtensionProperties")) {
    return;
  }
  bool opengl_supported = false;
  for (const auto& extension : extensions) {
    if (std::string{extension.extensionName} == XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) {
      opengl_supported = true;
    }
  }
  if (!opengl_supported) {
    std::cerr << "OpenXR runtime does not support XR_KHR_opengl_enable" << std::endl;
    return;
  }

  XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
  std::snprintf(instance_info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE,
                "trance");
  instance_info.applicationInfo.applicationVersion = 1;
  instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  const char* enabled_extensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
  instance_info.enabledExtensionCount = 1;
  instance_info.enabledExtensionNames = enabled_extensions;
  if (!xr_check(XR_NULL_HANDLE, xrCreateInstance(&instance_info, &_instance),
                "xrCreateInstance")) {
    return;
  }

  XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
  if (xr_check(_instance, xrGetInstanceProperties(_instance, &instance_properties),
               "xrGetInstanceProperties")) {
    std::cerr << "OpenXR runtime: " << instance_properties.runtimeName << " "
              << XR_VERSION_MAJOR(instance_properties.runtimeVersion) << "."
              << XR_VERSION_MINOR(instance_properties.runtimeVersion) << "."
              << XR_VERSION_PATCH(instance_properties.runtimeVersion) << std::endl;
  }

  XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  auto system_result = xrGetSystem(_instance, &system_info, &_system_id);
  if (system_result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
    std::cerr << "no OpenXR HMD available (is the headset connected / Quest Link active?)"
              << std::endl;
    return;
  }
  if (!xr_check(_instance, system_result, "xrGetSystem")) {
    return;
  }
  XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
  if (xr_check(_instance, xrGetSystemProperties(_instance, _system_id, &system_properties),
               "xrGetSystemProperties")) {
    std::cerr << "OpenXR system: " << system_properties.systemName << std::endl;
  }

  // Hidden window owns the OpenGL context (same pattern as OpenVrRenderer).
  // Vsync stays off unconditionally: xrWaitFrame is the frame pacing authority.
  _window.reset(new sf::RenderWindow);
  _window->create({}, "trance", sf::Style::None);
  _window->setVerticalSyncEnabled(false);
  _window->setFramerateLimit(0);
  _window->setVisible(false);
  if (!_window->setActive(true)) {
    std::cerr << "couldn't activate hidden OpenXR OpenGL context" << std::endl;
  }

  init_glew();

  // Extension functions aren't loader exports; resolve via xrGetInstanceProcAddr.
  // Calling this before xrCreateSession is mandatory (conformant runtimes return
  // XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING otherwise).
  PFN_xrGetOpenGLGraphicsRequirementsKHR get_gl_requirements = nullptr;
  if (!xr_check(_instance,
                xrGetInstanceProcAddr(_instance, "xrGetOpenGLGraphicsRequirementsKHR",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&get_gl_requirements)),
                "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)")) {
    return;
  }
  XrGraphicsRequirementsOpenGLKHR graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  if (!xr_check(_instance, get_gl_requirements(_instance, _system_id, &graphics_requirements),
                "xrGetOpenGLGraphicsRequirementsKHR")) {
    return;
  }
  GLint gl_major = 0;
  GLint gl_minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
  glGetIntegerv(GL_MINOR_VERSION, &gl_minor);
  XrVersion gl_version =
      XR_MAKE_VERSION(static_cast<uint16_t>(gl_major), static_cast<uint16_t>(gl_minor), 0);
  if (gl_version < graphics_requirements.minApiVersionSupported) {
    std::cerr << "OpenGL context version " << gl_major << "." << gl_minor
              << " is below the OpenXR runtime's minimum; attempting to continue" << std::endl;
  }

  HDC hdc = wglGetCurrentDC();
  HGLRC hglrc = wglGetCurrentContext();
  if (!hdc || !hglrc) {
    std::cerr << "no current WGL context for OpenXR session" << std::endl;
    return;
  }
  XrGraphicsBindingOpenGLWin32KHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
  graphics_binding.hDC = hdc;
  graphics_binding.hGLRC = hglrc;
  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &graphics_binding;
  session_info.systemId = _system_id;
  if (!xr_check(_instance, xrCreateSession(_instance, &session_info, &_session),
                "xrCreateSession")) {
    return;
  }

  // VIEW reference space with identity pose: a quad posed here rides the head
  // rigidly, which is the entire head-lock. No other space is ever located.
  XrReferenceSpaceCreateInfo space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  space_info.poseInReferenceSpace.orientation = {0.f, 0.f, 0.f, 1.f};
  space_info.poseInReferenceSpace.position = {0.f, 0.f, 0.f};
  if (!xr_check(_instance, xrCreateReferenceSpace(_session, &space_info, &_view_space),
                "xrCreateReferenceSpace")) {
    return;
  }

  // Per-eye native resolution is what the compositor resamples best; both eyes
  // share these dimensions.
  uint32_t view_count = 0;
  if (!xr_check(_instance,
                xrEnumerateViewConfigurationViews(_instance, _system_id,
                                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                  &view_count, nullptr),
                "xrEnumerateViewConfigurationViews")) {
    return;
  }
  std::vector<XrViewConfigurationView> views(view_count,
                                             XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
  if (!xr_check(_instance,
                xrEnumerateViewConfigurationViews(_instance, _system_id,
                                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                  view_count, &view_count, views.data()),
                "xrEnumerateViewConfigurationViews") ||
      views.empty()) {
    return;
  }
  _width = views[0].recommendedImageRectWidth;
  _height = views[0].recommendedImageRectHeight;

  // Both target runtimes list OPAQUE first, but enumerate to stay conformant.
  uint32_t blend_mode_count = 0;
  if (!xr_check(_instance,
                xrEnumerateEnvironmentBlendModes(_instance, _system_id,
                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                 &blend_mode_count, nullptr),
                "xrEnumerateEnvironmentBlendModes")) {
    return;
  }
  std::vector<XrEnvironmentBlendMode> blend_modes(blend_mode_count,
                                                  XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
  if (!xr_check(_instance,
                xrEnumerateEnvironmentBlendModes(_instance, _system_id,
                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                 blend_mode_count, &blend_mode_count,
                                                 blend_modes.data()),
                "xrEnumerateEnvironmentBlendModes") ||
      blend_modes.empty()) {
    return;
  }
  _blend_mode = blend_modes[0];

  // Prefer an sRGB-typed swapchain: the pipeline writes gamma-encoded bytes (the
  // OpenVR path submits them as ColorSpace_Gamma). With GL_FRAMEBUFFER_SRGB
  // disabled while drawing, GL stores our output verbatim and the compositor
  // decodes it as sRGB -- byte-exact match. Plain RGBA8 is treated as linear and
  // re-encoded by Quest Link, which washes the image out.
  uint32_t format_count = 0;
  if (!xr_check(_instance, xrEnumerateSwapchainFormats(_session, 0, &format_count, nullptr),
                "xrEnumerateSwapchainFormats")) {
    return;
  }
  std::vector<int64_t> formats(format_count);
  if (!xr_check(_instance,
                xrEnumerateSwapchainFormats(_session, format_count, &format_count, formats.data()),
                "xrEnumerateSwapchainFormats") ||
      formats.empty()) {
    return;
  }
  if (std::find(formats.begin(), formats.end(), int64_t{GL_SRGB8_ALPHA8}) != formats.end()) {
    _swapchain_format = GL_SRGB8_ALPHA8;
  } else if (std::find(formats.begin(), formats.end(), int64_t{GL_RGBA8}) != formats.end()) {
    std::cerr << "OpenXR runtime offers no GL_SRGB8_ALPHA8 swapchain; "
              << "falling back to GL_RGBA8 (colours may look washed out)" << std::endl;
    _swapchain_format = GL_RGBA8;
  } else {
    _swapchain_format = formats[0];
    std::cerr << "OpenXR runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8; "
              << "using format " << _swapchain_format << std::endl;
  }

  // One swapchain per eye: the scene is rendered twice with opposite eye_offset
  // shear, and each result goes to a quad layer restricted to that eye.
  for (int eye = 0; eye < 2; ++eye) {
    XrSwapchainCreateInfo swapchain_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain_info.usageFlags =
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchain_info.format = _swapchain_format;
    swapchain_info.sampleCount = 1;
    swapchain_info.width = _width;
    swapchain_info.height = _height;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = 1;
    swapchain_info.mipCount = 1;
    if (!xr_check(_instance, xrCreateSwapchain(_session, &swapchain_info, &_swapchain[eye]),
                  "xrCreateSwapchain")) {
      return;
    }

    uint32_t image_count = 0;
    if (!xr_check(_instance, xrEnumerateSwapchainImages(_swapchain[eye], 0, &image_count, nullptr),
                  "xrEnumerateSwapchainImages")) {
      return;
    }
    std::vector<XrSwapchainImageOpenGLKHR> images(
        image_count, XrSwapchainImageOpenGLKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    if (!xr_check(_instance,
                  xrEnumerateSwapchainImages(
                      _swapchain[eye], image_count, &image_count,
                      reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                  "xrEnumerateSwapchainImages")) {
      return;
    }
    // Wrap each runtime-owned texture in an FBO once at init; the runtime already
    // allocated storage, so no glTexImage2D here (unlike the OpenVR path).
    for (const auto& image : images) {
      GLuint fbo;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image.image, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "framebuffer failed" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        _fbo[eye].push_back(fbo);
        return;
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      _swapchain_tex[eye].push_back(image.image);
      _fbo[eye].push_back(fbo);
    }
  }
  _success = true;
#else
  (void) system;
  std::cerr << "OpenXR renderer is currently Windows-only (XR_KHR_opengl_enable Win32 binding)"
            << std::endl;
#endif
}

OpenXrRenderer::~OpenXrRenderer()
{
  // GL deletes need a current context; guarded so partial construction is safe.
  if (_window && !_window->setActive(true)) {
    std::cerr << "couldn't activate hidden OpenXR OpenGL context" << std::endl;
  }
  for (int eye = 0; eye < 2; ++eye) {
    for (auto fbo : _fbo[eye]) {
      glDeleteFramebuffers(1, &fbo);
    }
    // _swapchain_tex textures are runtime-owned; destroying the swapchain frees
    // them. Never glDeleteTextures them.
    if (_swapchain[eye] != XR_NULL_HANDLE) {
      xr_check(_instance, xrDestroySwapchain(_swapchain[eye]), "xrDestroySwapchain");
    }
  }
  if (_view_space != XR_NULL_HANDLE) {
    xr_check(_instance, xrDestroySpace(_view_space), "xrDestroySpace");
  }
  if (_session != XR_NULL_HANDLE) {
    xr_check(_instance, xrDestroySession(_session), "xrDestroySession");
  }
  if (_instance != XR_NULL_HANDLE) {
    xr_check(XR_NULL_HANDLE, xrDestroyInstance(_instance), "xrDestroyInstance");
  }
}

bool OpenXrRenderer::success() const
{
  return _success;
}

bool OpenXrRenderer::vr_enabled() const
{
  return true;
}

bool OpenXrRenderer::is_openvr() const
{
  return false;
}

uint32_t OpenXrRenderer::view_width() const
{
  return _width;
}

uint32_t OpenXrRenderer::width() const
{
  return _width;
}

uint32_t OpenXrRenderer::height() const
{
  return _height;
}

float OpenXrRenderer::eye_spacing_multiplier() const
{
  // Same derivation as the OpenVR path, but the "camera" here is the quad, not a
  // runtime projection: the content spans kQuadWidthMetres at kQuadDistanceMetres,
  // so its half-FOV tangent is fixed by our own geometry rather than queried.
  // eye_offset = eye_spacing_multiplier * eye_spacing_setting; at the shader's
  // near_plane=1 and nominal far_plane=129 the NDC parallax shift is
  // eye_offset / far_plane, and we want that to equal half_ipd / half_fov_tangent.
  // The 16 cancels the 1/16 default eye_spacing_setting, so the default gives
  // exactly physical parallax.
  //
  // A quad layer needs no per-runtime IPD query: the quad is a fixed virtual
  // screen, so the correct shear depends on the viewer's IPD only through the
  // nominal human average -- 64mm, the same 0.032f half-IPD the OpenVR path falls
  // back to when the runtime reports nothing.
  const float half_ipd = 0.032f;
  const float half_fov_tangent = (kQuadWidthMetres / 2.f) / kQuadDistanceMetres;
  const float nominal_far_plane = 1.f + 0.5f * 256.f;
  return 16.f * nominal_far_plane * half_ipd / half_fov_tangent;
}

void OpenXrRenderer::init()
{
}

bool OpenXrRenderer::update()
{
  if (_instance == XR_NULL_HANDLE) {
    return true;
  }
  if (_lost) {
    // render() hit an unrecoverable swapchain wait failure (see _lost's comment);
    // exit cleanly like instance loss rather than retrying into a wedged swapchain.
    std::cerr << "OpenXR swapchain lost; exiting" << std::endl;
    return false;
  }
  for (;;) {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    auto result = xrPollEvent(_instance, &event);
    if (result == XR_EVENT_UNAVAILABLE) {
      // Normal loop exit.
      break;
    }
    if (!xr_check(_instance, result, "xrPollEvent")) {
      // A hard event-pump failure means we can no longer see session state
      // changes (STOPPING/EXITING/LOSS_PENDING); treat it like instance loss.
      std::cerr << "OpenXR event polling failed; exiting" << std::endl;
      return false;
    }
    if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      std::cerr << "OpenXR instance loss pending; exiting" << std::endl;
      return false;
    }
    if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      auto& change = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
      _session_state = change.state;
      if (change.state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo begin_info{XR_TYPE_SESSION_BEGIN_INFO};
        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        _session_running =
            xr_check(_instance, xrBeginSession(_session, &begin_info), "xrBeginSession");
        if (!_session_running) {
          // Runtime says READY but refuses to begin: the HMD would stay black
          // forever. Terminate cleanly, same as the EXITING path.
          std::cerr << "OpenXR session failed to begin; exiting" << std::endl;
          return false;
        }
      } else if (change.state == XR_SESSION_STATE_STOPPING) {
        // Mandatory acknowledgement; Quest Link sends STOPPING on doff / Link
        // close, then READY again on resume -- the app survives without restart.
        xr_check(_instance, xrEndSession(_session), "xrEndSession");
        _session_running = false;
      } else if (change.state == XR_SESSION_STATE_EXITING ||
                 change.state == XR_SESSION_STATE_LOSS_PENDING) {
        // User quit from the runtime UI / runtime dying; exit cleanly.
        return false;
      }
      // IDLE / SYNCHRONIZED / VISIBLE / FOCUSED: no action.
    }
  }
  return true;
}

void OpenXrRenderer::render(const std::function<void(State)>& render_fn)
{
  if (!_session_running) {
    // No xrWaitFrame throttle available and the hidden window never swaps --
    // sleep instead of spinning while the runtime gets the session ready.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return;
  }
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
  if (!xr_check(_instance, xrWaitFrame(_session, &wait_info, &frame_state), "xrWaitFrame")) {
    // Anti-spin if the session breaks under us.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return;
  }
  // xrWaitFrame blocks to the runtime's pacing point -- it replaces WaitGetPoses
  // as the frame governor, including while SYNCHRONIZED/VISIBLE-but-unfocused.
  XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
  if (!xr_check(_instance, xrBeginFrame(_session, &begin_info), "xrBeginFrame")) {
    // Frame never began, so xrEndFrame must not be called; try again next loop.
    return;
  }
  // XR_FRAME_DISCARDED is a success code -- proceed, no log spam.

  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = frame_state.predictedDisplayTime;
  end_info.environmentBlendMode = _blend_mode;

  if (frame_state.shouldRender != XR_TRUE) {
    // Begin/End pair still required; pacing already came from xrWaitFrame.
    end_info.layerCount = 0;
    xr_check(_instance, xrEndFrame(_session, &end_info), "xrEndFrame");
    return;
  }

  // Render both eyes before submitting either: a partially-acquired frame must be
  // ended layerlessly, so nothing is composed until both swapchains are through
  // acquire/wait/render/release. eye 0 = left, eye 1 = right.
  uint32_t index[2] = {0, 0};
  // How many eyes have been acquired-and-waited so far, i.e. how many owe a
  // release before this frame can end. Only these get released on the error paths.
  int acquired = 0;
  bool eyes_ready = true;
  for (int eye = 0; eye < 2 && eyes_ready; ++eye) {
    XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!xr_check(_instance, xrAcquireSwapchainImage(_swapchain[eye], &acquire_info, &index[eye]),
                  "xrAcquireSwapchainImage")) {
      // No image acquired for this eye: index[eye] is stale, so no GL render and
      // no release for it. Any earlier eye is released below, and the frame -- which
      // was begun -- is still ended, layerlessly.
      eyes_ready = false;
      break;
    }
    XrSwapchainImageWaitInfo image_wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    image_wait_info.timeout = XR_INFINITE_DURATION;
    if (!xr_check(_instance, xrWaitSwapchainImage(_swapchain[eye], &image_wait_info),
                  "xrWaitSwapchainImage")) {
      // Image never became ready: skip the render, end layerless -- and treat it as
      // FATAL. The image acquired above can only be released after a successful
      // wait, so this failure permanently consumes a swapchain slot; retrying would
      // eventually wedge every acquire (XR_ERROR_CALL_ORDER_INVALID) with the view
      // black forever. The wait was XR_INFINITE_DURATION, so a failure means the
      // session/instance is gone anyway -- flag it and let update() exit cleanly.
      _lost = true;
      eyes_ready = false;
      break;
    }
    ++acquired;

    glBindFramebuffer(GL_FRAMEBUFFER, _fbo[eye][index[eye]]);
    // Gamma passthrough into the sRGB-typed image (see format selection above).
    glDisable(GL_FRAMEBUFFER_SRGB);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, _width, _height);
    // Render the app content once per eye; the opposite eye_offset shear between
    // the two passes is the entire stereo effect (the quads themselves are posed
    // identically). The debug HUD only draws for State::NONE, so it never lands in
    // the VR quads.
    render_fn(eye ? State::VR_RIGHT : State::VR_LEFT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  for (int eye = 0; eye < acquired; ++eye) {
    if (!xr_check(_instance, xrReleaseSwapchainImage(_swapchain[eye], &release_info),
                  "xrReleaseSwapchainImage")) {
      // FATAL, same reasoning as the wait failure above: an image that failed to
      // release is still owned by the app, so that swapchain slot is permanently
      // consumed and subsequent acquires would eventually wedge
      // (XR_ERROR_CALL_ORDER_INVALID) with the view black forever. Submitting a quad
      // sourced from an unreleased image is also invalid usage -- so drop the layers
      // and end the (already begun) frame layerlessly, then let update() exit cleanly.
      _lost = true;
      eyes_ready = false;
    }
  }
  if (!eyes_ready) {
    end_info.layerCount = 0;
    xr_check(_instance, xrEndFrame(_session, &end_info), "xrEndFrame");
    return;
  }

  // Head-locked stereo quads: identical identity-orientation pose and size in VIEW
  // space, one per eye, differing only in eyeVisibility and source swapchain. The
  // head-lock is deliberate -- the parallax lives in the rendered content, not in
  // the layer placement, so the two quads must NOT be posed apart.
  XrCompositionLayerQuad quads[2] = {XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD},
                                     XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD}};
  for (int eye = 0; eye < 2; ++eye) {
    auto& quad = quads[eye];
    quad.layerFlags = 0;
    quad.space = _view_space;
    quad.eyeVisibility = eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
    quad.subImage.swapchain = _swapchain[eye];
    quad.subImage.imageRect = {{0, 0},
                               {static_cast<int32_t>(_width), static_cast<int32_t>(_height)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose.orientation = {0.f, 0.f, 0.f, 1.f};
    quad.pose.position = {0.f, 0.f, -kQuadDistanceMetres};
    quad.size = {kQuadWidthMetres, kQuadWidthMetres * float(_height) / float(_width)};
  }

  const XrCompositionLayerBaseHeader* layers[] = {
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[0]),
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[1])};
  end_info.layerCount = 2;
  end_info.layers = layers;
  xr_check(_instance, xrEndFrame(_session, &end_info), "xrEndFrame");
}

bool OpenXrRenderer::render_idle()
{
  // Paused/hidden keep-alive: a running session must keep the
  // xrWaitFrame/xrBeginFrame/xrEndFrame loop going (stalling it makes the runtime
  // flag the app unresponsive), and submitting layerCount=0 frames blanks the quad
  // so hide/pause actually removes the content from the headset instead of
  // freezing the last submitted frame there.
  if (_instance == XR_NULL_HANDLE) {
    return false;
  }
  if (!_session_running) {
    // Same anti-spin as render(): no xrWaitFrame pacing available while the
    // runtime holds the session out of the running state.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
  }
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
  if (!xr_check(_instance, xrWaitFrame(_session, &wait_info, &frame_state), "xrWaitFrame")) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
  }
  XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
  if (!xr_check(_instance, xrBeginFrame(_session, &begin_info), "xrBeginFrame")) {
    // Frame never began, so xrEndFrame must not be called; xrWaitFrame already
    // paced this iteration.
    return true;
  }
  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = frame_state.predictedDisplayTime;
  end_info.environmentBlendMode = _blend_mode;
  end_info.layerCount = 0;
  xr_check(_instance, xrEndFrame(_session, &end_info), "xrEndFrame");
  return true;
}
