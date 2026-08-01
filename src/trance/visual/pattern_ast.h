#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_AST_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_AST_H
#include <cstdint>
#include <string>
#include <vector>

// Normalized pattern AST (Framing B IR). A textual pattern parses into this, and
// the compiler (pattern_compiler.h) lowers it to a Cycler tree. The surface DSL and
// the .session proto storage are front-ends that ultimately produce a Node tree;
// keeping the IR separate from both means the compiler and the equivalence tests
// don't depend on either yet.
//
// v0 carries the SCHEDULE (timing structure) plus the debug annotations the F1
// overlay already understands (phase labels, image-lane slots). Effects are
// recorded on leaves so intent travels with the schedule, but they are NOT yet
// lowered to behaviour -- that is the next milestone (registers + VisualControl
// wiring + action-log equivalence against the live visuals).
namespace pattern
{
  enum class Slot { None, Primary, Alternate, Runtime };

  // One effect a leaf performs. The compiler turns an ordered effect list into a
  // single action lambda (see pattern_compiler / CompiledVisual).
  //
  // Beyond the direct VisualControl effects, a small bounded set of "state" effects
  // read/write named *scalar* registers (Registers::scalars). These are the only
  // mutable state the DSL has -- there are no general variables -- and they exist
  // solely so the handful of stateful hardcoded visuals (toggles, counters, captured
  // randoms) can be expressed as data. A `when` guard turns any effect into a
  // conditional one, comparing a scalar register against a literal; this is the only
  // conditional in the language.
  struct Effect
  {
    enum class Kind {
      Image, Text, Anim, Themes, Font, SpiralNew, SpiralRot, Subtext, SmallSub,
      // Scalar-register state ops:
      Set,       // scalars[target] = ivalue
      Inc,       // scalars[target] += ivalue
      Toggle,    // scalars[target] ^= 1
      Roll,      // scalars[target] = choices[random(choices.size())]
      Pulse,     // bounded counter -> one-frame flag (see fields below)
      Copy,      // images[target] = images[src]
      SpiralSet, // deterministically set spiral type (ivalue) + width (mod_literal)
      // Grammar-driven theme audio: `audio <content> [loop]` pulls a
      // precanned path from the theme's audio pool (slot semantics identical to
      // Image) and starts it playing on the engine's single dedicated theme-audio
      // channel. `force` doubles as the `loop` flag here. `audio stop` is AudioStop
      // (no fields used). See docs/spec-grammar-v3.md's audio primitive section.
      Audio,
      AudioStop
    };
    Kind kind = Kind::Image;
    Slot slot = Slot::None;
    // If non-empty, the effect's slot bool is read from scalars[slot_reg] at fire time
    // (a toggle/flag used as a primary/alternate selector) instead of `slot`.
    std::string slot_reg;
    std::string target = "current";  // image reg (Image / Copy dst) or scalar reg name
    std::string src;                 // Copy source image register
    uint32_t split = 0;              // SplitType for a Text effect
    float rate = 0.f;                // rate for SpiralRot; constant volume (0..1) for Audio
                                      // (a literal `volume` modulator sets it once at fire
                                      // time instead of every frame -- see Audio in run_effect)
    bool force = false;              // force flag for Font / SmallSub; loop flag for Audio

    // Scalar-op operands:
    int32_t ivalue = 0;              // Set value / Inc step
    std::vector<int32_t> choices;    // Roll choices
    std::string mod_reg;             // Pulse modulus register (empty => mod_literal)
    int32_t mod_literal = 0;         // Pulse modulus literal
    std::string flag;                // Pulse output flag register

    // `when` guard: run the effect only if the condition holds. Truthy tests
    // scalars[guard_reg] != 0; Eq / Ge compare it against guard_value.
    enum class Guard { None, Truthy, Eq, Ge };
    Guard guard = Guard::None;
    std::string guard_reg;
    int32_t guard_value = 0;
  };

  // One draw statement in a pattern's `render { ... }` block -- how a pattern describes
  // WHAT is drawn, as data. The render evaluator (render_eval.cpp) runs the list each
  // frame: it evaluates the [expr] params against live cycler state + registers and maps
  // the statement to a VisualRender draw call. This replaces the per-pattern C++ presets.
  struct RenderStmt
  {
    // AudioVolume: a per-frame curve-drivable theme-audio volume set,
    // the same shape as Spiral's speed axis -- `speed` field carries the [0,1]-scaled
    // expr, evaluated and applied every frame so `volume (curve ...)` reads like zoom.
    enum class Op { Image, Text, Subtext, SmallText, Spiral, Warp, AudioVolume };
    Op op = Op::Image;

