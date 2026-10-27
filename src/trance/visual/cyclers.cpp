#include <trance/visual/cyclers.h>
#include <common/util.h>
#include <algorithm>
#include <utility>
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>

Cycler::Cycler() : _active{true}
{
}

void Cycler::activate(bool active)
{
  _active = active;
}

bool Cycler::active() const
{
  return _active;
}

void Cycler::set_phase(const char* phase)
{
  _phase = phase;
}

const std::string& Cycler::phase() const
{
  return _phase;
}

void Cycler::set_image_slot(ImageSlotHint hint, const char* label)
{
  _image_slot = hint;
  _image_label = label;
}

ImageSlotHint Cycler::image_slot() const
{
  return _image_slot;
}

const std::string& Cycler::image_label() const
{
  return _image_label;
}

bool Cycler::complete() const
{
  return position() == length();
}

uint32_t Cycler::frame() const
{
  auto p = position();
  return p ? p - 1 : length() - 1;
}

float Cycler::progress() const
{
  return static_cast<float>(frame()) / length();
}

OccurrenceCycler::OccurrenceCycler(const pattern::Node& spec, std::function<void()> entry,
                                   std::vector<Cycler*> children)
  : _spec(spec), _entry(std::move(entry)), _local(*this)
{
  _spec.children.clear();
  for (auto* child : children) _children.emplace_back(child);
  _span = std::max(1u, spec.length);
  activate(false);
}

uint32_t OccurrenceCycler::length() const { return _span; }
uint32_t OccurrenceCycler::position() const { return _position; }
bool OccurrenceCycler::complete() const { return _done; }
uint32_t OccurrenceCycler::elapsed_length() const { return _elapsed; }
uint32_t OccurrenceCycler::index() const
{
  return _spec.occurrence_kind == pattern::Node::Composition::Parallel ? _iteration : _selected;
}
const char* OccurrenceCycler::type_name() const
{
  switch (_spec.occurrence_kind) {
  case pattern::Node::Composition::Parallel: return "Parallel";
  case pattern::Node::Composition::Sequential: return "Sequential";
  case pattern::Node::Composition::Random: return "Random";
  }
  return "Occurrence";
}
std::vector<const Cycler*> OccurrenceCycler::children() const
{
  std::vector<const Cycler*> out;
  for (const auto& child : _children) out.push_back(child.get());
  return out;
}
Cycler* OccurrenceCycler::local_clock() { return &_local; }
uint32_t OccurrenceCycler::LocalClock::length() const { return _owner._body_length; }
uint32_t OccurrenceCycler::LocalClock::position() const { return _owner._body_position; }

void OccurrenceCycler::activate(bool value)
{
  Cycler::activate(value);
  _local.activate(value);
  if (!value) for (auto& child : _children) child->activate(false);
}
void OccurrenceCycler::reset()
{
  _position = _iteration = _body_position = _segment = _selected = _cooldown_until = 0;
  _elapsed = 0;
  _prepared = _body_started = _done = _iteration_done = false;
  for (auto& child : _children) child->reset();
  activate(false);
}
bool OccurrenceCycler::has_next_iteration() const
{
  return _spec.repeat_forever || _iteration + 1 < _spec.repeat_count;
}

void OccurrenceCycler::prepare(uint32_t inherited_span, bool trigger_actions)
{
  reset();
  _inherited = std::max(1u, inherited_span);
  begin_iteration(trigger_actions);
  _prepared = true;
}

void OccurrenceCycler::begin_iteration(bool trigger_actions)
{
  _position = 0;
  _iteration_done = false;
  _span = _spec.length ? _spec.length : _inherited;
  if (trigger_actions && _spec.duration_max > _span) {
    const uint32_t step = std::max(1u, _spec.duration_step);
    _span += step * random(1u + (_spec.duration_max - _span) / step);
  }
  _span = std::max(1u, _span);
  _ramp.clear();
  if (_spec.cadence_ramp_steps) {
    const uint32_t steps = _spec.cadence_ramp_steps;
    if (steps > _span) throw std::invalid_argument("ramp steps exceed occurrence duration");
    std::vector<double> raw(steps);
    double sum = 0;
    for (uint32_t i = 0; i < steps; ++i) {
      double p = (double(i) + .5) / steps;
      if (_spec.cadence_ramp_ease == 1) p = 1 - std::pow(1 - p, 3);
      else if (_spec.cadence_ramp_ease == 2) p = p * p * p;
      raw[i] = _spec.cadence_ramp_from +
          (double(_spec.cadence_ramp_to) - _spec.cadence_ramp_from) * p;
      sum += raw[i];
    }
    for (const auto value : raw)
      _ramp.push_back(std::max(1u, static_cast<uint32_t>(std::lround(value * _span / sum))));
    int64_t diff = int64_t(_span) - std::accumulate(_ramp.begin(), _ramp.end(), int64_t{0});
    uint32_t i = steps - 1;
    while (diff) {
      if (diff > 0) { ++_ramp[i]; --diff; }
      else if (_ramp[i] > 1) { --_ramp[i]; ++diff; }
      i = i ? i - 1 : steps - 1;
    }
  }
  _segment = 0;
  _body_length = !_ramp.empty() ? _ramp.front() : _spec.cadence ? _spec.cadence : _span;
  _body_position = _spec.offset ? (_body_length - _spec.offset % _body_length) % _body_length : 0;
  _body_started = false;
  _cooldown_until = 0;
  _selected = _spec.select_first;
  for (auto& child : _children) { child->reset(); child->activate(false); }
}

