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
    std::vector<Cycler*> kids;
    compile_children(kids);
    auto entry = make_action ? make_action(n) : std::function<void()>{};
    auto* occurrence = new OccurrenceCycler{n, std::move(entry), std::move(kids)};
    if (node_map && !n.id.empty()) (*node_map)[n.id + "_local"] = occurrence->local_clock();
    c = occurrence;

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
