#include <trance/visual/pattern_compiler.h>
#include <trance/visual/cyclers.h>
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
    default:
      return ImageSlotHint::None;
    }
  }
}

namespace
{
  std::unique_ptr<Cycler> compile_impl(pattern::Node n, const pattern::MakeAction& make_action,
                                      pattern::NodeMap* node_map)
  {
    auto entry = make_action ? make_action(n) : std::function<void()>{};
    std::vector<std::unique_ptr<Cycler>> kids;
    kids.reserve(n.children.size());
    for (auto& child : n.children)
      kids.push_back(compile_impl(std::move(child), make_action, node_map));
    n.children.clear();
    const auto id = n.id;
    const auto phase = n.phase;
    const auto slot = n.image_slot;
    const auto label = n.image_label;
    n.effects.clear();
    auto c = std::make_unique<OccurrenceCycler>(std::move(n), std::move(entry), std::move(kids));
    if (!phase.empty()) c->set_phase(phase.c_str());
    if (slot != pattern::Slot::None) c->set_image_slot(to_hint(slot), label.c_str());
    if (node_map && !id.empty()) {
      (*node_map)[id + "_local"] = c->local_clock();
      (*node_map)[id] = c.get();
    }
    return c;
  }
}

namespace pattern
{
  Cycler* compile(const Node& n, const MakeAction& make_action, NodeMap& node_map)
  {
    NodeMap pending;
    auto result = compile_impl(n, make_action, &pending);
    for (const auto& entry : pending) node_map[entry.first] = entry.second;
    return result.release();
  }

  Cycler* compile(const Node& n, const MakeAction& make_action)
  {
    return compile_impl(n, make_action, nullptr).release();
  }

  Cycler* compile(const Node& n)
  {
    return compile_impl(n, MakeAction{}, nullptr).release();
  }
}
