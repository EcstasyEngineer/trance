#ifndef TRANCE_SRC_TRANCE_RENDER_OPENXR_H
#define TRANCE_SRC_TRANCE_RENDER_OPENXR_H
#include <trance/render/render.h>
#include <chrono>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <openxr/openxr.h>
#pragma warning(pop)

// What a successful probe hands the render thread: an instance that has already been
// created and a system that has already answered, ready for the GL-bound half of an
// attach. OWNERSHIP OF THE INSTANCE TRANSFERS with this struct -- whoever holds it must
// destroy it (XrOutput's destructor does, on every one of its failure paths, which is
// what makes D7's no-retention rule hold even when the attach itself fails).
struct XrProbeVerdict {
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system_id = XR_NULL_SYSTEM_ID;
  // "<runtime> <major>.<minor>.<patch> (<system name>)", for the attach line.
  std::string runtime_name;
};

// The hot-attach half of the unified runtime (docs/spec-xr-unified.md D7): the thing that
// notices a headset which was not there when the app started, without ever making the
// render thread wait on the OpenXR loader. Three mechanisms, all load-bearing:
//
//   - an INLINE registry gate (one RegGetValue) every 5 s while unattached. With no
//     ActiveRuntime key registered, nothing is loaded into the process at all -- that is
//     the desktop-only machine, and probing must cost it nothing, forever.
//   - when a runtime IS registered, ONE detached worker thread, ever in flight, does
//     enumerate -> xrCreateInstance -> xrGetSystem and posts a verdict through a mutexed
//     slot. A registered-but-dead runtime (service stopped, stale registry) can block
//     those calls for seconds on DLL load and IPC timeouts; on the render thread that is
//     a periodic desktop hitch at every probe. Instance-level XR calls from a worker
//     while no other XR object exists are legal per the OpenXR threading rules, and the
//     instance handle transfers cleanly to the main thread afterwards.
//   - NO instance retention: a probe that fails destroys its instance, INCLUDING the
//     no-HMD case. The loader caches a successfully loaded runtime until a failed create
//     or xrDestroyInstance, so a retained no-HMD instance would pin the process to the
//     runtime that was active when it was made and defeat Oculus<->SteamVR switching.
//
// A hung probe cannot be killed, so the policy is explicit: no verdict within 30 s prints
// one line and stops probing for the run; the leaked thread is accepted and the desktop
// is never affected (it destroys its own instance if it ever finishes).
//
// Logging is state-change-only (D3): a leaf that is still true after 500 probes has still
// printed exactly once, so a console left running overnight stays readable.
class XrProbe
{
public:
  XrProbe();
  ~XrProbe();
  XrProbe(const XrProbe&) = delete;
  XrProbe& operator=(const XrProbe&) = delete;

  // Non-blocking; call once per main-loop iteration while unattached. Returns true when a
  // probe has succeeded and take_verdict() has an instance + system to attach to. Returns
  // false in every other case (not due yet, no runtime registered, probe still running,
  // probe failed), having printed at most one line -- and only on a state change.
  bool poll();
  // The successful verdict; only valid immediately after poll() returned true. Ownership
  // of the instance transfers to the caller.
  XrProbeVerdict take_verdict();

  // State transitions the owner performs, fed back so the log dedupe and the retry clock
  // see them. note_attach_failure takes XrOutput's captured construction log, which it
  // prints only if this is not already the state we were in.
  void note_attached();
  void note_attach_failure(const std::string& log);
  void note_detached();

  // Whether probing is possible at all (a Win32 build) and still enabled (the watchdog
  // has not given up on a hung runtime). This is the only "off" the status verb reports.
  bool probing() const;
  // One-line "why is there no headset output" for the F2 panel; empty before the first
  // verdict and while attached (the panel banner is a failure surface, #41).
  std::string status_detail() const;

private:
  // Which failure leaf we are currently sitting in -- the dedupe key for D3's
  // state-change-only logging, and what status_detail() renders. Unknown means "nothing
  // decided yet": the startup state, and where a detach puts us so the next failure of an
  // already-reported kind prints again.
  enum class State {
    Unknown,
    NoRuntime,
    Unreachable,
    NoOpenGl,
    NoHmd,
    AttachFailed,
    Attached,
    Disabled,
  };
  // The mutexed hand-off slot between the worker thread and poll(). Held by shared_ptr on
  // both sides: the worker outlives this object if it hangs, so the slot -- not the probe
  // -- is what its lifetime is tied to.
  struct Slot;

  void set_state(State state, const std::string& message);

  std::shared_ptr<Slot> _slot;  // Non-null exactly while a probe is in flight.
  std::chrono::steady_clock::time_point _next_probe;
  std::chrono::steady_clock::time_point _probe_started;
  XrProbeVerdict _verdict;
  State _state;
};