    // Image op: the image register to draw (e.g. "current"). A register that was never
    // written resolves to an empty Image (used where the original drew {}).
    std::string image_reg = "current";

    // Image op animation. has_anim=false draws a still (render_image). Otherwise
    // render_animation_or_image with the type chosen each frame:
    //   anim_gate non-empty and evaluates to 0 -> NONE
    //   else Registers::anim_slot == Alternate -> ANIM_ALTERNATE
    //   else -> ANIM
    // (syntax: `anim` / `anim every Nth`)
    //
    // WHICH theme the animation comes from is deliberately NOT a param here. It is
    // whatever the last Anim effect loaded (Registers::anim_slot), because the load is
    // where the content word already lives -- `image reward ... anim`, `anim alternate`,
    // a burst's `enter { anim reward }`. A second, independent selector on the draw could
    // only ever contradict the load and pull a frame from a streamer holding something
    // else. (An `anim alt [expr]` draw-side override was declared here once and never
    // parsed, which is exactly how the draw came to ignore the load entirely.)
    bool has_anim = false;
    std::string anim_gate;  // `anim if [expr]`  -- show ANIM only when expr != 0

    // Numeric params as raw [expr] text (empty => op default). Used per op:
    //   Image:               alpha, origin (zoom_origin), zoom
    //   Text:                origin, zoom, shadow_origin, shadow_zoom
    //   Subtext / SmallText: alpha, origin
    //   Spiral:              speed (per-frame rotation advance; empty => static)
    //   AudioVolume:         speed (per-frame theme-audio volume, 0..1; empty => no-op --
    //                        a constant volume is set once at fire time instead, see Audio)
    std::string alpha, origin, zoom, shadow_origin, shadow_zoom, speed;

    // Optional `when [cond]`: draw only if the expr evaluates non-zero. Empty => always.
    std::string when;
  };

  struct Node
  {
    enum class Type { Action, Seq, Par, One, Rep, Off, Burst };
    Type type = Type::Action;

    // Stable id (optional) so a render preset can address this node through the
    // compiler's id->Cycler map (e.g. read "slow_loop"->active()).
    std::string id;

    // Debug annotations -- mirror Cycler::set_phase / set_image_slot exactly, so the
    // compiled tree drives the same overlay the hardcoded patterns do.
    std::string phase;             // optional section label, e.g. "SLOW"
    Slot image_slot = Slot::None;  // set on image-bearing leaves / lanes
    std::string image_label = "img";

    // Action leaf:
    uint32_t length = 0;
    uint32_t action_frame = 0;
    std::vector<Effect> effects;
    // Rate divider: run the effects only every Nth time the leaf fires (1 = always).
    // Bounded named state owned by the compiled action, not a user variable.
    uint32_t divide = 1;

    // Rep: repetitions in `count`. Off: offset frames in `count`. Both lower
    // children[0].
    uint32_t count = 0;

    // Burst (Type::Burst): a base loop (effects) randomly interrupted by a bounded
    // burst (burst_effects). `length` is the total; these are the burst params.
    // burst_enter_effects fire ONCE at each burst's start (the `enter { }` block) --
    // for one-shot setup like picking the burst's animation, which would re-randomize
    // every period if it lived in the per-tick burst block.
    uint32_t burst_period = 0;
    uint32_t burst_chance_den = 0;
    uint32_t burst_cooldown = 0;
    uint32_t burst_dur_min = 0;
    uint32_t burst_dur_max = 0;
    std::vector<Effect> burst_effects;
    std::vector<Effect> burst_enter_effects;

    // Seq / Par / One children; Rep / Off use children[0].
    std::vector<Node> children;
  };

  // A fully compiled pattern, ready for pattern::compile(): the lowered AST plus the
  // render block and the selection metadata (name / weight) director.cpp needs to pick
  // and label it. Filled in by patternv3::parse's ParseResult (pattern_parser_v3.h);
  // kept here rather than there since both director's built-in table and its custom-pattern
  // list store it independent of any one parser.
  struct Parsed
  {
    std::string name;
    uint32_t weight = 1;
    std::vector<RenderStmt> render_block;
    Node root;
  };
}

#endif
