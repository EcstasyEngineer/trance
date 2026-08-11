#include <trance/render/openxr.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#pragma warning(push, 0)
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

  // How often an unattached run looks for a headset, and how long a probe may go without
  // posting a verdict before we give up on it for the whole run (D7).
  const auto kProbeInterval = std::chrono::seconds(5);
  const auto kProbeWatchdog = std::chrono::seconds(30);

  // An XrResult's symbolic name. xrResultToString needs a live instance; before one
  // exists, fall back to a static switch over the common pre-instance codes, then the
  // raw number.
  std::string xr_result_name(XrInstance instance, XrResult result)
  {
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, result, name))) {
      return name;
    }
    switch (result) {
    case XR_ERROR_RUNTIME_UNAVAILABLE:
      return "XR_ERROR_RUNTIME_UNAVAILABLE (no active OpenXR runtime)";
    case XR_ERROR_API_VERSION_UNSUPPORTED:
      return "XR_ERROR_API_VERSION_UNSUPPORTED";
    case XR_ERROR_EXTENSION_NOT_PRESENT:
      return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_RUNTIME_FAILURE:
      return "XR_ERROR_RUNTIME_FAILURE";
    default:
      return std::to_string(static_cast<int32_t>(result));
    }
  }

  // Log an XrResult failure to `out`. The stream, rather than std::cerr directly, because
  // two of the three callers do not print as they go: the probe runs on a worker thread
  // and the attach accumulates a log the caller may decide to swallow (D3).
  bool xr_check(std::ostream& out, XrInstance instance, XrResult result, const char* what)
  {
    if (XR_SUCCEEDED(result)) {
      return true;
    }
    out << what << " failed: " << xr_result_name(instance, result) << std::endl;
    return false;
  }

#ifdef _WIN32
  // Whether the loader has any runtime to load at all. The OpenXR loader resolves the
  // active runtime's manifest from SOFTWARE\Khronos\OpenXR\1\ActiveRuntime (HKCU
  // consulted before HKLM); with neither set, nothing is loaded into the process and
  // enumeration succeeds while reporting nothing. We only ever READ this -- runtime
  // selection is the user's, made in the Oculus or SteamVR desktop app.
  bool active_openxr_runtime_registered()
  {
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
      DWORD size = 0;
      if (RegGetValueA(root, "SOFTWARE\\Khronos\\OpenXR\\1", "ActiveRuntime", RRF_RT_REG_SZ,
                       nullptr, nullptr, &size) == ERROR_SUCCESS) {
        return true;
      }
    }
    return false;
  }

  // Where a probe stopped. Kept separate from XrProbe::State (which also covers states no
  // probe can produce -- attached, attach-failed, watchdog-disabled) so the worker body
  // below is an ordinary function with no knowledge of the probe's bookkeeping.
  enum class ProbeLeaf {
    Ok,
    Unreachable,
    NoOpenGl,
    NoHmd,
  };

  // THE WORKER BODY: runs on the detached probe thread, touches nothing but the OpenXR
  // loader and its own locals. Every failure destroys the instance before returning (D7's
  // no-retention rule: a retained instance pins the loader to the runtime that was active
  // when it was created, which is exactly what breaks Oculus<->SteamVR switching). On Ok
  // the instance and system are handed out and become the caller's to destroy.
  //
  // The three-way diagnosis this preserves (D3) is load-bearing for self-diagnosis: a
  // runtime that is not registered at all (the inline gate in XrProbe::poll, which is why
  // it is not a leaf here), one that is registered but does not answer, and one that
  // answers but has no HMD attached are three different things for the user to do
  // something about. Every tail says the app keeps retrying, because it does.
  ProbeLeaf run_probe(std::ostream& out, XrInstance& instance_out, XrSystemId& system_out,
                      std::string& runtime_name)
  {
    uint32_t extension_count = 0;
    const auto enumerated =
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
    if (XR_FAILED(enumerated)) {
      out << "could not connect to the OpenXR runtime ("
          << xr_result_name(XR_NULL_HANDLE, enumerated)
          << "): is Quest Link or SteamVR running? trance retries every 5 seconds";
      return ProbeLeaf::Unreachable;
    }
    if (extension_count == 0) {
      // Zero extensions from a SUCCESSFUL enumeration is the loader saying it had no
      // runtime to ask -- it is NOT a runtime saying it lacks a feature, so it must never
      // be reported as one (that sends the reader hunting a driver or headset problem
      // that does not exist). The no-ActiveRuntime-key machine never gets here at all: the
      // registry gate answers it inline, without loading anything.
      out << "the OpenXR runtime is registered but answered with zero instance extensions; "
             "trance retries every 5 seconds";
      return ProbeLeaf::Unreachable;
    }
    std::vector<XrExtensionProperties> extensions(
        extension_count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    if (!xr_check(out, XR_NULL_HANDLE,
                  xrEnumerateInstanceExtensionProperties(nullptr, extension_count,
                                                         &extension_count, extensions.data()),
                  "xrEnumerateInstanceExtensionProperties")) {
      // xr_check has already put the result name on its own line; this is its tail.
      out << "  trance retries every 5 seconds";
      return ProbeLeaf::Unreachable;
    }
    bool opengl_supported = false;
    for (const auto& extension : extensions) {
      if (std::string{extension.extensionName} == XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) {
        opengl_supported = true;
      }
    }
    if (!opengl_supported) {
      out << "the active OpenXR runtime does not support XR_KHR_opengl_enable; no headset "
             "output is possible against it";
      return ProbeLeaf::NoOpenGl;
    }

    XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::snprintf(instance_info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE,
                  "trance");
    instance_info.applicationInfo.applicationVersion = 1;
    instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    const char* enabled_extensions[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    instance_info.enabledExtensionCount = 1;
    instance_info.enabledExtensionNames = enabled_extensions;
    XrInstance instance = XR_NULL_HANDLE;
    const auto created = xrCreateInstance(&instance_info, &instance);
    if (XR_FAILED(created)) {
      out << "could not create an OpenXR instance (" << xr_result_name(XR_NULL_HANDLE, created)
          << "); trance retries every 5 seconds";
      return ProbeLeaf::Unreachable;
    }

    XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &instance_properties))) {
      std::ostringstream name;
      name << instance_properties.runtimeName << " "
           << XR_VERSION_MAJOR(instance_properties.runtimeVersion) << "."
           << XR_VERSION_MINOR(instance_properties.runtimeVersion) << "."
           << XR_VERSION_PATCH(instance_properties.runtimeVersion);
      runtime_name = name.str();
    } else {
      runtime_name = "(unnamed OpenXR runtime)";
    }

    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    const auto system_result = xrGetSystem(instance, &system_info, &system_id);
    if (system_result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
      out << "no OpenXR HMD available (is the headset connected / Quest Link active?); "
             "trance retries every 5 seconds";
      // Destroyed, not kept: this is the retention case D7 calls out by name. Holding a
      // no-HMD instance open would keep the loader pinned to this runtime, so donning a
      // headset after switching runtimes would attach to the wrong one.
      xrDestroyInstance(instance);
      return ProbeLeaf::NoHmd;
    }
    if (XR_FAILED(system_result)) {
      out << "xrGetSystem failed: " << xr_result_name(instance, system_result)
          << "; trance retries every 5 seconds";
      xrDestroyInstance(instance);
      return ProbeLeaf::Unreachable;
    }
    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(instance, system_id, &system_properties))) {
      runtime_name += std::string{" ("} + system_properties.systemName + ")";
    }
    instance_out = instance;
    system_out = system_id;
    return ProbeLeaf::Ok;
  }
