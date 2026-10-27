#pragma once
// Historical schedule oracle; excluded from the product.
#include <trance/visual/cyclers.h>

// Performs an action periodically.
class ActionCycler : public Cycler
{
public:
  // No-op action with the given length.
  ActionCycler(uint32_t length);
  // Performs the action on the first frame of every N (length 1 = every frame).
  ActionCycler(uint32_t length, const std::function<void()>& action);

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

// A timed occurrence. Entry effects fire once; child clocks run only while this
// occurrence advances and reset at its boundary. Patterns/cadences use a fixed
// length; a burst owner supplies a newly sampled duration on restart.
class PhaseCycler : public Cycler
{
public:
  PhaseCycler(uint32_t length, std::function<void()> entry, std::vector<Cycler*> children,
              bool sequence = false);
  uint32_t length() const override;
  uint32_t position() const override;
  uint32_t index() const override;
  void reset() override;
  void restart(uint32_t length);
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  const char* type_name() const override { return "Phase"; }
  std::vector<const Cycler*> children() const override;

private:
  uint32_t _length;
  uint32_t _position = 0;
  bool _sequence;
  std::function<void()> _entry;
  std::vector<std::unique_ptr<Cycler>> _children;
};

// A base phase interrupted by a sampled-duration phase. Only the active phase
// advances. index() remains the controller's 0/1 state; its progress spans the
// enclosing pattern. Child Phase clocks measure their own activations.
class BurstCycler : public Cycler
{
public:
  struct Params {
    uint32_t length;
    uint32_t period;
    uint32_t chance_den;  // 0 = never
    uint32_t cooldown;    // period ticks after burst exit
    uint32_t dur_min;     // inclusive duration range in period ticks
    uint32_t dur_max;
  };
  BurstCycler(const Params& params, PhaseCycler* base, PhaseCycler* burst);
  uint32_t length() const override;
  uint32_t position() const override;
  void reset() override;
  void advance(bool trigger_actions = true) override;
  void activate(bool active) override;
  uint32_t index() const override;
  const char* type_name() const override { return "Burst"; }
  std::vector<const Cycler*> children() const override;

private:
  Params _params;
  std::unique_ptr<PhaseCycler> _base;
  std::unique_ptr<PhaseCycler> _burst;
  uint32_t _position = 0;
  bool _in_burst = false;
  bool _started = false;
  uint32_t _cooldown = 0;
};

