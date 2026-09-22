// Phase ownership cannot be checked by --lint: these assertions observe effects,
// activation and render values across transitions. No media, window or GL context.
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser_v3.h>
#include <trance/visual/render_eval.h>
#include <common/util.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Kind = pattern::Effect::Kind;
using Slot = pattern::Slot;

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, const std::string& message)
{
  require(std::isfinite(actual) && std::abs(actual - expected) < 0.00001,
          message + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
}
std::string frames(const std::vector<uint32_t>& values)
{
  std::string result;
  for (const auto value : values) result += std::to_string(value) + " ";
  return result;
}
struct Event { uint32_t frame; Kind kind; Slot slot; };

struct Run {
  patternv3::ParseResult parsed;
  pattern::NodeMap nodes;
  pattern::Registers regs;
  std::vector<Event> events;
  uint32_t frame = 0;
  std::unique_ptr<Cycler> root;

  explicit Run(const std::string& source) : parsed(patternv3::parse(source))
  {
    require(parsed.ok, "parse: " + parsed.error);
  }
  void compile()
  {
    root.reset(pattern::compile(parsed.root, [this](const pattern::Node& node) {
      const auto effects = node.effects;
      return [this, effects] {
        for (const auto& effect : effects) {
          events.push_back({frame, effect.kind, effect.slot});
          // No media effects run. Retain just the scalar/slot state read by the
          // generated render expressions (including builtin chance guards).
          if (effect.kind == Kind::Set) regs.scalars[effect.target] = effect.ivalue;
          if (effect.kind == Kind::Anim) regs.anim_slot = effect.slot;
        }
      };
    }, nodes));
  }
  std::vector<uint32_t> picks(Slot slot) const
  {
    std::vector<uint32_t> result;
    for (const auto& event : events)
      if (event.kind == Kind::Image && event.slot == slot) result.push_back(event.frame);
    return result;
  }
  std::vector<double> visible_zooms() const
  {
    std::vector<double> result;
    for (const auto& draw : parsed.render_block) {
      if (draw.op == pattern::RenderStmt::Op::Image
          && pattern::eval_cond_expr(draw.when, regs, nodes, root.get()))
        result.push_back(pattern::eval_expr(draw.zoom, 0, regs, nodes, root.get()));
    }
    return result;
  }
};

void nested_cuts()
{
  Run run(R"(
pattern scene for 240f {
  burst -> phase period 8f chance 1/1 cooldown 32f duration 24f {
    base { image primary }
    burst { every 16f { image secondary zoom (curve 0 -> 1) } }
  }
})");
  run.compile();
  for (run.frame = 0; run.frame < 144; ++run.frame) {
    run.root->advance();
    const auto zooms = run.visible_zooms();
    require(zooms.size() == 1, "exactly the active branch is visible");
    const auto local = run.frame % 56;
    close(zooms.front(), local < 24 ? double(local % 16) / 16 : 0,
          "nested cut restarts with its phase, including a partial final cut");
  }
  require(run.picks(Slot::Secondary) == std::vector<uint32_t>({0, 16, 56, 72, 112, 128}),
          "nested picks must never run in the inactive branch");
  require(run.picks(Slot::Primary) == std::vector<uint32_t>({24, 80, 136}),
          "bare base selection occurs once on each fresh entry");
}

void phase_clock(uint32_t duration, bool animation)
{
  Run run("pattern scene for 512f { burst -> phase period 8f chance 1/1 cooldown 32f duration "
          + std::to_string(duration) + "f { base { image primary } burst -> held { image secondary "
          + "zoom (curve 0.1 -> 0.7) "
          + (animation ? "anim " : "")
          + "draw cur zoom (curve 0 -> 1 over scene) "
            "draw cur zoom (curve 0 -> 1 over phase) "
            "every 8f { draw cur zoom (curve 0.1 -> 0.7 over held) } } } }");
  run.compile();
  const auto period = duration + 32;
  for (run.frame = 0; run.frame < period + duration; ++run.frame) {
    run.root->advance();
    const auto before = run.events.size();
    const auto zooms = run.visible_zooms();
    require(zooms == run.visible_zooms() && before == run.events.size(),
            "repeated eye/desktop render evaluation must not advance or select");
    const auto local = run.frame % period;
    if (local < duration) {
      require(zooms.size() == 4, "all burst draws are active together");
      close(zooms[0], 0.1 + 0.6 * double(local) / duration, "burst uses sampled local duration");
      close(zooms[1], double(run.frame) / 512, "explicit parent clock keeps global phase");
      close(zooms[2], zooms[1], "named controller retains its full-duration clock");
      close(zooms[3], zooms[0], "nested cut can explicitly follow its named phase");
    } else {
      require(zooms.size() == 1, "burst descendants stop drawing outside their parent");
    }
  }
  // This compares the authored transform for still and animation requests. Actual
  // ThemeBank fallback/decoding is deliberately outside this headless test.
  require(run.picks(Slot::Secondary) == std::vector<uint32_t>({0, period}),
          "held media is selected once per burst, never once per rendered frame");
}

void entry_order_and_reset()
{
  Run run(R"(
pattern scene for 128f {
  burst period 8f chance 1/1 cooldown 32f duration 24f {
    base { image primary }
    enter { image primary }
    burst { image secondary }
  }
})");
  run.compile();
  for (run.frame = 0; run.frame < 128; ++run.frame) run.root->advance(false);
  require(run.events.empty(), "schedule-only advance must suppress all effects");
  run.root->reset();
  for (run.frame = 0; run.frame < 128; ++run.frame) run.root->advance();
  require(run.picks(Slot::Secondary) == std::vector<uint32_t>({0, 56, 112}),
          "reset restarts burst occurrences");
  for (const auto at : {0u, 56u, 112u}) {
    std::vector<Slot> order;
    for (const auto& event : run.events)
      if (event.frame == at && event.kind == Kind::Image) order.push_back(event.slot);
    require(order == std::vector<Slot>({Slot::Primary, Slot::Secondary}),
            "enter setup must happen before the burst's bare effects");
  }
  run.events.clear();
  run.frame = 128;
  run.root->advance();
  require(run.picks(Slot::Secondary) == std::vector<uint32_t>({128}),
          "automatic whole-pattern repetition starts a fresh phase");
}

void nested_sequence_and_parent()
{
  Run run(R"(
pattern scene for 192f seq {
  pattern before for 64f {}
  pattern active for 128f {
    burst period 8f chance 1/1 cooldown 32f duration 24f {
      base {}
      burst {
        pattern pair for 32f seq {
          pattern first for 16f { image primary zoom (curve 0 -> 1) }
          pattern second for 16f { image secondary zoom (curve 0 -> 1) }
        }
      }
    }
  }
})");
  run.compile();
  const auto initial_rng = get_mersenne_twister();
  for (const auto& item : run.nodes)
    require(std::isfinite(item.second->progress()), "unstarted phase clocks are safe to inspect");
  for (run.frame = 0; run.frame < 192; ++run.frame) {
    run.root->advance();
    for (const auto& item : run.nodes)
      require(std::isfinite(item.second->progress()), "inactive phase progress stays finite");
    for (const auto zoom : run.visible_zooms()) require(std::isfinite(zoom), "inactive clocks stay safe");
    if (run.frame < 64) {
      require(run.visible_zooms().empty(), "inactive parent hides nested phase draws");
      require(get_mersenne_twister() == initial_rng, "inactive parent must not consume burst RNG");
    }
  }
  require(run.picks(Slot::Primary) == std::vector<uint32_t>({64, 120, 176}),
          "inactive parent must not consume nested selections; sequence restarts each burst: got "
          + frames(run.picks(Slot::Primary)));
  require(run.picks(Slot::Secondary) == std::vector<uint32_t>({80, 136}),
          "phase exit truncates its nested sequence at the parent boundary");
}

void sampled_durations()
{
  get_mersenne_twister().seed(2026);
  Run run(R"(
pattern scene for 8192f {
  burst period 8f chance 1/1 cooldown 32f duration 64f..128f {
    base {}
    burst { image secondary zoom (curve 0 -> 1) draw cur zoom [this.length] }
  }
})");
  run.compile();
  std::set<uint32_t> lengths;
  uint32_t local = 0;
  uint32_t sampled = 0;
  bool previous = false;
  for (run.frame = 0; run.frame < 8192; ++run.frame) {
    run.root->advance();
    const auto zooms = run.visible_zooms();
    const bool active = !zooms.empty();
    if (active) {
      require(zooms.size() == 2, "sampled phase draws its curve and length together");
      if (!previous) {
        local = 0;
        sampled = static_cast<uint32_t>(zooms[1]);
        lengths.insert(sampled);
        require(sampled >= 64 && sampled <= 128 && sampled % 8 == 0,
                "sampled duration stays within the declared tick-aligned range");
      }
      close(zooms[1], sampled, "a duration is sampled once, not rerolled every frame");
      close(zooms[0], double(local) / sampled, "successive differently sized phases start at zero");
      ++local;
    } else if (previous) {
      require(local == sampled, "sampled phase stops at its own duration");
    }
    previous = active;
  }
  require(lengths.size() > 1, "fixture exercises more than one sampled duration");
}

void validation()
{
  const auto source = [](const std::string& base, const std::string& burst,
                         const std::string& duration = "24f..32f") {
    return "pattern scene for 240f { burst period 8f chance 1/1 cooldown 32f duration "
           + duration + " { base { " + base + " } burst { " + burst + " } } }";
  };
  for (const auto& effect : {"image primary zoom (curve 0 -> 1)",
                             "image primary zoom [this.progress]", "image primary zoom [this.length]"})
    require(!patternv3::parse(source(effect, "image secondary")).ok,
            "indefinite base cannot promise normalized progress or a known endpoint");
  require(patternv3::parse(source("every 8f { image primary zoom (curve 0 -> 1) }",
                                         "image secondary env in 0.25 hold 0.25 out 0.25")).ok,
          "finite cuts and fractional sampled-duration envelopes are valid");
  require(patternv3::parse(source("image primary zoom (curve 0 -> 1 over scene)",
                                         "image secondary")).ok,
          "indefinite base may explicitly use an ancestor's known clock");
  require(!patternv3::parse(source("", "image secondary env in 8f out 8f")).ok,
          "frame-unit envelope requires a fixed duration, not a sampled range");
  require(patternv3::parse(source("", "image secondary env in 8f out 8f", "24f")).ok,
          "frame-unit envelope works when the burst has a fixed duration");
  require(!patternv3::parse("pattern scene for 128f { burst period 8f duration 24f { "
                            "base {} base {} burst {} } }").ok,
          "duplicate phase blocks must not merge contradictory lifetimes");
  require(!patternv3::parse("pattern scene for 128f { burst period 8f duration 24f { "
                            "enter { every 8f { image primary } } burst {} } }").ok,
          "entry events cannot own timed child schedules");
  require(!patternv3::parse(source("", "", "4294967295f")).ok,
          "rounding a burst duration to whole periods must not overflow frames");
  const std::string ramp = "every ramp 8f -> 4f steps 4 { image primary }";
  require(!patternv3::parse(source(ramp, "", "24f")).ok,
          "compile-time ramp cannot normalize against an indefinite base");
  require(!patternv3::parse(source("", ramp)).ok,
          "compile-time ramp cannot normalize against a sampled duration");
  require(patternv3::parse(source("", ramp, "24f")).ok,
          "a fixed-duration burst supports a compile-time ramp");
  require(patternv3::parse(source("", "pattern cuts for 24f { " + ramp + " }")).ok,
          "a timed child makes a ramp well-defined inside a sampled phase");
}

pattern::Node* burst_node(pattern::Node& node)
{
  if (node.type == pattern::Node::Type::Burst) return &node;
  for (auto& child : node.children) if (auto* found = burst_node(child)) return found;
  return nullptr;
}

void super_fast(uint32_t ticks)
{
  Run run(builtin::pattern_source_v3(8));
  auto* burst = burst_node(run.parsed.root);
  require(burst != nullptr, "super_fast contains a burst");
  burst->burst_chance_den = 1;
  burst->burst_dur_min = ticks;
  burst->burst_dur_max = ticks;
  const auto duration = ticks * burst->burst_period;
  run.compile();
  for (run.frame = 0; run.frame < duration + 16; ++run.frame) {
    run.root->advance();
    const auto zooms = run.visible_zooms();
    require(zooms.size() == 1, "super_fast draws exactly one image lane");
    close(zooms.front(), run.frame < duration
          ? 0.1 + 0.6 * double(run.frame) / duration
          : 0.0625 + 0.125 * double((run.frame - duration) % 8) / 8,
          "super_fast maintains motion throughout held media and individual rapid cuts");
  }
  uint32_t animation_selections = 0;
  for (const auto& event : run.events) if (event.kind == Kind::Anim) ++animation_selections;
  require(animation_selections == 1, "held animation/fallback is selected once across its moving envelope");
}
} // namespace

int main()
{
  try {
    nested_cuts();
    for (const auto duration : {24u, 64u, 128u}) {
      phase_clock(duration, false);
      phase_clock(duration, true);
    }
    entry_order_and_reset();
    nested_sequence_and_parent();
    sampled_durations();
    validation();
    super_fast(8);
    super_fast(16);
    std::cout << "phase execution, local clocks and SuperFast motion passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "phase_execution_test: " << error.what() << '\n';
    return 1;
  }
}