#endif
}

struct XrProbe::Slot {
  std::mutex mutex;
  bool done = false;
  // Set by poll() when the watchdog gave up, or by ~XrProbe: the worker, if it ever
  // finishes, destroys its own instance instead of posting into a slot nobody reads.
  bool abandoned = false;
  bool ok = false;
  State state = State::Unreachable;
  std::string message;
  XrProbeVerdict verdict;
};

XrProbe::XrProbe()
: _next_probe{std::chrono::steady_clock::now()}
, _probe_started{}
, _state{State::Unknown}
{
}

XrProbe::~XrProbe()
{
  if (_slot) {
    std::lock_guard<std::mutex> lock{_slot->mutex};
    if (_slot->done) {
      if (_slot->verdict.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(_slot->verdict.instance);
      }
    } else {
      // Still running -- and unkillable. It owns the cleanup now.
      _slot->abandoned = true;
    }
  }
  // A verdict that poll() reported but nobody took (shutdown between the two).
  if (_verdict.instance != XR_NULL_HANDLE) {
    xrDestroyInstance(_verdict.instance);
  }
}

bool XrProbe::poll()
{
#ifdef _WIN32
  if (_state == State::Disabled || _state == State::Attached) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (_slot) {
    // A probe is in flight: take its verdict if it posted one, or give up on it.
    bool done = false;
    bool ok = false;
    State state = State::Unreachable;
    std::string message;
    XrProbeVerdict verdict;
    const bool timed_out = now - _probe_started >= kProbeWatchdog;
    {
      std::lock_guard<std::mutex> lock{_slot->mutex};
      done = _slot->done;
      if (done) {
        ok = _slot->ok;
        state = _slot->state;
        message = _slot->message;
        verdict = _slot->verdict;
      } else if (timed_out) {
        _slot->abandoned = true;
      }
    }
    if (!done) {
      if (timed_out) {
        // Explicit policy, since a hung thread cannot be killed (D7): stop probing for
        // the run and leak the thread. The desktop is unaffected either way -- that is
        // the entire reason probing is off-thread.
        _slot.reset();
        set_state(State::Disabled,
                  "XR probe is not responding; XR disabled until restart (broken runtime "
                  "install?)");
      }
      return false;
    }
    _slot.reset();
    _next_probe = now + kProbeInterval;
    if (ok) {
      _verdict = verdict;
      return true;
    }
    set_state(state, message);
    return false;
  }
  if (now < _next_probe) {
    return false;
  }
  // The inline gate: one registry read, no loader involvement, so a machine with no VR
  // software on it pays nothing for probing forever.
  if (!active_openxr_runtime_registered()) {
    _next_probe = now + kProbeInterval;
    set_state(State::NoRuntime,
              "no active OpenXR runtime is registered: SOFTWARE\\Khronos\\OpenXR\\1\\"
              "ActiveRuntime is set in neither HKEY_CURRENT_USER nor HKEY_LOCAL_MACHINE -- "
              "set it in the Oculus (Quest Link) or SteamVR desktop app; trance retries "
              "every 5 seconds");
    return false;
  }
  auto slot = std::make_shared<Slot>();
  _slot = slot;
  _probe_started = now;
  // Detached, and exactly one at a time: the next probe cannot start until this one posts
  // a verdict or the watchdog abandons it.
  std::thread{[slot] {
    std::ostringstream out;
    XrProbeVerdict verdict;
    const auto leaf = run_probe(out, verdict.instance, verdict.system_id, verdict.runtime_name);
    std::lock_guard<std::mutex> lock{slot->mutex};
    if (slot->abandoned) {
      if (verdict.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(verdict.instance);
      }
      return;
    }
    slot->ok = leaf == ProbeLeaf::Ok;
    switch (leaf) {
    case ProbeLeaf::NoOpenGl:
      slot->state = State::NoOpenGl;
      break;
    case ProbeLeaf::NoHmd:
      slot->state = State::NoHmd;
      break;
    default:
      slot->state = State::Unreachable;
      break;
    }
    slot->message = out.str();
    slot->verdict = verdict;
    slot->done = true;
  }}.detach();
  return false;
#else
  // No Win32 graphics binding, so there is nothing an attach could bind to; XrOutput's
  // availability gate says the same thing to every other caller.
  return false;
#endif
}

