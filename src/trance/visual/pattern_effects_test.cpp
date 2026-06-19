// Headless action-log proof for the Framing-B compiler's BEHAVIOUR lowering: the
// effects attached to a data-described pattern must fire at the same frames, in the
// same order, as the hand-built schedule. The "API" here is just a log, so this
// stays isolated (no SFML/protobuf) -- it tests the compiler's effect routing
// (which leaf, which action_frame, which order), not VisualControl itself.
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
  using pattern::Effect;
  using pattern::Node;
  using pattern::Slot;

  Effect eff(Effect::Kind kind, Slot slot = Slot::None)
  {
    Effect e;
    e.kind = kind;
    e.slot = slot;
    return e;
  }

  // The effect tuples of each SLOW_FLASH leaf, defined ONCE and shared between the
  // data form and the reference, so the test exercises the compiler's routing, not
  // a re-typed copy of the effect content.
  const std::vector<Effect> INIT = {eff(Effect::Kind::Themes)};
  const std::vector<Effect> ENTER = {eff(Effect::Kind::SpiralNew), eff(Effect::Kind::Font)};
  const std::vector<Effect> SLOW_LOOP = {eff(Effect::Kind::Image, Slot::Primary),
                                         eff(Effect::Kind::Anim, Slot::Primary),
                                         eff(Effect::Kind::Text, Slot::Primary),
                                         eff(Effect::Kind::SmallSub, Slot::Primary)};
  const std::vector<Effect> SLOW_SPIRAL = {eff(Effect::Kind::SpiralRot)};
  const std::vector<Effect> UPLOAD = {eff(Effect::Kind::Upload)};
  const std::vector<Effect> FAST_IMAGE = {eff(Effect::Kind::Image, Slot::Alternate)};
  const std::vector<Effect> FAST_TEXT = {eff(Effect::Kind::Text, Slot::Alternate)};
  const std::vector<Effect> FAST_SUB = {eff(Effect::Kind::SmallSub, Slot::Alternate)};
  const std::vector<Effect> FAST_SPIRAL = {eff(Effect::Kind::SpiralRot)};

  // ---- the data form ----
  Node leaf(uint32_t length, uint32_t action_frame, const std::vector<Effect>& effects)
  {
    Node n;
    n.type = Node::Type::Action;
    n.length = length;
    n.action_frame = action_frame;
    n.effects = effects;
    return n;
  }
  Node group(Node::Type t, std::vector<Node> c)
  {
    Node n;
    n.type = t;
    n.children = std::move(c);
    return n;
  }
  Node rep(uint32_t count, Node child)
  {
    Node n;
    n.type = Node::Type::Rep;
    n.count = count;
    n.children.push_back(std::move(child));
    return n;
  }

  Node slow_flash_ast()
  {
    auto slow_main =
        group(Node::Type::One, {leaf(1, 0, ENTER), rep(16, leaf(64, 0, SLOW_LOOP))});
    auto slow = group(Node::Type::Par,
                      {slow_main, leaf(1, 0, SLOW_SPIRAL), leaf(64, 32, UPLOAD)});

    auto fast_loop = group(Node::Type::Par, {leaf(8, 0, FAST_IMAGE), leaf(16, 8, FAST_TEXT)});
    auto fast_main = group(Node::Type::One, {leaf(1, 0, ENTER), rep(32, fast_loop)});
    auto fast = group(Node::Type::Par,
                      {fast_main, leaf(16, 0, FAST_SUB), leaf(1, 0, FAST_SPIRAL)});

    return group(Node::Type::One,
                 {leaf(1, 0, INIT), rep(2, group(Node::Type::Seq, {slow, fast}))});
  }

  // ---- logging ----
  struct LogEntry {
    uint32_t frame;
    Effect::Kind kind;
    Slot slot;
    bool operator!=(const LogEntry& o) const
    {
      return frame != o.frame || kind != o.kind || slot != o.slot;
    }
  };

  uint32_t g_frame = 0;

  std::function<void()> log_effects(std::vector<LogEntry>& log, const std::vector<Effect>& effects)
  {
    return [&log, &effects] {
      for (const auto& e : effects) {
        log.push_back({g_frame, e.kind, e.slot});
      }
    };
  }

  // ---- the reference: same schedule, hand-built, logging the same tuples ----
  Cycler* slow_flash_reference(std::vector<LogEntry>& log)
  {
    auto slow_main = new OneShotCycler{
        {new ActionCycler{1, 0, log_effects(log, ENTER)},
         new RepeatCycler{16, new ActionCycler{64, 0, log_effects(log, SLOW_LOOP)}}}};
    auto slow = new ParallelCycler{{slow_main,
                                    new ActionCycler{1, 0, log_effects(log, SLOW_SPIRAL)},
                                    new ActionCycler{64, 32, log_effects(log, UPLOAD)}}};

    auto fast_loop = new ParallelCycler{{new ActionCycler{8, 0, log_effects(log, FAST_IMAGE)},
                                         new ActionCycler{16, 8, log_effects(log, FAST_TEXT)}}};
    auto fast_main = new OneShotCycler{
        {new ActionCycler{1, 0, log_effects(log, ENTER)}, new RepeatCycler{32, fast_loop}}};
    auto fast = new ParallelCycler{{fast_main,
                                    new ActionCycler{16, 0, log_effects(log, FAST_SUB)},
                                    new ActionCycler{1, 0, log_effects(log, FAST_SPIRAL)}}};

    auto main_repeat = new RepeatCycler{2, new SequenceCycler{{slow, fast}}};
    return new OneShotCycler{{new ActionCycler{1, 0, log_effects(log, INIT)}, main_repeat}};
  }
}

int main()
{
  std::vector<LogEntry> compiled_log;
  std::vector<LogEntry> reference_log;

  Node ast = slow_flash_ast();
  Cycler* compiled =
      pattern::compile(ast, [&](const Node& n) { return log_effects(compiled_log, n.effects); });
  Cycler* reference = slow_flash_reference(reference_log);

  const uint32_t total = 3072;
  for (g_frame = 0; g_frame < total; ++g_frame) {
    compiled->advance(true);
    reference->advance(true);
  }

  int failures = 0;
  if (compiled_log.size() != reference_log.size()) {
    std::cerr << "log size " << compiled_log.size() << " != " << reference_log.size() << "\n";
    ++failures;
  } else {
    for (std::size_t i = 0; i < compiled_log.size(); ++i) {
      if (compiled_log[i] != reference_log[i]) {
        std::cerr << "log diverged at entry " << i << " (frame " << compiled_log[i].frame << ")\n";
        ++failures;
        break;
      }
    }
  }

  // Spec sanity: change_themes fires exactly once per 3072-frame cycle (the root
  // init), and the bed is non-trivial.
  uint32_t themes = 0;
  for (const auto& e : compiled_log) {
    if (e.kind == Effect::Kind::Themes) {
      ++themes;
    }
  }
  if (themes != 1) {
    std::cerr << "expected 1 themes event, got " << themes << "\n";
    ++failures;
  }
  if (compiled_log.size() < 100) {
    std::cerr << "log implausibly small: " << compiled_log.size() << "\n";
    ++failures;
  }

  if (failures == 0) {
    std::cout << "PASS: compiled SLOW_FLASH effects == hardcoded firing over " << total
              << " frames (" << compiled_log.size() << " events)\n";
    return 0;
  }
  return 1;
}
