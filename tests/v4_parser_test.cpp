#include <trance/visual/pattern_parser.h>
#include <trance/visual/pattern_parser_v4.h>

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
  using J = nlohmann::json;
  void require(bool value, const char* message)
  {
    if (!value)
      throw std::runtime_error(message);
  }
  J image()
  {
    return {{"op", "image"}, {"source", "either"}, {"animation", {{"every", 2}}}};
  }
  J node(const char* kind = "parallel", uint32_t duration = 32)
  {
    return {{"kind", kind}, {"duration", duration}, {"children", J::array({{{"node", image()}}})}};
  }
  J document(const J& root)
  {
    return {{"version", 4}, {"root", root}};
  }
  void rejected(const J& j)
  {
    auto r = patternv4::parse(j.dump());
    require(!r.ok, "invalid AST accepted");
    require(!r.error.empty(), "rejection lacks diagnostic");
  }
} // namespace
int main()
{
  try {
    auto root = node();
    root["children"].push_back({{"node", image()}});
    auto p = patternv4::parse(document(root).dump());
    require(p.ok, p.error.c_str());
    require(p.root.occurrence_kind == pattern::Node::Composition::Parallel, "wrong composition");
    require(p.render_block.size() == 2, "lost output");
    require(p.render_block[0].image_reg != p.render_block[1].image_reg, "image identity shared");
    for (auto parent : {"parallel", "sequential", "random"})
      for (auto child : {"parallel", "sequential", "random"}) {
        J leaf = node();
        J inside = node(child);
        if (std::string(child) != "parallel")
          inside["children"] = J::array({{{"node", leaf}}});
        if (std::string(child) == "random")
          inside["select"] = {{"order", "random"}, {"allow_self", true}, {"first", 0}};
        J outside = node(parent);
        outside["children"] = J::array({{{"node", inside}}});
        if (std::string(parent) == "random")
          outside["select"] = {{"order", "round_robin"}, {"allow_self", false}, {"first", 0}};
        auto result = patternv4::parse(document(outside).dump());
        require(result.ok, result.error.c_str());
      }
    for (auto probability : {J(0), J(1), J({{"numerator", 1}, {"denominator", 3}})}) {
      J v = node();
      v["children"][0]["node"]["selection_probability"] = probability;
      auto result = patternv4::parse(document(v).dump());
      require(result.ok, result.error.c_str());
      bool exact = false;
      for (auto& e : result.root.effects)
        if (e.kind == pattern::Effect::Kind::Chance) {
          exact = true;
          if (probability.is_object())
            require(e.probability_numerator == 1 && e.probability_denominator == 3,
                    "rational rounded");
        }
      require(exact, "probability missing");
    }
    J v = node();
    v["wat"] = 1;
    rejected(document(v));
    v = node();
    v["duration"] = 4294967295u;
    rejected(document(v));
    v = node();
    v["repeat"] = 2147483647;
    rejected(document(v));
    v = node("parallel", 2147483647);
    v["cadence"] = 2147483647;
    v["offset"] = 1;
    rejected(document(v));
    v = node("parallel", 1000000);
    v["cadence"] = {{"ramp", {{"from", 10}, {"to", 1}, {"steps", 65537}, {"ease", "linear"}}}};
    rejected(document(v));
    v = node();
    v["id"] = "scene";
    rejected(document(v));
    v = node();
    v["children"][0]["node"]["zoom"] = {{"curve", {{"from", 0}, {"to", 1}, {"scope", "sibling"}}}};
    rejected(document(v));
    v = node();
    v["children"][0]["node"]["zoom"] = {{"triangle", {{"from", 0}, {"peak", 1}, {"to", 0}}}};
    rejected(document(v));
    v = node();
    v["children"][0]["node"]["animation"]["every"] = 0;
    rejected(document(v));
    v = node();
    v["children"][0]["node"]["selection_probability"] = {{"numerator", 2}, {"denominator", 1}};
    rejected(document(v));
    v = node();
    v["children"][0]["exit"] = {{"probability", 1}, {"check_every", 1}};
    rejected(document(v));
    v = node("sequential");
    rejected(document(v));
    v = node();
    v["id"] = "parent";
    J c = node();
    c["id"] = "parent_local";
    v["children"].push_back({{"node", c}});
    rejected(document(v));
    v = node();
    c = node();
    c["id"] = "v4scope_1_local";
    v["children"].push_back({{"node", c}});
    auto collision = patternv4::parse(document(v).dump());
    require(collision.ok, collision.error.c_str());
    require(collision.root.id != "v4scope_1", "generated clock alias collides with authored id");
    require(!patternv4::parse("{\"version\":4,\"root\":").ok, "malformed JSON accepted");
    require(!patternv4::parse("{\"version\":4,\"version\":3,\"root\":{}}").ok,
            "duplicate JSON member accepted");
    require(!pattern::parse_source("pattern old for 8f { image primary }").ok,
            "product still accepts V3 text");
    require(pattern::parse_source(document(node()).dump()).ok, "product rejects V4 JSON");
    std::cout << "Native V4 parser: nine composition pairs, private outputs, exact probabilities "
                 "and strict diagnostics passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
