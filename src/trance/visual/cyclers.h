#ifndef TRANCE_SRC_TRANCE_VISUAL_CYCLERS_H
#define TRANCE_SRC_TRANCE_VISUAL_CYCLERS_H
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
  bool complete() const;
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

// Performs an action periodically.
class ActionCycler : public Cycler
{
public:
  // No-op action with the given length.
  ActionCycler(uint32_t length);
  // Performs the action every frame.
  ActionCycler(const std::function<void()>& action);
  // Performs the action on the first frame of every N.
  ActionCycler(uint32_t length, const std::function<void()>& action);
  // Performs the action on the Kth frame of every N.
  ActionCycler(uint32_t length, uint32_t action_frame, const std::function<void()>& action);

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  const char* type_name() const override
  {
    return "Action";
  }

private:
  uint32_t _position;
  uint32_t _length;
  uint32_t _action_frame;
  std::function<void()> _action;
};

// Performs multiple actions in parallel. The cycle is complete when all subcycles are completed
// (so the length is the maximum of the subcycle lengths).
class OneShotCycler : public Cycler
{
public:
  OneShotCycler(std::vector<Cycler*> subcycles);

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override
  {
    return "OneShot";
  }
  std::vector<const Cycler*> children() const override
  {
    std::vector<const Cycler*> r;
    for (const auto& c : _subcycles) {
      r.push_back(c.get());
    }
    return r;
  }

private:
  void calculate_active();
  std::vector<std::unique_ptr<Cycler>> _subcycles;
};

// Performs multiple actions repeatedly in parallel. The cycle is complete when all subcycles
// are completed at the same time (so the length is the least common multiple of the subcycle
// lengths).
class ParallelCycler : public Cycler
{
public:
  ParallelCycler(std::vector<Cycler*> subcycles);

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override
  {
    return "Parallel";
  }
  std::vector<const Cycler*> children() const override
  {
    std::vector<const Cycler*> r;
    for (const auto& c : _subcycles) {
      r.push_back(c.get());
    }
    return r;
  }

private:
  std::vector<std::unique_ptr<Cycler>> _subcycles;
  uint32_t _position;
  uint32_t _length;
};

// Performs multiple actions in sequence. The cycle is complete when all subcycles are completed
// (so the length is the sum of the subcycle lengths).
class SequenceCycler : public Cycler
{
public:
  SequenceCycler(std::vector<Cycler*> subcycles);
  uint32_t index() const override;

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override
  {
    return "Sequence";
  }
  std::vector<const Cycler*> children() const override
  {
    std::vector<const Cycler*> r;
    for (const auto& c : _subcycles) {
      r.push_back(c.get());
    }
    return r;
  }

private:
  void calculate_active();
  std::vector<std::unique_ptr<Cycler>> _subcycles;
};

// Performs an action repeatedly. The length is the length of the subcycle multiplied by the number
// of repetitions.
class RepeatCycler : public Cycler
{
public:
  RepeatCycler(uint32_t repetitions, Cycler* subcycle);
  uint32_t index() const override;

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override
  {
    return "Repeat";
  }
  std::vector<const Cycler*> children() const override
  {
    return {_subcycle.get()};
  }

private:
  std::unique_ptr<Cycler> _subcycle;
  uint32_t _repetitions;
  uint32_t _index;
};

// Offsets a subcycle within its period. The length is the same as that of the subcycle.
class OffsetCycler : public Cycler
{
public:
  OffsetCycler(uint32_t offset, Cycler* subcycle);

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override
  {
    return "Offset";
  }
  std::vector<const Cycler*> children() const override
  {
    return {_subcycle.get()};
  }

private:
  void advance_to_offset();
  std::unique_ptr<Cycler> _subcycle;
  uint32_t _offset;
  uint32_t _position;
};

// A base loop randomly interrupted by a bounded burst, then a cooldown -- the narrow,
// purpose-named replacement for a general state machine (see SUPER_FAST). It acts
// every `period` frames over a fixed total `length`; `index()` is 1 during a burst so
// a render preset can react. The two behaviours are supplied as callables (the base
// loop and the burst), mirroring the rest of the action-via-callback design.
class BurstCycler : public Cycler
{
public:
  struct Params {
    uint32_t length;       // total frames
    uint32_t period;       // act every N frames
    uint32_t chance_den;   // per-tick burst chance = 1/chance_den (0 = never)
    uint32_t cooldown;     // ticks of no-burst after one ends
    uint32_t dur_min;      // burst duration in ticks, inclusive range
    uint32_t dur_max;
  };
  // `enter` (optional) fires once at each burst's start, before that tick's burst
  // action -- the `enter { }` grammar block (one-shot setup, e.g. change-animation).
  BurstCycler(const Params& params, std::function<void()> base, std::function<void()> burst,
              std::function<void()> enter = {});

  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  uint32_t index() const override;  // 1 during a burst, else 0
  const char* type_name() const override
  {
    return "Burst";
  }

private:
  Params _params;
  std::function<void()> _base;
  std::function<void()> _burst;
  std::function<void()> _enter;
  uint32_t _position;
  bool _in_burst;
  uint32_t _burst_remaining;
  uint32_t _cooldown;
};

#endif