// The headset OUTPUT of the one desktop renderer -- deliberately NOT a Renderer
// (docs/spec-xr-unified.md phase 2). There is no VR "mode" any more: ScreenRenderer owns
// the single visible window and its GL context, and holds one of these when a headset is
// attached. Everything here is the eye half of a frame -- the two per-eye swapchains, the
// head-locked quad layers, and the xrWaitFrame/xrBeginFrame/xrEndFrame handshake; the
// desktop pass is ScreenRenderer's and runs regardless of what this object did.
class XrOutput
{
public:
  // Takes over a probe's instance + system (ownership transfers: this object destroys the
  // instance, whether construction succeeds or fails) and performs the GL-bound half of an
  // attach on the calling thread -- graphics requirements FIRST, which conformant runtimes
  // require before session creation, then session, swapchains, FBOs.
  //
  // Binds to the OpenGL context that is CURRENT ON THIS THREAD when it is constructed --
  // the visible window's, since the hidden helper window is gone. The runtime keeps the
  // hDC/hGLRC handed to xrCreateSession for the whole life of the session, so the window
  // that owns them must outlive this object and must not be re-created under it (trap 4:
  // a future fullscreen/windowed toggle has to detach, recreate, then re-attach).
  //
  // Nothing here writes to stderr: construction diagnostics accumulate in log() and the
  // caller decides whether to print them, because an attach that fails repeats every 5 s
  // and D3's message discipline is state-change-only.
  XrOutput(XrInstance instance, XrSystemId system_id);
  // Requires that same GL context to still be current: the FBO deletes and the
  // swapchain/session teardown are GL-bound. ScreenRenderer::detach_xr() is the only
  // caller and makes the window active first (trap 5).
  ~XrOutput();
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
  // Everything construction had to say, newline-terminated, printed by the caller: the
  // whole diagnosis on failure, the runtime/GL-version/swapchain-format notes on success.
  const std::string& log() const;

  uint32_t width() const;
  uint32_t height() const;
  float eye_spacing_multiplier() const;

  // What the event pump left the output in this iteration. Three states, because the
  // caller has three different things to do with them:
  //   Running -- the session is between xrBeginSession and xrEndSession, so eye passes
  //     are submitted and xrWaitFrame is available as a pacer.
  //   Idle -- attached, but the runtime holds the session out of the running state
  //     (pre-READY, or a STOPPING that was acknowledged on doff / Link close). Nothing
  //     may be submitted and there is NO xrWaitFrame to block on, so the desktop side
  //     must keep pacing the loop -- which is why ScreenRenderer::sync_pacing() keys the
  //     vsync/limiter restore off session_running() and not off being attached (trap 11).
  //   DetachRequested -- every failure that used to quit the app: instance loss, a
  //     wedged swapchain, event-pump death, EXITING/LOSS_PENDING, and (trap 12) a failed
  //     xrEndFrame/xrEndSession, which is otherwise a dead session nothing would ever
  //     notice. ScreenRenderer tears the XR side down with the GL context current, keeps
  //     playing on the desktop, and re-enters probing -- trance never exits for an XR
  //     failure (D7).
  enum class Update {
    Running,
    Idle,
    DetachRequested,
  };
  Update update();
  // Whether the last update() left the session between xrBeginSession and xrEndSession,
  // i.e. whether frames may (and must) be submitted at all. The Running-vs-Idle half of
  // update()'s answer, asked at render time.
  bool session_running() const;

  // The two eye passes of one frame: acquire/wait/render/release both swapchains, then
  // submit the pair of head-locked quads. Only legal while update() last said Running.
  void render(const std::function<void(Renderer::State)>& render_fn);
  // Keep-alive frame submission for an iteration that rendered no eye content.
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
  // Returns true if it performed its own frame pacing (so the caller must not add an
  // anti-spin sleep).
  bool render_idle(bool blank);

private:
  // The two head-locked quad layers, rebuilt identically by render() and render_idle().
  // Nothing in them varies per frame, which is precisely why the idle path can
  // re-present the last rendered frame without touching a swapchain.
  void fill_quads(XrCompositionLayerQuad (&quads)[2]) const;
  // Everything the constructor does after the probe's instance/system, writing its
  // diagnostics to `out` rather than stderr. Split out so the log can be captured whole.
  bool build(std::ostream& out);
  // The single point where a failure becomes a detach (D7, trap 12). Prints ONE line --
  // `what` names the leaf, so every failure kind stays distinguishable -- and only the
  // first time, since a wedged runtime can fail every call of every frame. update()
  // reports DetachRequested from then on.
  void request_detach(const char* what);

  bool _success;
  std::string _log;
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
  // Latched by request_detach(): from here on update() reports DetachRequested and the
  // owner tears this object down. Set by session/instance loss, a wedged swapchain, an
  // event-pump death, EXITING -- and by any failed frame call, because a session whose
  // xrEndFrame fails forever is dead but posts no event to say so (trap 12).
  bool _detach_pending;
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
