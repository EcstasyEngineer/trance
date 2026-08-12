#include <trance/visual/pattern_compiler.h>
#include <trance/visual/cyclers.h>
#include <memory>
#include <vector>

namespace
{
  ImageSlotHint to_hint(pattern::Slot s)
  {
    switch (s) {
    case pattern::Slot::Primary:
      return ImageSlotHint::Primary;
    case pattern::Slot::Secondary:
      return ImageSlotHint::Alternate;
    case pattern::Slot::Runtime:
      return ImageSlotHint::Runtime;
    default:
      return ImageSlotHint::None;
    }
  }
}

namespace
{
  Cycler* compile_impl(const pattern::Node& n, const pattern::MakeAction& make_action,
                       pattern::NodeMap* node_map)
  {
    using pattern::Node;
    Cycler* c = nullptr;
    auto compile_children = [&](std::vector<Cycler*>& kids) {
      kids.reserve(n.children.size());
      for (const auto& k : n.children) {
        kids.push_back(compile_impl(k, make_action, node_map));
      }
    };
    switch (n.type) {
    case Node::Type::Action: {
      // The leaf's behaviour comes from the caller's MakeAction (no-op for a pure timer);
      // an ActionCycler without one is just a counter.
      std::function<void()> fn = make_action ? make_action(n) : std::function<void()>{};
      // `divide N`: run the effects only every Nth firing. The counter is bounded
      // state owned here, so every MakeAction (tests + engine) gets it identically.
      if (fn && n.divide > 1) {
        auto counter = std::make_shared<uint32_t>(0);
        uint32_t div = n.divide;
        std::function<void()> inner = fn;
        fn = [inner, counter, div] {
          if ((*counter)++ % div == 0) {
            inner();
          }
        };
      }
      c = fn ? new ActionCycler{n.length, fn} : new ActionCycler{n.length};
      break;
    }
    case Node::Type::Seq: {
      std::vector<Cycler*> kids;
      compile_children(kids);
      c = new SequenceCycler{kids};
      break;
    }
    case Node::Type::Par: {
      std::vector<Cycler*> kids;
      compile_children(kids);
      c = new ParallelCycler{kids};
      break;
    }
    case Node::Type::One: {
      std::vector<Cycler*> kids;
      compile_children(kids);
      c = new OneShotCycler{kids};
      break;
    }
    case Node::Type::Rep:
      c = new RepeatCycler{n.count, compile_impl(n.children.at(0), make_action, node_map)};
      break;
    case Node::Type::Off:
      c = new OffsetCycler{n.count, compile_impl(n.children.at(0), make_action, node_map)};
      break;
    case Node::Type::Burst: {
      // Reuse the action seam: the base and burst behaviours are just two effect
      // lists, so synthesise a Node for each and run them through make_action.
      pattern::Node base_node;
      base_node.effects = n.effects;
      pattern::Node burst_node;
      burst_node.effects = n.burst_effects;
      pattern::Node enter_node;
      enter_node.effects = n.burst_enter_effects;
      std::function<void()> base = make_action ? make_action(base_node) : std::function<void()>{};
      std::function<void()> burst =
          make_action ? make_action(burst_node) : std::function<void()>{};
      std::function<void()> enter =
          make_action ? make_action(enter_node) : std::function<void()>{};
      BurstCycler::Params p{n.length,        n.burst_period,  n.burst_chance_den,
                            n.burst_cooldown, n.burst_dur_min, n.burst_dur_max};
      c = new BurstCycler{p, base, burst, enter};
      break;
    }
    }

    if (!n.phase.empty()) {
      c->set_phase(n.phase.c_str());
    }
    if (n.image_slot != pattern::Slot::None) {
      c->set_image_slot(to_hint(n.image_slot), n.image_label.c_str());
    }
    if (node_map && !n.id.empty()) {
      (*node_map)[n.id] = c;
    }
    return c;
  }
}

namespace pattern
{
  Cycler* compile(const Node& n, const MakeAction& make_action, NodeMap& node_map)
  {
    return compile_impl(n, make_action, &node_map);
  }

  Cycler* compile(const Node& n, const MakeAction& make_action)
  {
    return compile_impl(n, make_action, nullptr);
  }

  Cycler* compile(const Node& n)
  {
    return compile_impl(n, MakeAction{}, nullptr);
  }
}
