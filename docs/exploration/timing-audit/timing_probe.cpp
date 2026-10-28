// Bounded exploration harness: compiles the production parser/compiler/scheduler.
// No production timing code is copied or changed.
#include <trance/visual/pattern_parser_v4.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/cyclers.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using J = nlohmann::json;
J edge(J n) { return {{"node", std::move(n)}}; }
J leaf(const char* id, unsigned duration) {
  return {{"kind", "parallel"}, {"id", id}, {"duration", duration},
          {"children", J::array({edge({{"op", "image"}, {"source", "primary"}})})}};
}
J sequence(unsigned duration, unsigned a = 600, unsigned b = 600) {
  return {{"kind", "sequential"}, {"id", "seq"}, {"duration", duration},
          {"children", J::array({edge(leaf("a", a)), edge(leaf("b", b))})}};
}
void require(bool p, const char* message) { if (!p) throw std::runtime_error(message); }
struct Run {
  pattern::NodeMap clocks;
  std::unique_ptr<Cycler> root;
  std::vector<std::string> trace;
  std::vector<std::pair<unsigned, std::string>> entries;
  unsigned frame = 0;
  explicit Run(J n) {
    auto parsed = patternv4::parse(J({{"version", 4}, {"root", n}}).dump());
    require(parsed.ok, parsed.error.c_str());
    require(parsed.warnings.empty(), "native parser unexpectedly warns");
    root.reset(pattern::compile(parsed.root, [this](const pattern::Node& node) {
      return [this, id = node.id] { entries.emplace_back(frame, id); };
    }, clocks));
  }
  void tick() {
    root->advance();
    std::string active;
    if (clocks.at("a")->active()) active += "A";
    if (clocks.at("b")->active()) active += "B";
    trace.push_back(active.empty() ? "inactive" : active);
    ++frame;
  }
  void until_complete() { do { tick(); } while (!root->complete() && frame < 5000); }
  void check_span(unsigned begin, unsigned end, const char* label) const {
    for (unsigned i = begin; i < end; ++i) require(trace.at(i) == label, label);
  }
  void print(const char* name) const {
    std::cout << name << ": elapsed=" << root->elapsed_length()
              << " authored_length=" << root->length()
              << " complete=" << root->complete() << "; ";
    unsigned begin = 0;
    for (unsigned i = 1; i <= trace.size(); ++i) {
      if (i == trace.size() || trace[i] != trace[begin]) {
        std::cout << trace[begin] << "[" << begin << "," << i << ") "; begin = i;
      }
    }
    std::cout << '\n';
  }
};
int main() {
  try {
    Run root(sequence(2000)); root.until_complete();
    require(root.frame == 1200 && root.root->position() == 1200, "root must finish at 1200");
    root.check_span(0, 600, "A"); root.check_span(600, 1200, "B"); root.print("root sequence");
    require(root.clocks.at("b")->frame() == 599, "last child frame must be 599");
    root.tick(); require(root.trace.back() == "A", "external root advance restarts");
    require(root.entries.back() == std::make_pair(1200u, std::string("a")), "restart entry tick");
    auto outer = J{{"kind", "parallel"}, {"duration", 2000}, {"children", J::array({edge(sequence(2000))})}};
    Run nested(outer); nested.until_complete();
    nested.check_span(0, 600, "A"); nested.check_span(600, 1200, "B");
    nested.check_span(1200, 2000, "inactive"); nested.print("nested sequence under parallel");
    Run clipped(sequence(1000)); clipped.until_complete();
    clipped.check_span(0, 600, "A"); clipped.check_span(600, 1000, "B"); clipped.print("clipped sequence");
    auto repeated = sequence(2000); repeated["repeat"] = 2;
    Run twice(repeated); twice.until_complete();
    require(twice.frame == 2400, "repeat counts actual sequence iterations");
    twice.check_span(1200, 1800, "A"); twice.check_span(1800, 2400, "B"); twice.print("explicit repeat 2");
    auto cadenced = sequence(2000); cadenced["cadence"] = 1200;
    Run cadence(cadenced); cadence.until_complete();
    cadence.check_span(0, 600, "A"); cadence.check_span(600, 1200, "B");
    cadence.check_span(1200, 1800, "A"); cadence.check_span(1800, 2000, "B"); cadence.print("explicit cadence 1200");
    auto together = sequence(2000); together["kind"] = "parallel";
    Run parallel(together); parallel.until_complete();
    parallel.check_span(0, 600, "AB"); parallel.check_span(600, 2000, "inactive"); parallel.print("parallel finite children");
    auto loop = sequence(1200); loop["repeat"] = "forever";
    outer["children"] = J::array({edge(loop)});
    Run forever(outer); forever.until_complete();
    forever.check_span(0, 600, "A"); forever.check_span(600, 1200, "B");
    forever.check_span(1200, 1800, "A"); forever.check_span(1800, 2000, "B"); forever.print("explicit nested forever");
    outer["duration"] = 1000;
    outer["children"] = J::array({edge(sequence(1000, 400, 400))});
    Run paste(outer); paste.until_complete();
    paste.check_span(0, 400, "A"); paste.check_span(400, 800, "B");
    paste.check_span(800, 1000, "inactive"); paste.print("1000 parent, 400 plus 400");
    auto inferred = sequence(2000); inferred.erase("duration");
    auto rejected = patternv4::parse(J({{"version", 4}, {"root", inferred}}).dump());
    require(!rejected.ok, "root without duration must be rejected");
    J sequential_parent{{"kind", "sequential"}, {"id", "parent"}, {"duration", 2000},
                        {"children", J::array({edge(inferred)})}};
    rejected = patternv4::parse(J({{"version", 4}, {"root", sequential_parent}}).dump());
    require(!rejected.ok, "sequential child without duration must be rejected");
    outer["duration"] = 2000; outer["children"] = J::array({edge(inferred)});
    Run omitted(outer); omitted.until_complete();
    omitted.check_span(1200, 2000, "inactive");
    require(omitted.clocks.at("seq")->length() == 2000, "omitted duration inherits clock span");
    require(omitted.clocks.at("seq")->elapsed_length() == 1200, "inherited sequence ends early");
    omitted.print("nested omitted sequential duration");
    std::cout << "PASS native parse -> compile -> scheduler: nine cases, two inference rejections, and exact boundaries\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
