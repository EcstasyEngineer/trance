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
class XrOutput;
class XrProbe;
struct XrProbeVerdict;

GLuint compile(const std::string& vertex_text, const std::string& fragment_text);
void init_glew();

// glReadPixels the window's currently-bound back buffer (row-flipped for sf::Image)
// and write it to `path`. Call from a pre-display hook so it sees the fully composited
// frame -- works even when the physical display is locked or there's no compositor to
// grab from. Logs and returns false on write failure.
bool save_window_screenshot(sf::RenderWindow& window, const std::string& path);

// The renderer: it owns the one visible window and its GL context, and optionally an
// XrOutput fed from that same context (docs/spec-xr-unified.md sec 1). There is no
// renderer selection and no VR mode -- attaching a headset adds two passes to the frame,
// it does not replace the window. There is likewise no renderer INTERFACE any more: this
// class used to derive from an abstract `Renderer` that existed to let a VR mode swap
// itself in for the window, and phase 5 collapsed the two now that the headset is an
// output of this one class rather than an alternative to it.
class ScreenRenderer
{
public:
  // TODO: could factor out actual rendering to intermediate texture(s) and add multisampling?
  // NONE is the flat desktop pass, run on every frame the window is presented at all (see
  // desktop_pass_due for the one thing that skips it); VR_LEFT/VR_RIGHT are the two eye
  // passes that precede it while a headset is attached, drawn into the OpenXR output's two
  // per-eye swapchains. There is no mono VR pass.
  enum class State {
    NONE = 0,
    VR_LEFT = 1,
    VR_RIGHT = 2,
  };

  // `start_hidden` (--hidden, #58): create AND LEAVE the window unmapped, so a run that
  // begins in silent running never puts a frame on screen. Everything else is unchanged --
  // the GL context, the XR probe and the frame loop all run exactly as when visible.
  ScreenRenderer(const trance_pb::System& system, const OverlayConfig& overlay = {},
                 bool start_hidden = false);
  ~ScreenRenderer();
  ScreenRenderer(const ScreenRenderer&) = delete;
  ScreenRenderer& operator=(const ScreenRenderer&) = delete;

  sf::RenderWindow& window();

  // Where the XR side is right now, for the `status` verb (the QA observability hook,
  // spec phase 4) -- one of:
  //   "off"           -- probing is impossible (no XR backend in this build) or has been
  //                      given up on for the run (the probe watchdog fired).
  //   "unattached"    -- no headset output; the background probe is looking for one.
  //   "attached"      -- a headset is attached and its session is running (XR paces).
  //   "attached-idle" -- attached, session not running (pre-READY, or STOPPING
  //                      acknowledged on doff / Link close); the desktop paces.
  const char* xr_status() const;
  // One line for the F2 panel's persistent banner (#41): why there is no headset output
  // right now, or empty while attached / before the first probe has decided anything.
  // Live, not latched at startup, because attach is now a background probe: a run that
  // starts with no runtime and ends up attached must not keep claiming otherwise.
  std::string xr_status_detail() const;

  // Decides -- and LATCHES, for the next render() -- whether that frame's desktop (NONE)
  // pass and its buffer swap run at all (D6, trap 13). They are skipped only while the
  // window is minimized AND the headset is pacing the loop: the point of the skip is that
  // a minimized window must not be able to hold the headset back, and where there is no
  // headset the desktop present is the loop's ONLY pacer, so skipping it would replace a
  // blocked swap with a hot spin. Everything else about the frame is unaffected -- the eye
  // passes, the event pump, audio and the content tick all run exactly as when visible.
  //
  // `force` is a pending screenshot (trap 15): the verb has already been acknowledged and
  // is consumed by the pre-display UI hook, which only runs inside the desktop pass, so
  // without this an acknowledged screenshot would hang forever behind a minimized window.
  //
  // LATCHED rather than re-asked inside render() because the caller has to make the same
  // decision for the ImGui frame: starting a frame the desktop pass never renders leaves
  // ImGui's NewFrame/Render pairing broken. One answer, one iteration, both users.
  bool desktop_pass_due(bool force);