XrProbeVerdict XrProbe::take_verdict()
{
  XrProbeVerdict verdict = _verdict;
  _verdict = XrProbeVerdict{};
  return verdict;
}

void XrProbe::note_attached()
{
  _state = State::Attached;
}

void XrProbe::note_attach_failure(const std::string& log)
{
  _next_probe = std::chrono::steady_clock::now() + kProbeInterval;
  if (_state == State::AttachFailed) {
    return;
  }
  _state = State::AttachFailed;
  // The whole captured diagnosis, once. A session that cannot be created against this
  // window's context usually cannot be created against it five seconds later either, so
  // repeating this every probe is exactly the overnight-console noise D3 forbids.
  std::cerr << log;
  std::cerr << "could not create an OpenXR session on the window's GL context; trance "
               "retries every 5 seconds"
            << std::endl;
}

void XrProbe::note_detached()
{
  // Back to "nothing decided", so the next failure prints even if it is the same leaf as
  // the one before the attach -- a detach is itself a state change the user saw a line
  // for. The retry clock is pushed out a full interval: whatever just killed the session
  // is usually still in progress.
  _state = State::Unknown;
  _next_probe = std::chrono::steady_clock::now() + kProbeInterval;
}

bool XrProbe::probing() const
{
  return XrOutput::available() && _state != State::Disabled;
}

std::string XrProbe::status_detail() const
{
  switch (_state) {
  case State::NoRuntime:
    return "no headset output: no active OpenXR runtime is registered (retrying every 5s)";
  case State::Unreachable:
    return "no headset output: the OpenXR runtime is registered but did not answer "
           "(retrying every 5s)";
  case State::NoOpenGl:
    return "no headset output: the OpenXR runtime does not support XR_KHR_opengl_enable";
  case State::NoHmd:
    return "no headset output: no HMD (connect the headset / start Quest Link)";
  case State::AttachFailed:
    return "no headset output: the OpenXR session could not be created (see the console)";
  case State::Disabled:
    return "no headset output: the XR probe stopped responding; XR is off until restart";
  case State::Unknown:
  case State::Attached:
  default:
    return {};
  }
}

void XrProbe::set_state(State state, const std::string& message)
{
  // D3, the whole of it: first occurrence and transitions, never once per probe.
  if (state == _state) {
    return;
  }
  _state = state;
  if (!message.empty()) {
    std::cerr << message << std::endl;
  }
}

XrOutput::XrOutput(XrInstance instance, XrSystemId system_id)
: _success{false}
, _width{0}
, _height{0}
, _instance{instance}
, _system_id{system_id}
, _session{XR_NULL_HANDLE}
, _view_space{XR_NULL_HANDLE}
, _swapchain{XR_NULL_HANDLE, XR_NULL_HANDLE}
, _swapchain_format{0}
, _blend_mode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE}
, _session_state{XR_SESSION_STATE_UNKNOWN}
, _session_running{false}
, _detach_pending{false}
, _has_content{false}
{
  // The instance is already ours (the probe made it off-thread and handed it over), so
  // every early return below still runs the destructor's xrDestroyInstance -- which is
  // what keeps D7's no-retention rule true for a failed ATTACH as well as a failed probe.
  std::ostringstream out;
  _success = build(out);
  _log = out.str();
}

