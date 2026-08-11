#ifndef TRANCE_SRC_TRANCE_RENDER_OPENXR_H
#define TRANCE_SRC_TRANCE_RENDER_OPENXR_H
#include <trance/render/render.h>
#include <functional>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <openxr/openxr.h>
#pragma warning(pop)

// The headset OUTPUT of the one desktop renderer -- deliberately NOT a Renderer
// (docs/spec-xr-unified.md phase 2). There is no VR "mode" any more: ScreenRenderer owns
// the single visible window and its GL context, and holds one of these when a headset is
// attached. Everything here is the eye half of a frame -- the two per-eye swapchains, the
// head-locked quad layers, and the xrWaitFrame/xrBeginFrame/xrEndFrame handshake; the
// desktop pass is ScreenRenderer's and runs regardless of what this object did.
class XrOutput
{
public:
  // Binds to the OpenGL context that is CURRENT ON THIS THREAD when it is constructed --
  // the visible window's, since the hidden helper window is gone. The runtime keeps the
  // hDC/hGLRC handed to xrCreateSession for the whole life of the session, so the window
  // that owns them must outlive this object and must not be re-created under it (trap 4:
  // a future fullscreen/windowed toggle has to detach, recreate, then re-attach).
  XrOutput();
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
  //     must keep pacing the loop (trap 11, wired in phase 3).
  //   DetachRequested -- every failure that used to quit the app: instance loss, a
  //     wedged swapchain, event-pump death, EXITING/LOSS_PENDING. Phase 4 turns this
  //     into "tear the XR side down and keep playing on the desktop, then re-probe";
  //     until then ScreenRenderer detaches and ends the run, as VR mode did.
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
  // good -- treat it as session loss and make the next update() ask for detach.
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