  // An XR output is attached, i.e. a headset is being fed alongside the window. NOT a
  // mode: the desktop pass runs either way, and every per-pass question (image scale,
  // text targets, eye shear) asks Director::vr_pass() about the CURRENT pass instead.
  bool vr_enabled() const;
  // Dimensions of the pass currently being rendered -- the eye swapchain's during
  // VR_LEFT/VR_RIGHT, the window's during NONE (and whenever no pass is running, which
  // is what startup sizing reads). With both outputs live these genuinely differ per
  // pass, so nothing may cache them across a frame (trap 2).
  uint32_t view_width() const;
  uint32_t width() const;
  uint32_t height() const;
  // The largest height any pass of a frame can have. For the one resource that must be
  // sized ONCE, before any pass exists, and therefore cannot follow the per-pass
  // dimensions above: the font atlas (trap 2's first enumerated site). height() answers
  // for the CURRENT pass, so at Director-construction time -- no pass running -- it can
  // only report the window, which is right for the desktop and wrong for the headset: an
  // atlas rasterized for a 1080p window is drawn ~2x upscaled on a 2208px Quest 3 eye and
  // reads soft. Taking the max costs nothing detached and only glyph memory attached,
  // since render_text scales the string to a fraction of the view either way.
  //
  // KNOWN GAP, unpaid by phase 4 (which introduced the late attach and therefore owed
  // this): this is read ONCE, at Director construction. With hot-attach the headset can
  // arrive minutes later, and it then draws text from an atlas rasterized for the window
  // -- soft, exactly the way the pre-max_height code was on every VR run. Paying it means
  // rebuilding the FontCache on attach, which needs VisualApiImpl to retain the session +
  // cache-size it was built with and adds a synchronous font preload to the attach hitch
  // budget (D7); both belong with a phase that is allowed to touch the visual pipeline.
  // A run that starts with the headset already attached is unaffected.
  uint32_t max_height() const;
  float eye_spacing_multiplier() const;

  void init();
  // Per-iteration event pump: the XR event queue while a headset is attached, the
  // hot-attach probe while one is not. Nothing it can find ends the run -- every XR
  // failure detaches and keeps playing on the desktop (D7) -- which is why it returns
  // nothing to check.
  void update();
  // Renders one frame: the eye passes (when a headset is attached and its session is
  // running) followed ALWAYS by the desktop pass -- no XR failure or early-out may skip
  // the latter (trap 10). `blank` means the content must not be VISIBLE in the headset
  // this frame (paused/hidden): the eye submission goes layerless so the headset empties
  // instead of freezing, while the desktop pass repaints as usual so the F2 panel stays
  // live (D8, trap 9).
  void render(const std::function<void(State)>& render_fn, bool blank);

  // Frame-loop keep-alive for any iteration where the main loop drew nothing -- paused,
  // hidden, or simply between visual frames. Returns true if the renderer performed its
  // own frame pacing (so the caller must not add an anti-spin sleep); with no headset
  // attached there is nothing to keep alive and it returns false.
  //
  // WHY it must run even when not paused: a VR frame loop is a handshake with the
  // runtime, not a consequence of having something new to draw. A running OpenXR session
  // REQUIRES continuous xrWaitFrame/xrBeginFrame/xrEndFrame (the spec asks applications
  // to keep the loop running "to maintain synchronisation", calling xrEndFrame with no
  // layers if need be). Stalling it -- which is what happens if this is gated on a visual
  // frame being due at global_fps -- makes the runtime flag the app unresponsive and
  // drops the headset to the grey void.
  //
  // `blank` carries the same meaning as render()'s: paused/hidden submits layerlessly,
  // merely-between-visual-frames re-presents the last quads (see XrOutput::render_idle).
  bool render_idle(bool blank);

