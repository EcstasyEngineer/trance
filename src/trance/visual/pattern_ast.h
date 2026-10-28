#ifndef TRANCE_SRC_TRANCE_VISUAL_PATTERN_AST_H
#define TRANCE_SRC_TRANCE_VISUAL_PATTERN_AST_H
#include <cstdint>
#include <string>
#include <vector>

// Native execution IR: the V4 AST frontend lowers authored compositions to schedules and ordered
// render statements. CompiledVisual binds effects to image captures and runtime
// actions; the render evaluator reads occurrence clocks and internal state.
// Debug annotations expose that same tree to the F1 overlay.
namespace pattern
{
  // Which of the two live ThemeBank sides a content word selects. `Secondary` is theme 1;
  // it is deliberately NOT spelled "Alternate", because the grammar's `alternate` content
  // word means something else entirely (deterministic A/B ping-pong between the two sides).
  // The ThemeBank/api boundary below this still speaks `bool alternate` by design.
  enum class Slot { None, Primary, Secondary };

  // One effect a leaf performs. The compiler turns an ordered effect list into a
  // single action lambda (see pattern_compiler / CompiledVisual).
  //
  // Beyond the direct VisualControl effects, a small bounded set of "state" effects
  // read/write named *scalar* registers (Registers::scalars). These are the only
  // mutable scalar state in the runtime. The parser generates them for `chance`,
  // `alternate`, and animation accents; these are never user variables.
  // Guards condition effect execution; render conditions separately
  // control visibility.
  struct Effect
  {
    enum class Kind { Image, Text, Anim, Subtext, SmallSub, Toggle, Roll, Pulse, Chance };
    Kind kind = Kind::Image;
    Slot slot = Slot::None;
    // If non-empty, the effect's slot bool is read from scalars[slot_reg] at fire time
    // (a toggle/flag used as a primary/alternate selector) instead of `slot`.
    std::string slot_reg;
    std::string target = "current";  // generated image capture or scalar-state identity
    uint32_t split = 0;              // SplitType for a Text effect
    bool force = false;              // refresh caption selection

    // Internal selection and animation-accent state.
    std::vector<int32_t> choices;    // Roll choices
    int32_t mod_literal = 0;         // Pulse modulus
    std::string flag;                // Pulse output flag register

    // Conditional selection is gated by an internal probability result.
    enum class Guard { None, Truthy };
    Guard guard = Guard::None;
    std::string guard_reg;
    uint32_t probability_numerator = 0;
    uint32_t probability_denominator = 1;
  };

  // One compiled output statement. The frontend generates parameters from typed
  // AST values/curves; they are not an authored expression language. The evaluator
  // maps them against occurrence clocks and capture state to a VisualRender call.
  struct RenderStmt
  {
    enum class Op { Image, Text, Subtext, SmallText, Spiral };
    Op op = Op::Image;

    // Image op: the image register to draw (e.g. "current"). A register that was never
    // written resolves to an empty Image (used where the original drew {}).
    std::string image_reg = "current";

    // Image animation is gated per output and bound to its captured theme.
    bool has_anim = false;
    // V4 images bind animation rendering to their own captured theme side.
    // The frontend supplies the owning image capture identity.
    std::string anim_image_reg;
    std::string anim_gate;  // `anim if [expr]`  -- show ANIM only when expr != 0

    // Numeric params as raw [expr] text (empty => op default). Used per op:
    //   Image:               alpha, origin (zoom_origin), zoom
    //   Text:                origin, zoom, shadow_origin, shadow_zoom
    //   Subtext / SmallText: alpha, origin
    //   Spiral:              speed (per-frame rotation advance; empty => static)
    std::string alpha, origin, zoom, shadow_origin, shadow_zoom, speed;

    // Optional `when [cond]`: draw only if the expr evaluates non-zero. Empty => always.
    std::string when;
  };

  struct Node
  {
    // Stable id (optional) so a render preset can address this node through the
    // compiler's id->Cycler map (e.g. read "slow_loop"->active()).
    std::string id;

    // Debug annotations -- mirror Cycler::set_phase / set_image_slot exactly, so the
    // compiled tree drives the same overlay the hardcoded patterns do.
    std::string phase;             // optional section label, e.g. "SLOW"
    Slot image_slot = Slot::None;  // set on image-bearing leaves / lanes
    std::string image_label = "img";

    // Occurrence duration. Effects fire once at occurrence entry.
    uint32_t length = 0;
    std::vector<Effect> effects;
    // V4 native occurrence scheduling. Zero length inherits the enclosing span.
    enum class Composition { Parallel, Sequential, Random };
    Composition occurrence_kind = Composition::Parallel;
    enum class ChildMode { Independent, Loop, Wait };
    ChildMode child_mode = ChildMode::Independent;
    uint32_t duration_max = 0, duration_step = 1;
    bool repeat_forever = false;
    uint32_t repeat_count = 1;
    uint32_t offset = 0, cadence = 0;
    uint32_t cadence_ramp_from = 0, cadence_ramp_to = 0, cadence_ramp_steps = 0;
    int cadence_ramp_ease = 0; // 0 linear, 1 early, 2 late.
    bool select_random = true, select_allow_self = false;
    uint32_t select_first = 0, select_cooldown = 0;
    struct Exit {
      bool enabled = false;
      uint32_t numerator = 0, denominator = 1, check_every = 1;
    };
    std::vector<Exit> child_exits;

    // Ordered children interpreted by occurrence_kind.
    std::vector<Node> children;
  };

  // A fully compiled pattern, ready for pattern::compile(): the lowered AST plus the
  // render block and the selection metadata (name / weight) director.cpp needs to pick
  // and label it. Filled in by the V4 frontend's neutral ParseResult;
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
