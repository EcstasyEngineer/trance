#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_compiler.h>
#include <common/util.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Node = pattern::Node;
using Kind = Node::Composition;
void check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
Node node(Kind kind, uint32_t duration, const char* id)
{
  Node n; n.occurrence_kind = kind;
  n.length = duration; n.id = id; return n;
}
struct Run {
  unsigned frame = 0;
  std::vector<std::pair<unsigned, std::string>> events;
  pattern::NodeMap clocks;
  std::unique_ptr<Cycler> root;
  explicit Run(const Node& n) {
    root.reset(pattern::compile(n, [this](const Node& value) {
      const auto id = value.id;
      return [this, id] { events.emplace_back(frame, id); };
    }, clocks));
  }
  void tick(unsigned count = 1) { while (count--) { root->advance(); ++frame; } }
  unsigned count(const std::string& id) const {
    unsigned result = 0; for (const auto& e : events) if (e.second == id) ++result;
    return result;
  }
};
void nesting_and_completion()
{
  const Kind kinds[] = {Kind::Parallel, Kind::Sequential, Kind::Random};
  for (const auto outer : kinds) for (const auto inner : kinds) {
    auto root = node(outer, 12, "root");
    auto child = node(inner, 4, "child");
    child.children.push_back(node(Kind::Parallel, 2, "leaf"));
    root.children.push_back(child);
    Run run(root); run.tick();
    check(run.count("leaf") == 1, "all nine nesting pairs enter leaf once");
    run.tick(2);
  }
  auto root = node(Kind::Parallel, 12, "root");
  root.children.push_back(node(Kind::Parallel, 2, "short"));
  Run run(root); run.tick(12);
  check(run.count("short") == 1, "finite child must not repeat in longer parent");
  check(!run.clocks.at("short")->active(), "finite child cannot render during inactive tail");
  check(run.root->complete(), "root completes its authored duration");
  run.tick();
  check(run.count("short") == 2, "external root advance repeats complete program");
}
void sequential_and_repeat()
{
  auto root = node(Kind::Sequential, 100, "root");
  root.children = {node(Kind::Parallel, 2, "a"), node(Kind::Parallel, 3, "b")};
  root.repeat_count = 2;
  Run run(root); run.tick(10);
  check(run.count("a") == 2 && run.count("b") == 2, "explicit sequence repetition");
  check(run.root->complete(), "long owner does not silently loop finished sequence");
  check(run.root->elapsed_length() == 10, "completed lifetime includes every explicit iteration");
  check(run.events.at(2).first == 2, "second sequential child starts after first lifetime");
  auto timed = node(Kind::Parallel, 3, "repeat"); timed.repeat_count = 2;
  Run twice(timed); twice.tick(6);
  check(twice.count("repeat") == 2 && twice.root->complete(), "timed repeat count");
  timed.repeat_forever = true;
  auto owner = node(Kind::Parallel, 8, "owner"); owner.children.push_back(timed);
  Run clipped(owner); clipped.tick(8);
  check(clipped.count("repeat") == 3 && clipped.root->complete(), "parent clips forever child");
}
void child_loops()
{
  auto seq = node(Kind::Sequential, 11, "root");
  seq.child_mode = Node::ChildMode::Loop;
  seq.children = {node(Kind::Parallel, 2, "a"), node(Kind::Parallel, 3, "b")};
  Run run(seq); run.tick(11);
  check(run.root->complete() && run.root->elapsed_length() == 11, "loop keeps owner cap");
  check(run.count("a") == 3 && run.count("b") == 2, "loop restarts entire sequence");
  std::vector<unsigned> a, b;
  for (const auto& e : run.events) {
    if (e.second == "a") a.push_back(e.first);
    if (e.second == "b") b.push_back(e.first);
  }
  check(a == std::vector<unsigned>({0,5,10}) && b == std::vector<unsigned>({2,7}),
        "sequence loop has no consumed boundary ticks");
  seq.occurrence_kind = Kind::Parallel;
  Run parallel(seq); parallel.tick(11);
  check(parallel.count("a") == 6 && parallel.count("b") == 4,
        "parallel children loop independently");
  seq.cadence = 4;
  Run cadence(seq); cadence.tick(11);
  check(cadence.count("a") == 6 && cadence.count("b") == 5,
        "loop restarts are bounded by cadence");
}
void throwing_factory()
{
  auto root = node(Kind::Parallel, 5, "root");
  root.children = {node(Kind::Parallel, 2, "a"), node(Kind::Parallel, 3, "b")};
  pattern::NodeMap clocks;
  bool threw = false;
  try {
    std::unique_ptr<Cycler> compiled(pattern::compile(root, [](const Node& n) {
      if (n.id == "b") throw std::runtime_error("injected factory failure");
      return std::function<void()>{};
    }, clocks));
  } catch (const std::runtime_error&) { threw = true; }
  check(threw && clocks.empty(), "failed compile does not publish dangling clock pointers");
}
Node selector(bool random_order = false)
{
  auto n = node(Kind::Random, 100, "root"); n.select_random = random_order;
  n.children = {node(Kind::Parallel, 2, "a"), node(Kind::Parallel, 3, "b"),
                node(Kind::Parallel, 1, "c")};
  return n;
}
void selectors()
{
  auto n = selector(); Run run(n); run.tick(7);
  check(run.count("a") == 2 && run.count("b") == 1 && run.count("c") == 1,
        "round robin visits every ordinary child");
  n.children.resize(1); Run single(n); single.tick(7);
  check(single.count("a") == 4, "single child restarts without zero-time loop");
  n = selector(); n.children.resize(2);
  n.child_exits.resize(2); n.child_exits[0] = {true, 1, 1, 1};
  n.select_cooldown = 5;
  Run chance(n); chance.tick();
  check(chance.count("a") == 0 && chance.count("b") == 1, "initial chance precedes effects");
  chance.tick(3);
  check(chance.count("a") == 1, "completion ignores chance cooldown");
  chance.tick(); check(chance.count("a") == 1, "cooldown suppresses chance transition");
  n.child_exits[0].numerator = 0; Run never(n); never.tick();
  check(never.count("a") == 1 && never.count("b") == 0, "zero chance never switches");
  n = selector(true); n.children.resize(2);
  get_mersenne_twister().seed(17); Run random_run(n); random_run.tick(20);
  check(random_run.count("a") > 1 && random_run.count("b") > 1,
        "random without self uses eligible other children");
  n.children.resize(1); n.select_allow_self = true;
  n.child_exits = {{true, 1, 1, 1}};
  Run self(n); self.tick(5);
  check(self.count("a") == 5, "self selection is a real restart");
  n = selector(); n.children[0].length = 100;
  n.child_exits = {{true, 1, 2, 1}};
  get_mersenne_twister().seed(32); Run rational(n); rational.tick(50);
  check(rational.count("b") > 0, "rational chance executes transitions");
}
void clocks_offsets_sampling()
{
  auto shifted = node(Kind::Parallel, 12, "shifted"); shifted.cadence = 4; shifted.offset = 2;
  auto hidden = selector(true); hidden.length = 0; hidden.duration_max = 8;
  hidden.child_exits = {{true, 1, 2, 1}, {true, 1, 2, 1}, {true, 1, 2, 1}};
  shifted.children.push_back(hidden);
  get_mersenne_twister().seed(123);
  const auto before = get_mersenne_twister();
  Run no_preroll_rng(shifted); no_preroll_rng.root->advance(false);
  check(get_mersenne_twister() == before, "phase preroll cannot consume random state");
  auto n = node(Kind::Parallel, 12, "root"); n.cadence = 4; n.offset = 2;
  Run phase(n); phase.tick();
  check(phase.events.empty(), "phase preroll cannot run entry actions");
  check(phase.clocks.at("root_local")->frame() == 2, "phase shifted local clock");
  check(phase.clocks.at("root")->frame() == 0, "ancestor clock spans whole occurrence");
  phase.tick(2); check(phase.count("root") == 1, "phase enters after shifted boundary");
  phase.root->reset(); phase.tick();
  check(phase.count("root") == 1, "reset reapplies phase without firing entry");
  n = node(Kind::Parallel, 12, "root"); n.cadence = 4;
  n.children.push_back(node(Kind::Parallel, 0, "inherited"));
  Run inherited(n); inherited.tick(12);
  check(inherited.count("inherited") == 3, "inherited children restart on cadence boundary");
  n = node(Kind::Parallel, 30, "root");
  auto sample = node(Kind::Parallel, 4, "sample"); sample.duration_max = 10;
  sample.duration_step = 2;
  n.children.push_back(sample); Run sampled(n); sampled.tick();
  auto* clock = sampled.clocks.at("sample"); const auto duration = clock->length();
  check(duration >= 4 && duration <= 10 && duration % 2 == 0, "sample aligned inclusive range");
  sampled.tick(duration - 1); check(clock->length() == duration, "duration sampled once per activation");
  sampled.tick(10); check(sampled.count("sample") == 1 && !clock->active(), "sampled child tail inactive");
  n = node(Kind::Parallel, 24, "root"); n.cadence_ramp_from = 8;
  n.cadence_ramp_to = 2; n.cadence_ramp_steps = 4;
  Run ramp(n); ramp.tick(24);
  check(ramp.count("root") == 4 && ramp.root->complete(), "ramp exactly fills authored span");
}
}
int main()
{
  try {
    nesting_and_completion(); sequential_and_repeat(); child_loops(); throwing_factory();
    selectors(); clocks_offsets_sampling();
    std::cout << "V4 native occurrence runtime: all checks passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