  // Pre-display UI hook: runs after the scene is drawn but BEFORE the buffer swap, so a
  // 2D UI (the F2 ImGui panels) composites onto the same frame it belongs to. Calling
  // display() again outside render() instead double-swaps: the UI lands on the previous
  // frame's back buffer, strobing the UI at half rate and ping-ponging the scene one
  // frame back every other swap. It runs in the desktop (NONE) pass only -- the eye
  // passes are per-eye targets with no flat 2D surface to composite onto, which is also
  // why the F2 panel and the F1 HUD never appear in the headset.
  void set_ui_hook(std::function<void()> hook) { _ui_hook = std::move(hook); }

private:
  // Take over a probe's instance + system and stand the headset output up on this
  // window's GL context: the GL-bound half of an attach (graphics requirements, session,
  // swapchains, FBOs), which is why it runs here and not on the probe thread. Returns
  // false having destroyed the instance if the session could not be created; the window
  // plays on regardless, and the probe keeps looking.
  bool attach_xr(const XrProbeVerdict& verdict);
  // Tear the XR side down with this window's GL context current (trap 5): the runtime
  // holds the hDC/hGLRC from the graphics binding, and the FBO deletes plus the
  // swapchain/session teardown are all GL-bound. Same ordering at detach time as at
  // process exit -- which is why the destructor routes through here too. Detach is never
  // an exit: the desktop keeps playing and probing resumes (D7).
  void detach_xr();

  // The one condition under which xrWaitFrame is the loop's pacer (D5): a headset is
  // attached AND its session is actually running. Attached-idle -- pre-READY, or a
  // STOPPING acknowledged on doff / Link close -- has no xrWaitFrame to block on and so
  // still needs the desktop's pacing (trap 11), which is exactly why this asks
  // session_running() rather than just whether an XrOutput exists.
  bool xr_paces() const;
  // Push the pacing that xr_paces() implies onto the window, if it isn't already there:
  // forced off while XR paces, restored to the system.json-derived values otherwise.
  // Called on every state transition that can change the answer -- attach, detach, and
  // each update() (which is where running<->idle flips).
  void sync_pacing();

  // DECLARED FIRST so it is destroyed LAST: the XR teardown below is GL-bound, and the
  // context it needs is this window's (trap 5). Member order is the half of that ordering
  // the compiler enforces; the destructor supplies the other half by making the context
  // current before it lets go of _xr.
  std::unique_ptr<sf::RenderWindow> _window;
  std::function<void()> _ui_hook;

  // Held by pointer through an incomplete type on purpose: openxr.h needs
  // ScreenRenderer::State for its per-eye render callback, so it includes this header --
  // and a member of a forward-declared class is what keeps that from becoming a cycle. The
  // destructor, attach and detach all live in render.cpp, which has the full definition.
  std::unique_ptr<XrOutput> _xr;
  // The hot-attach probe (D7), polled from update() whenever _xr is null: a registry read
  // inline every 5 s, and one detached worker thread at a time when a runtime is actually
  // registered. Always present, even where XR cannot work at all -- it answers "off" then,
  // which is what the status verb reports.
  std::unique_ptr<XrProbe> _probe;
  // The pass being rendered right now, so the per-pass dimension accessors above can
  // answer for it. NONE outside render(), which is what makes startup sizing read the
  // window.
  State _pass = State::NONE;
  // The desktop pacing this window was configured with at startup, kept so it can be put
  // back on detach and in attached-idle. Read from system.json once, in the constructor,
  // rather than re-derived later: display_refresh_hz() is a live mode-table read and a
  // mid-run refresh change must not silently become a different restore value than the
  // one the run started with.
  bool _vsync;
  uint32_t _framerate_limit;
  // Whether the pacing currently pushed onto the window is the XR one (forced off).
  // Starts false: the constructor applies the desktop pacing itself.
  bool _xr_pacing = false;
  // desktop_pass_due()'s latched answer, consumed by render(). True by default so a
  // render() that was never preceded by the query behaves exactly as it always did.
  bool _desktop_pass = true;
  // --hidden (#58): suppresses the one setVisible(true) in init(), so a run that starts in
  // silent running never maps the window at all. Read once, in init(); every later
  // hide/show goes through main.cpp's seam, which owns the window's visibility from then on.
  bool _start_hidden = false;
};

#endif