uint32_t OccurrenceCycler::body_remaining() const
{
  return std::min(_body_length - _body_position, _span - _position);
}
void OccurrenceCycler::start_child(uint32_t child, bool trigger_actions)
{
  auto* value = _children.at(child).get();
  if (auto* occurrence = dynamic_cast<OccurrenceCycler*>(value))
    occurrence->prepare(std::max(1u, body_remaining()), trigger_actions);
  else value->reset();
}
void OccurrenceCycler::begin_body(bool trigger_actions)
{
  const uint32_t phase = _body_position;
  if (phase > 1000000) throw std::invalid_argument("offset phase exceeds schedule preroll budget");
  _body_position = 0;
  _selected = _spec.select_first;
  _cooldown_until = 0;
  const bool enter_actions = trigger_actions && !phase;
  if (_spec.occurrence_kind == pattern::Node::Composition::Parallel) {
    for (uint32_t i = 0; i < _children.size(); ++i) start_child(i, enter_actions);
  } else if (!_children.empty()) start_child(_selected, enter_actions);
  _body_started = true;
  // A phase shift advances clocks without actions or random choices.
  for (uint32_t i = 0; i < phase; ++i) {
    run_body(false);
    ++_body_position;
  }
}

void OccurrenceCycler::run_body(bool trigger_actions)
{
  using Composition = pattern::Node::Composition;
  for (auto& child : _children) child->activate(false);
  if (trigger_actions && !_body_position && _entry) _entry();
  if (_spec.occurrence_kind == Composition::Parallel) {
    for (auto& child : _children) if (!child->complete()) {
      child->activate(true);
      child->advance(trigger_actions);
    }
    return;
  }
  if (_children.empty()) return;
  if (_spec.occurrence_kind == Composition::Sequential) {
    if (_children[_selected]->complete()) {
      if (_selected + 1 == _children.size()) return;
      start_child(++_selected, trigger_actions);
    }
  } else {
    bool transition = _children[_selected]->complete();
    if (!transition && trigger_actions && _selected < _spec.child_exits.size()) {
      const auto& exit = _spec.child_exits[_selected];
      if (exit.enabled && _body_position >= _cooldown_until &&
          exit.check_every && _body_position % exit.check_every == 0) {
        transition = exit.numerator >= exit.denominator ||
            (exit.numerator && random(exit.denominator) < exit.numerator);
      }
    }
    if (transition) {
      const uint32_t count = static_cast<uint32_t>(_children.size());
      if (!_spec.select_random) _selected = (_selected + 1) % count;
      else if (trigger_actions && (count > 1 || _spec.select_allow_self)) {
        if (_spec.select_allow_self) _selected = random(count);
        else {
          const uint32_t choice = random(count - 1);
          _selected = choice >= _selected ? choice + 1 : choice;
        }
      }
      start_child(_selected, trigger_actions);
      _cooldown_until = _body_position + _spec.select_cooldown;
    }
  }
  auto& child = _children[_selected];
  if (!child->complete()) { child->activate(true); child->advance(trigger_actions); }
}

void OccurrenceCycler::advance(bool trigger_actions)
{
  if (_done) reset(); // External root advancement repeats the whole program.
  if (!_prepared) prepare(_spec.length ? _spec.length : _inherited, trigger_actions);
  if (_iteration_done) { ++_iteration; begin_iteration(trigger_actions); }
  activate(true);
  if (_body_position == _body_length) {
    _body_position = 0;
    if (!_ramp.empty() && _segment + 1 < _ramp.size()) _body_length = _ramp[++_segment];
    _body_started = false;
  }
  if (!_body_started) begin_body(trigger_actions);
  run_body(trigger_actions);
  ++_body_position;
  ++_position;
  if (_elapsed < std::numeric_limits<uint32_t>::max()) ++_elapsed;
  const bool sequence_end = !_spec.cadence && _ramp.empty() &&
      _spec.occurrence_kind == pattern::Node::Composition::Sequential && !_children.empty() &&
      _selected + 1 == _children.size() && _children.back()->complete();
  if (_position >= _span || sequence_end) {
    _iteration_done = true;
    _done = !has_next_iteration();
  }
}