bool XrOutput::build(std::ostream& out)
{
#ifdef _WIN32
  if (_instance == XR_NULL_HANDLE || _system_id == XR_NULL_SYSTEM_ID) {
    out << "no OpenXR instance to attach to" << std::endl;
    return false;
  }

  // Everything here runs against the GL context that is already current on this thread
  // -- the visible window's (render.cpp, which requests 4.5-compat explicitly for the
  // version-range check just below). There is no hidden helper window any more: one
  // window, one context, two outputs. This is also why the probe stops where it does:
  // enumerate/create/getSystem need no context and may block for seconds, everything
  // below needs the context and must therefore run here, on the render thread.

  // Extension functions aren't loader exports; resolve via xrGetInstanceProcAddr.
  // Calling this before xrCreateSession is mandatory (conformant runtimes return
  // XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING otherwise) -- which is precisely why it
  // is the FIRST thing the render thread does with a handed-over instance.
  PFN_xrGetOpenGLGraphicsRequirementsKHR get_gl_requirements = nullptr;
  if (!xr_check(out, _instance,
                xrGetInstanceProcAddr(_instance, "xrGetOpenGLGraphicsRequirementsKHR",
                                      reinterpret_cast<PFN_xrVoidFunction*>(&get_gl_requirements)),
                "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)")) {
    return false;
  }
  XrGraphicsRequirementsOpenGLKHR graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  if (!xr_check(out, _instance, get_gl_requirements(_instance, _system_id, &graphics_requirements),
                "xrGetOpenGLGraphicsRequirementsKHR")) {
    return false;
  }
  GLint gl_major = 0;
  GLint gl_minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
  glGetIntegerv(GL_MINOR_VERSION, &gl_minor);
  if (gl_major == 0) {
    // GL_MAJOR_VERSION / GL_MINOR_VERSION are 3.0+ queries. On an older context they
    // are not accepted enums: glGetIntegerv raises GL_INVALID_ENUM and leaves the
    // outputs untouched, so the naive read reports version 0.0 -- which then compares
    // "below minimum" for reasons that have nothing to do with the actual context.
    // GL_VERSION exists on every context and is specified to start with
    // "<major>.<minor>", so parse that instead. (strtol, not sscanf: MSVC treats the
    // latter as deprecated and /W3 /WX turns that into a build failure.)
    glGetError();  // Swallow the GL_INVALID_ENUM the two failed queries just raised.
    const auto* version_string = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version_string) {
      char* after_major = nullptr;
      const long parsed_major = std::strtol(version_string, &after_major, 10);
      if (after_major && *after_major == '.') {
        gl_major = static_cast<GLint>(parsed_major);
        gl_minor = static_cast<GLint>(std::strtol(after_major + 1, nullptr, 10));
      }
    }
  }
  const XrVersion gl_version =
      XR_MAKE_VERSION(static_cast<uint16_t>(gl_major), static_cast<uint16_t>(gl_minor), 0);
  const XrVersion min_version = graphics_requirements.minApiVersionSupported;
  const XrVersion max_version = graphics_requirements.maxApiVersionSupported;
  out << "OpenGL context: " << gl_major << "." << gl_minor << " (OpenXR runtime accepts "
      << XR_VERSION_MAJOR(min_version) << "." << XR_VERSION_MINOR(min_version) << " to "
      << XR_VERSION_MAJOR(max_version) << "." << XR_VERSION_MINOR(max_version) << ")"
      << std::endl;
  if (gl_major == 0) {
    out << "couldn't determine the OpenGL context version; refusing to create an "
           "OpenXR session against an unknown context"
        << std::endl;
    return false;
  }
  if (gl_version < min_version) {
    // Hard failure, where this used to print "attempting to continue" and call
    // xrCreateSession regardless. Continuing buys nothing: the runtime is entitled to
    // reject the session, and if it doesn't, the mismatch resurfaces later as a
    // swapchain or submit failure with no trace of the real cause. A clear stop here,
    // with the VR-unavailable banner main.cpp prints on a failed backend, beats a
    // confusing failure three hundred lines downstream.
    out << "OpenGL context is below the OpenXR runtime's minimum; not creating a session"
        << std::endl;
    return false;
  }
  if (gl_version > max_version) {
    // Above the ceiling is only a warning, unlike below the floor. A context newer than
    // the runtime was tested against is very likely still fine (GL compatibility
    // profiles are backward compatible), and hard-failing here would break machines
    // where this works today purely on a conservative number in the runtime's manifest.
    // Surfaced because it is the first thing to suspect if xrCreateSession then fails
    // with XR_ERROR_GRAPHICS_DEVICE_INVALID or similar.
    out << "OpenGL context is above the OpenXR runtime's maximum tested version; "
           "continuing, but this is the first thing to suspect if the session fails"
        << std::endl;
  }

  HDC hdc = wglGetCurrentDC();
  HGLRC hglrc = wglGetCurrentContext();
  if (!hdc || !hglrc) {
    out << "no current WGL context for OpenXR session" << std::endl;
    return false;
  }
  XrGraphicsBindingOpenGLWin32KHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
  graphics_binding.hDC = hdc;
  graphics_binding.hGLRC = hglrc;
  XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &graphics_binding;
  session_info.systemId = _system_id;
  if (!xr_check(out, _instance, xrCreateSession(_instance, &session_info, &_session),
                "xrCreateSession")) {
    return false;
  }

  // VIEW reference space with identity pose: a quad posed here rides the head
  // rigidly, which is the entire head-lock. No other space is ever located.
  XrReferenceSpaceCreateInfo space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  space_info.poseInReferenceSpace.orientation = {0.f, 0.f, 0.f, 1.f};
  space_info.poseInReferenceSpace.position = {0.f, 0.f, 0.f};
  if (!xr_check(out, _instance, xrCreateReferenceSpace(_session, &space_info, &_view_space),
                "xrCreateReferenceSpace")) {
    return false;
  }

  // Per-eye native resolution is what the compositor resamples best; both eyes
  // share these dimensions.
  uint32_t view_count = 0;
  if (!xr_check(out, _instance,
                xrEnumerateViewConfigurationViews(_instance, _system_id,
                                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                  &view_count, nullptr),
                "xrEnumerateViewConfigurationViews")) {
    return false;
  }
  std::vector<XrViewConfigurationView> views(view_count,
                                             XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
  if (!xr_check(out, _instance,
                xrEnumerateViewConfigurationViews(_instance, _system_id,
                                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                  view_count, &view_count, views.data()),
                "xrEnumerateViewConfigurationViews") ||
      views.empty()) {
    return false;
  }
  _width = views[0].recommendedImageRectWidth;
  _height = views[0].recommendedImageRectHeight;

  // Both target runtimes list OPAQUE first, but enumerate to stay conformant.
  uint32_t blend_mode_count = 0;
  if (!xr_check(out, _instance,
                xrEnumerateEnvironmentBlendModes(_instance, _system_id,
                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                 &blend_mode_count, nullptr),
                "xrEnumerateEnvironmentBlendModes")) {
    return false;
  }
  std::vector<XrEnvironmentBlendMode> blend_modes(blend_mode_count,
                                                  XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
  if (!xr_check(out, _instance,
                xrEnumerateEnvironmentBlendModes(_instance, _system_id,
                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                 blend_mode_count, &blend_mode_count,
                                                 blend_modes.data()),
                "xrEnumerateEnvironmentBlendModes") ||
      blend_modes.empty()) {
    return false;
  }
  _blend_mode = blend_modes[0];

  // Prefer an sRGB-typed swapchain: the pipeline writes gamma-encoded bytes. With
  // GL_FRAMEBUFFER_SRGB
  // disabled while drawing, GL stores our output verbatim and the compositor
  // decodes it as sRGB -- byte-exact match. Plain RGBA8 is treated as linear and
  // re-encoded by Quest Link, which washes the image out.
  uint32_t format_count = 0;
  if (!xr_check(out, _instance, xrEnumerateSwapchainFormats(_session, 0, &format_count, nullptr),
                "xrEnumerateSwapchainFormats")) {
    return false;
  }
  std::vector<int64_t> formats(format_count);
  if (!xr_check(out, _instance,
                xrEnumerateSwapchainFormats(_session, format_count, &format_count, formats.data()),
                "xrEnumerateSwapchainFormats") ||
      formats.empty()) {
    return false;
  }
  if (std::find(formats.begin(), formats.end(), int64_t{GL_SRGB8_ALPHA8}) != formats.end()) {
    _swapchain_format = GL_SRGB8_ALPHA8;
  } else if (std::find(formats.begin(), formats.end(), int64_t{GL_RGBA8}) != formats.end()) {
    out << "OpenXR runtime offers no GL_SRGB8_ALPHA8 swapchain; "
        << "falling back to GL_RGBA8 (colours may look washed out)" << std::endl;
    _swapchain_format = GL_RGBA8;
  } else {
    _swapchain_format = formats[0];
    out << "OpenXR runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8; "
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
    if (!xr_check(out, _instance, xrCreateSwapchain(_session, &swapchain_info, &_swapchain[eye]),
                  "xrCreateSwapchain")) {
      return false;
    }

    uint32_t image_count = 0;
    if (!xr_check(out, _instance, xrEnumerateSwapchainImages(_swapchain[eye], 0, &image_count, nullptr),
                  "xrEnumerateSwapchainImages")) {
      return false;
    }
    std::vector<XrSwapchainImageOpenGLKHR> images(
        image_count, XrSwapchainImageOpenGLKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    if (!xr_check(out, _instance,
                  xrEnumerateSwapchainImages(
                      _swapchain[eye], image_count, &image_count,
                      reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                  "xrEnumerateSwapchainImages")) {
      return false;
    }
    // Wrap each runtime-owned texture in an FBO once at init; the runtime already
    // allocated the storage, so there is no glTexImage2D here.
    for (const auto& image : images) {
      GLuint fbo;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image.image, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        out << "framebuffer failed" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        _fbo[eye].push_back(fbo);
        return false;
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      _fbo[eye].push_back(fbo);
    }
  }
  return true;
#else
  out << "OpenXR output is currently Windows-only (XR_KHR_opengl_enable Win32 binding)"
      << std::endl;
  return false;
#endif
}

XrOutput::~XrOutput()
{
  // The GL deletes below need the window's context current; the caller
  // (ScreenRenderer::detach_xr) guarantees it. Each handle is guarded so partial
  // construction -- every one of the early returns above -- tears down safely.
  for (int eye = 0; eye < 2; ++eye) {
    for (auto fbo : _fbo[eye]) {
      glDeleteFramebuffers(1, &fbo);
    }
    // The textures those FBOs wrapped are runtime-owned; destroying the swapchain
    // frees them. Never glDeleteTextures them.
    if (_swapchain[eye] != XR_NULL_HANDLE) {
      xr_check(std::cerr, _instance, xrDestroySwapchain(_swapchain[eye]), "xrDestroySwapchain");
    }
  }
  if (_view_space != XR_NULL_HANDLE) {
    xr_check(std::cerr, _instance, xrDestroySpace(_view_space), "xrDestroySpace");
  }
  if (_session != XR_NULL_HANDLE) {
    xr_check(std::cerr, _instance, xrDestroySession(_session), "xrDestroySession");
  }
  // ALWAYS, including every failed-construction path: the instance came from a probe and
  // this object is the only thing that can release it. A retained instance pins the loader
  // to the runtime that created it, which is what would defeat runtime switching (D7).
  if (_instance != XR_NULL_HANDLE) {
    xr_check(std::cerr, XR_NULL_HANDLE, xrDestroyInstance(_instance), "xrDestroyInstance");
  }
}

bool XrOutput::success() const
{
  return _success;
}

const std::string& XrOutput::log() const
{
  return _log;
}

bool XrOutput::session_running() const
{
  return _session_running;
}

uint32_t XrOutput::width() const
{
  return _width;
}

uint32_t XrOutput::height() const
{
  return _height;
}

float XrOutput::eye_spacing_multiplier() const
{
  // The "camera" here is the quad, not a runtime projection: the content spans
  // kQuadWidthMetres at kQuadDistanceMetres,
  // so its half-FOV tangent is fixed by our own geometry rather than queried.
  // eye_offset = eye_spacing_multiplier * eye_spacing_setting; at the shader's
  // near_plane=1 and nominal far_plane=129 the NDC parallax shift is
  // eye_offset / far_plane, and we want that to equal half_ipd / half_fov_tangent.
  // The 16 cancels the 1/16 default eye_spacing_setting, so the default gives
  // exactly physical parallax.
  //
  // A quad layer needs no per-runtime IPD query: the quad is a fixed virtual
  // screen, so the correct shear depends on the viewer's IPD only through the
  // nominal human average -- 64mm.
  const float half_ipd = 0.032f;
  const float half_fov_tangent = (kQuadWidthMetres / 2.f) / kQuadDistanceMetres;
  const float nominal_far_plane = 1.f + 0.5f * 256.f;
  return 16.f * nominal_far_plane * half_ipd / half_fov_tangent;
}

void XrOutput::request_detach(const char* what)
{
  // ONE line per detach, naming the leaf, and only the first time: a wedged runtime can
  // fail every call of every frame, and D3's message discipline is what keeps a console
  // left running overnight readable. From here on update() reports DetachRequested and
  // the owner tears this object down -- the app itself never exits for an XR failure.
  if (_detach_pending) {
    return;
  }
  _detach_pending = true;
  std::cerr << "OpenXR " << what
            << "; detaching the headset output (the desktop keeps playing, and trance "
               "re-attaches when the runtime returns)"
            << std::endl;
}

XrOutput::Update XrOutput::update()
{
  if (_instance == XR_NULL_HANDLE) {
    return Update::Idle;
  }
  if (_detach_pending) {
    // A frame call or a previous poll already diagnosed it and printed its line.
    return Update::DetachRequested;
  }
  for (;;) {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    auto result = xrPollEvent(_instance, &event);
    if (result == XR_EVENT_UNAVAILABLE) {
      // Normal loop exit.
      break;
    }
    if (!xr_check(std::cerr, _instance, result, "xrPollEvent")) {
      // A hard event-pump failure means we can no longer see session state
      // changes (STOPPING/EXITING/LOSS_PENDING); treat it like instance loss.
      request_detach("event polling failed");
      return Update::DetachRequested;
    }
    if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      request_detach("instance loss pending");
      return Update::DetachRequested;
    }
    if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      auto& change = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
      _session_state = change.state;
      if (change.state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo begin_info{XR_TYPE_SESSION_BEGIN_INFO};
        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        _session_running =
            xr_check(std::cerr, _instance, xrBeginSession(_session, &begin_info), "xrBeginSession");
        if (!_session_running) {
          // Runtime says READY but refuses to begin: the HMD would stay black
          // forever. Detach, same as the EXITING path.
          request_detach("session failed to begin");
          return Update::DetachRequested;
        }
      } else if (change.state == XR_SESSION_STATE_STOPPING) {
        // Mandatory acknowledgement; Quest Link sends STOPPING on doff / Link
        // close, then READY again on resume -- the app survives without restart. That
        // doff/resume machine is deliberately UNCHANGED by hot-attach: STOPPING->READY
        // stays attached-idle (the desktop paces, trap 11) and never detaches.
        //
        // A FAILED xrEndSession is different, and used to be logged and ignored: the
        // runtime has not acknowledged the stop, so the session is dead and no further
        // event will ever say so. Route it into a detach (trap 12); the probe re-attaches
        // when the runtime is healthy again.
        if (!xr_check(std::cerr, _instance, xrEndSession(_session), "xrEndSession")) {
          request_detach("session end failed");
        }
        _session_running = false;
      } else if (change.state == XR_SESSION_STATE_EXITING ||
                 change.state == XR_SESSION_STATE_LOSS_PENDING) {
        // User quit from the runtime UI (SteamVR's "quit app") / runtime dying.
        request_detach(change.state == XR_SESSION_STATE_EXITING
                           ? "session exited at the runtime's request"
                           : "session loss pending");
        return Update::DetachRequested;
      }
      // IDLE / SYNCHRONIZED / VISIBLE / FOCUSED: no action.
    }
  }
  return _session_running ? Update::Running : Update::Idle;
}

void XrOutput::render(const std::function<void(Renderer::State)>& render_fn)
{
  // Only called while update() last reported Running, which is exactly the state where
  // xrWaitFrame is the loop's SOLE pacer: phase 3 forces the window's vsync and framerate
  // limit off while the session runs (D5), and lets a minimized window skip the present
  // entirely (D6), so the desktop side is no longer a fallback pacer for anything here.
  // A path that returns without ever reaching xrWaitFrame's block therefore must not just
  // return -- otherwise a runtime that fails xrWaitFrame every iteration while posting no
  // event (a wedged Link that never says LOSS_PENDING/EXITING, so update() keeps reporting
  // Running) spins the loop at 100% CPU. Phase 3 held that off with a 10 ms sleep; trap 12
  // replaces the floor with the real answer: a failed frame call IS the detach signal a
  // dead session never posts, so it detaches and the probe re-attaches later.
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
  if (!xr_check(std::cerr, _instance, xrWaitFrame(_session, &wait_info, &frame_state),
                "xrWaitFrame")) {
    request_detach("frame wait failed");
    return;
  }
  // xrWaitFrame blocks to the runtime's pacing point -- it replaces WaitGetPoses
  // as the frame governor, including while SYNCHRONIZED/VISIBLE-but-unfocused.
  XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
  if (!xr_check(std::cerr, _instance, xrBeginFrame(_session, &begin_info), "xrBeginFrame")) {
    // Frame never began, so xrEndFrame must not be called. The xrWaitFrame above did
    // block to the runtime's pacing point, so this iteration is paced either way -- but
    // it still detaches (trap 12): a session that cannot begin a frame is dead, and
    // retrying it forever would print this line every frame and never recover.
    request_detach("frame begin failed");
    return;
  }
  // XR_FRAME_DISCARDED is a success code -- proceed, no log spam.

  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = frame_state.predictedDisplayTime;
  end_info.environmentBlendMode = _blend_mode;

  if (frame_state.shouldRender != XR_TRUE) {
    // Begin/End pair still required; pacing already came from xrWaitFrame.
    end_info.layerCount = 0;
    if (!xr_check(std::cerr, _instance, xrEndFrame(_session, &end_info), "xrEndFrame")) {
      request_detach("frame submission failed");
    }
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
    if (!xr_check(std::cerr, _instance,
                  xrAcquireSwapchainImage(_swapchain[eye], &acquire_info, &index[eye]),
                  "xrAcquireSwapchainImage")) {
      // No image acquired for this eye: index[eye] is stale, so no GL render and
      // no release for it. Any earlier eye is released below, and the frame -- which
      // was begun -- is still ended, layerlessly.
      //
      // And it detaches. This used to retry forever, which reads like the conservative
      // choice and is not: every acquire failure code (SESSION_LOST, INSTANCE_LOST,
      // CALL_ORDER_INVALID, RUNTIME_FAILURE) describes a swapchain or session that is
      // already broken, so retrying means a permanently black headset that still reports
      // itself attached, printing this line once per frame. Detaching hands it to the
      // probe, which re-attaches within seconds if the runtime is in fact healthy.
      request_detach("swapchain acquire failed");
      eyes_ready = false;
      break;
    }
    XrSwapchainImageWaitInfo image_wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    image_wait_info.timeout = XR_INFINITE_DURATION;
    if (!xr_check(std::cerr, _instance, xrWaitSwapchainImage(_swapchain[eye], &image_wait_info),
                  "xrWaitSwapchainImage")) {
      // Image never became ready: skip the render, end layerless -- and treat it as
      // FATAL. The image acquired above can only be released after a successful
      // wait, so this failure permanently consumes a swapchain slot; retrying would
      // eventually wedge every acquire (XR_ERROR_CALL_ORDER_INVALID) with the view
      // black forever. The wait was XR_INFINITE_DURATION, so a failure means the
      // session/instance is gone anyway -- detach and let the probe re-attach.
      request_detach("swapchain wait failed (the swapchain is wedged)");
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
    // the VR quads. Accumulating render-time state advances on the frame's FIRST pass
    // only and by playback elapsed time (Director's mutation epoch, trap 1), so both
    // eyes -- and the desktop pass after them -- draw identical content.
    render_fn(eye ? Renderer::State::VR_RIGHT : Renderer::State::VR_LEFT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  for (int eye = 0; eye < acquired; ++eye) {
    if (!xr_check(std::cerr, _instance, xrReleaseSwapchainImage(_swapchain[eye], &release_info),
                  "xrReleaseSwapchainImage")) {
      // FATAL, same reasoning as the wait failure above: an image that failed to
      // release is still owned by the app, so that swapchain slot is permanently
      // consumed and subsequent acquires would eventually wedge
      // (XR_ERROR_CALL_ORDER_INVALID) with the view black forever. Submitting a quad
      // sourced from an unreleased image is also invalid usage -- so drop the layers
      // and end the (already begun) frame layerlessly, then detach.
      request_detach("swapchain release failed (the swapchain is wedged)");
      eyes_ready = false;
    }
  }
  if (!eyes_ready) {
    end_info.layerCount = 0;
    if (!xr_check(std::cerr, _instance, xrEndFrame(_session, &end_info), "xrEndFrame")) {
      request_detach("frame submission failed");
    }
    return;
  }

  XrCompositionLayerQuad quads[2];
  fill_quads(quads);
  const XrCompositionLayerBaseHeader* layers[] = {
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[0]),
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[1])};
  end_info.layerCount = 2;
  end_info.layers = layers;
  if (!xr_check(std::cerr, _instance, xrEndFrame(_session, &end_info), "xrEndFrame")) {
    // A submit that failed composed nothing, so it must NOT claim the swapchains hold a
    // presentable frame (trap 12: _has_content used to be set regardless, which would let
    // the idle path re-present a frame the runtime never took).
    request_detach("frame submission failed");
    return;
  }
  // Both swapchains now hold a released image, so the idle path may re-present these
  // same layers without acquiring anything.
  _has_content = true;
}

void XrOutput::fill_quads(XrCompositionLayerQuad (&quads)[2]) const
{
  // Head-locked stereo quads: identical identity-orientation pose and size in VIEW
  // space, one per eye, differing only in eyeVisibility and source swapchain. The
  // head-lock is deliberate -- the parallax lives in the rendered content, not in
  // the layer placement, so the two quads must NOT be posed apart.
  //
  // Every field here is a constant of the configuration, which is what lets render_idle
  // rebuild the identical layers to re-present the last frame -- there is no per-frame
  // state (no swapchain image index: a quad names its swapchain, and the runtime composes
  // whichever image was most recently released from it).
  for (int eye = 0; eye < 2; ++eye) {
    auto& quad = quads[eye];
    quad = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
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
}

bool XrOutput::render_idle(bool blank)
{
  // Keep-alive for any iteration that drew nothing: a running session must keep the
  // xrWaitFrame/xrBeginFrame/xrEndFrame loop going regardless of whether the app has
  // new content, or the runtime flags it unresponsive.
  //
  // `blank` decides what the frame carries, and getting this wrong is visible:
  //   paused/hidden -> layerCount=0, which is what actually removes the content from
  //     the headset rather than freezing the last submitted frame there.
  //   between visual frames -> the SAME quads render() submitted. They name swapchains,
  //     not images, so re-presenting the most recently released image needs no acquire
  //     and no redraw. Submitting layerless frames in this case instead would blank the
  //     view on every gap between visual frames -- a strobe at (runtime rate -
  //     global_fps), which is most of the frames whenever global_fps is below the
  //     headset's refresh rate.
  if (_instance == XR_NULL_HANDLE) {
    return false;
  }
  if (!_session_running) {
    // Attached-idle: no xrWaitFrame to block on while the runtime holds the session out
    // of the running state, and this path presents nothing either -- so sleep rather
    // than spin. Unreachable through ScreenRenderer, which asks session_running() before
    // calling here and, since phase 3, hands the sub-state back to the desktop's own
    // pacing (trap 11) -- this stays as the guard that makes the contract true for any
    // caller, not as the mechanism.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
  }
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
  if (!xr_check(std::cerr, _instance, xrWaitFrame(_session, &wait_info, &frame_state),
                "xrWaitFrame")) {
    // Detach rather than sleep-and-retry (trap 12), exactly as render() does: a session
    // whose frame calls fail is dead, and nothing else will ever say so. The sleep stays
    // for this one iteration -- the detach lands on the next update() and until then
    // nothing in this loop blocks.
    request_detach("frame wait failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
  }
  XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
  if (!xr_check(std::cerr, _instance, xrBeginFrame(_session, &begin_info), "xrBeginFrame")) {
    // Frame never began, so xrEndFrame must not be called; xrWaitFrame already
    // paced this iteration.
    request_detach("frame begin failed");
    return true;
  }
  XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};
  end_info.displayTime = frame_state.predictedDisplayTime;
  end_info.environmentBlendMode = _blend_mode;
  // shouldRender false means the runtime wants nothing composed this frame anyway, and
  // _has_content false means no image has ever been released, so there is nothing to
  // re-present -- both collapse to the layerless frame.
  XrCompositionLayerQuad quads[2];
  const XrCompositionLayerBaseHeader* layers[2];
  if (!blank && _has_content && frame_state.shouldRender == XR_TRUE) {
    fill_quads(quads);
    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[0]);
    layers[1] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[1]);
    end_info.layerCount = 2;
    end_info.layers = layers;
  } else {
    end_info.layerCount = 0;
  }
  if (!xr_check(std::cerr, _instance, xrEndFrame(_session, &end_info), "xrEndFrame")) {
    request_detach("frame submission failed");
  }
  return true;
}
