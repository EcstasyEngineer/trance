#ifndef TRANCE_SRC_TRANCE_VISUAL_CYCLERS_H
#define TRANCE_SRC_TRANCE_VISUAL_CYCLERS_H
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <trance/visual/pattern_ast.h>

// Which theme slot an image-bearing cycler sources from, for the debug overlay.
// Primary = slot[1], Alternate = slot[2], Runtime = decided by hidden state at
// fire time (not statically known). None = not an image-bearing node.
enum class ImageSlotHint : uint8_t { None, Primary, Alternate, Runtime };

// Interface for constructing visualiser patterns.
class Cycler
{
public:
  Cycler();
  virtual ~Cycler() = default;

  // The length of the cycle; i.e. how many times advance() must be called until complete() returns
  // true.
  virtual uint32_t length() const = 0;
  // Completed traversal duration, for director selection policy. Usually equal
  // to length(); repeated occurrences keep a separate local clock.
  virtual uint32_t elapsed_length() const { return length(); }
  // The current cycle head position.
  virtual uint32_t position() const = 0;
  // Reset the cycle head to the begininning of the sequence.
  virtual void reset() = 0;
  // Invoke actions under the cycle head and advance it. If the cycle head is currently at the end
  // of the sequence already, the position is reset first.
  virtual void advance(bool trigger_actions = true) = 0;

  // Sets a flag indicating whether the cycle is currently active.
  virtual void activate(bool active);

  // Whether the cycle is currently active.
  bool active() const;
  // Whether the cycle head is at the end of the sequence.
  virtual bool complete() const;
  // The current frame of the cycler. This is position() - 1 mod length().
  uint32_t frame() const;
  // Current frame, as a float, scaled between 0 and 1.
  float progress() const;

  // Debug introspection: a short human name for the cycler type, and the
  // immediate subcycles (empty for leaf cyclers). Used by the debug overlay to
  // render the cycler tree, which is what actually distinguishes one visual
  // pattern from another.
  virtual const char* type_name() const
  {
    return "Cycler";
  }
  virtual std::vector<const Cycler*> children() const
  {
    return {};
  }
  // Repetition / sequence index for Rep / Seq nodes; 0 for leaf / parallel nodes.
  // Lets the render evaluator read loop/segment position through a base Cycler* held in
  // a node-id map (see pattern_compiler / render_eval).
  virtual uint32_t index() const
  {
    return 0;
  }

  // Optional human-readable "section" label for the debug overlay (e.g. "SLOW",
  // "FAST", "INTERLEAVE"). Empty by default; set at construction on the handful of
  // subtrees that correspond to a section a viewer would recognise. The overlay
  // reports the deepest active labelled node as the current section. Pure
  // annotation -- it never affects scheduling.
  void set_phase(const char* phase);
  const std::string& phase() const;

  // Mark this node as the lane/leaf that sources a displayed image, and from which
  // theme slot. Set only on image-bearing nodes (not spiral/text/font/timers). The
  // overlay reports the slot of the active image lanes as the theme(s) "on screen".
  void set_image_slot(ImageSlotHint hint, const char* label = "img");
  ImageSlotHint image_slot() const;
  const std::string& image_label() const;

private:
  bool _active;
  std::string _phase;
  ImageSlotHint _image_slot = ImageSlotHint::None;
  std::string _image_label;
};

// V4's three compositions share one occurrence scheduler. Children are advanced
// only by their owner, so completion never silently becomes implicit repetition.
class OccurrenceCycler final : public Cycler
{
public:
  OccurrenceCycler(pattern::Node spec, std::function<void()> entry,
                   std::vector<std::unique_ptr<Cycler>> children);
  uint32_t length() const override;
  uint32_t position() const override;
  bool complete() const override;
  uint32_t elapsed_length() const override;
  uint32_t index() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override;
  std::vector<const Cycler*> children() const override;
  Cycler* local_clock();
  // Called on activation so inherited duration uses the available owner window.
  void prepare(uint32_t inherited_span);

private:
  class LocalClock final : public Cycler {
  public:
    explicit LocalClock(OccurrenceCycler& owner) : _owner(owner) {}
    uint32_t length() const override;
    uint32_t position() const override;
    void reset() override {}
    void advance(bool = true) override {}
  private:
    OccurrenceCycler& _owner;
  };
  void begin_iteration();
  void begin_body();
  void start_child(uint32_t child);
  void run_body(bool trigger_actions);
  uint32_t body_remaining() const;
  bool has_next_iteration() const;
  pattern::Node _spec;
  std::function<void()> _entry;
  std::vector<std::unique_ptr<Cycler>> _children;
  LocalClock _local;
  std::vector<uint32_t> _ramp;
  uint32_t _span = 1, _position = 0, _iteration = 0;
  uint32_t _elapsed = 0;
  uint32_t _body_length = 1, _body_position = 0, _segment = 0;
  uint32_t _selected = 0, _cooldown_until = 0, _inherited = 1;
  bool _prepared = false, _body_started = false, _done = false, _iteration_done = false;
};

#endif
