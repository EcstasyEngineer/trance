// Headless equivalence proof for the Framing-B compiler: a data-described
// SLOW_FLASH must lower to a Cycler tree structurally identical to the hardcoded
// SlowFlashVisual schedule, frame for frame. Pure cyclers + compiler -- no SFML,
// no protobuf -- so it runs anywhere with a C++17 compiler.
//
// This proves the riskiest Plan-B assumption (the schedule can be datafied and
// reconstructed as the same machine). Behaviour (effects) and a text parser come
// next; this is the foundation they build on.
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
  // ---- the data form: SLOW_FLASH as a pattern::Node tree -------------------
  using pattern::Node;
  using pattern::Slot;

  Node action(uint32_t length)
  {
    Node n;
    n.type = Node::Type::Action;
    n.length = length;
    return n;
  }
  Node group(Node::Type type, std::vector<Node> children)
  {
    Node n;
    n.type = type;
    n.children = std::move(children);
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
  Node phase(Node n, const char* label)
  {
    n.phase = label;
    return n;
  }
  Node image(Node n, Slot slot)
  {
    n.image_slot = slot;
    return n;
  }

  Node slow_flash_ast()
  {
    auto slow_main = group(Node::Type::One,
                           {action(1), rep(16, image(action(64), Slot::Primary))});
    auto slow = phase(group(Node::Type::Par, {slow_main, action(1), action(64)}), "SLOW");

    auto fast_loop = group(Node::Type::Par, {image(action(8), Slot::Alternate), action(16)});
    auto fast_main = group(Node::Type::One, {action(1), rep(32, fast_loop)});
    auto fast = phase(group(Node::Type::Par, {fast_main, action(16), action(1)}), "FAST");

    return group(Node::Type::One,
                 {action(1), rep(2, group(Node::Type::Seq, {slow, fast}))});
  }

  // ---- the reference form: the same schedule hand-built the old way ---------
  // Mirrors SlowFlashVisual's constructor (visual.cpp), no-op actions only.
  Cycler* slow_flash_reference()
  {
    auto slow_loop = new ActionCycler{64};
    slow_loop->set_image_slot(ImageSlotHint::Primary);
    auto slow_main = new OneShotCycler{{new ActionCycler{1}, new RepeatCycler{16, slow_loop}}};
    auto slow = new ParallelCycler{{slow_main, new ActionCycler{1}, new ActionCycler{64}}};
    slow->set_phase("SLOW");

    auto fast_image = new ActionCycler{8};
    fast_image->set_image_slot(ImageSlotHint::Alternate);
    auto fast_loop = new ParallelCycler{{fast_image, new ActionCycler{16}}};
    auto fast_main = new OneShotCycler{{new ActionCycler{1}, new RepeatCycler{32, fast_loop}}};
    auto fast = new ParallelCycler{{fast_main, new ActionCycler{16}, new ActionCycler{1}}};
    fast->set_phase("FAST");

    auto main_repeat = new RepeatCycler{2, new SequenceCycler{{slow, fast}}};
    return new OneShotCycler{{new ActionCycler{1}, main_repeat}};
  }

  // ---- comparison ----------------------------------------------------------
  int g_failures = 0;

  bool node_equal(const Cycler* a, const Cycler* b, const std::string& path)
  {
    auto fail = [&](const std::string& what) {
      std::cerr << "MISMATCH at " << path << ": " << what << "\n";
      ++g_failures;
      return false;
    };
    if (std::string{a->type_name()} != b->type_name()) {
      return fail(std::string{"type "} + a->type_name() + " != " + b->type_name());
    }
    if (a->length() != b->length()) {
      return fail("length " + std::to_string(a->length()) + " != " + std::to_string(b->length()));
    }
    if (a->position() != b->position()) {
      return fail("position " + std::to_string(a->position()) + " != "
                  + std::to_string(b->position()));
    }
    if (a->active() != b->active()) {
      return fail("active differs");
    }
    if (a->phase() != b->phase()) {
      return fail("phase '" + a->phase() + "' != '" + b->phase() + "'");
    }
    if (a->image_slot() != b->image_slot()) {
      return fail("image_slot differs");
    }
    auto ac = a->children();
    auto bc = b->children();
    if (ac.size() != bc.size()) {
      return fail("child count " + std::to_string(ac.size()) + " != " + std::to_string(bc.size()));
    }
    for (std::size_t i = 0; i < ac.size(); ++i) {
      if (!node_equal(ac[i], bc[i], path + "/" + std::to_string(i))) {
        return false;
      }
    }
    return true;
  }

  // Deepest active labelled node -- the overlay's "current section".
  const Cycler* active_section(const Cycler* c)
  {
    if (!c || !c->active()) {
      return nullptr;
    }
    const Cycler* best = c->phase().empty() ? nullptr : c;
    for (const Cycler* kid : c->children()) {
      if (const Cycler* deeper = active_section(kid)) {
        best = deeper;
      }
    }
    return best;
  }

  void check(bool cond, const std::string& what)
  {
    if (!cond) {
      std::cerr << "CHECK FAILED: " << what << "\n";
      ++g_failures;
    }
  }
}

int main()
{
  Cycler* compiled = pattern::compile(slow_flash_ast());
  Cycler* reference = slow_flash_reference();

  // Spec tie (docs/visual-grammar.md SLOW_FLASH): 3072 frames total, and SLOW runs
  // ~2x as long as FAST (1024 vs 512 per 1536-frame half).
  check(compiled->length() == 3072, "compiled root length is 3072");

  const uint32_t total = 3072;
  uint32_t slow = 0;
  uint32_t fast = 0;
  for (uint32_t f = 0; f < total; ++f) {
    // The hard proof: structurally identical to the hand-built schedule every frame
    // (type, length, position, active, phase, image_slot, child shape).
    if (!node_equal(compiled, reference, "root")) {
      std::cerr << "diverged at frame " << f << "\n";
      break;
    }
    // The overlay's "current section" must also agree (it derives from active()).
    const Cycler* cs = active_section(compiled);
    const Cycler* rs = active_section(reference);
    if ((cs ? cs->phase() : "") != (rs ? rs->phase() : "")) {
      check(false, "section disagreement at frame " + std::to_string(f));
      break;
    }
    if (cs && cs->phase() == "SLOW") {
      ++slow;
    } else if (cs && cs->phase() == "FAST") {
      ++fast;
    }

    compiled->advance(false);
    reference->advance(false);
  }

  // Every frame is in some section, and the SLOW:FAST split is ~2:1 (the off-by-one
  // is the initial-frame calculate_active() quirk, hence the tolerance).
  check(slow + fast == total, "every frame has a section");
  check(slow > 2000 && slow < 2100, "SLOW frames ~2048 (got " + std::to_string(slow) + ")");
  check(fast > 1000 && fast < 1100, "FAST frames ~1024 (got " + std::to_string(fast) + ")");

  if (g_failures == 0) {
    std::cout << "PASS: compiled SLOW_FLASH == hardcoded schedule over " << total << " frames\n";
    return 0;
  }
  std::cerr << g_failures << " failure(s)\n";
  return 1;
}
