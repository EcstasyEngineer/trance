#include <trance/visual/pattern_parser.h>
#include <trance/visual/pattern_parser_v4.h>

#include <iostream>
#include <trance/visual/render_eval.h>
#include <memory>
#include <cmath>
#include <limits>
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
    // Inherit on the root resolves independently finite timed subpatterns.
    J inherit = {{"kind", "sequential"},
                 {"children", J::array({{{"node", node("parallel", 2)}},
                                         {{"node", node("parallel", 3)}}})}};
    auto inferred = patternv4::parse(document(inherit).dump());
    require(inferred.ok, inferred.error.c_str());
    require(inferred.root.length == 5, "inherited sequence sum");
    pattern::NodeMap clocks;
    std::unique_ptr<Cycler> scheduler(pattern::compile(inferred.root, {}, clocks));
    for (unsigned i = 0; i < 5; ++i) scheduler->advance();
    require(scheduler->complete() && scheduler->elapsed_length() == 5, "inherited execution extent");
    inherit["loop_subpatterns"] = true;
    inferred = patternv4::parse(document(inherit).dump());
    require(inferred.ok && inferred.root.length == 5 && inferred.root.loop_subpatterns,
            "looping inherited root has a finite natural bound");
    inherit["kind"] = "parallel";
    inherit["loop_subpatterns"] = false;
    inherit["children"].push_back({{"node", image()}});
    inferred = patternv4::parse(document(inherit).dump());
    require(inferred.ok && inferred.root.length == 3, "inherited max ignores direct outputs");
    auto invalid = inherit; invalid["children"] = J::array({{{"node", image()}}});
    auto unresolved = patternv4::parse(document(invalid).dump());
    require(!unresolved.ok && unresolved.error.find("$.root.duration: Root needs") == 0,
            "unresolved inheritance needs located explanation");
    invalid = inherit; invalid["children"][0]["node"]["duration"] =
        {{"min", 2}, {"max", 4}, {"step", 1}};
    rejected(document(invalid));
    invalid = inherit; invalid["children"][0]["node"]["repeat"] = "forever";
    rejected(document(invalid));
    invalid = inherit; invalid["children"][0]["node"].erase("duration");
    rejected(document(invalid));
    for (const auto mode : {"independent", "loop", "wait"}) {
      invalid = node(); invalid["child_mode"] = mode; rejected(document(invalid));
    }
    invalid = node(); invalid["loop_subpatterns"] = "true"; rejected(document(invalid));
    auto random_mode = node("random");
    random_mode["select"] = {{"order", "random"}, {"allow_self", true}, {"first", 0}};
    random_mode["children"] = J::array({{{"node", node()}}});
    random_mode["loop_subpatterns"] = false;
    rejected(document(random_mode));
    auto nested = node("sequential", 9);
    auto inherited_child = node(); inherited_child.erase("duration");
    inherited_child["id"] = "inherited_child";
    nested["children"] = J::array({{{"node", node("parallel", 2)}}, {{"node", inherited_child}}});
    auto nested_parse = patternv4::parse(document(nested).dump());
    require(nested_parse.ok, nested_parse.error.c_str());
    clocks.clear(); scheduler.reset(pattern::compile(nested_parse.root, {}, clocks));
    for (unsigned i = 0; i < 9; ++i) scheduler->advance();
    require(scheduler->complete() && clocks.at("inherited_child")->length() == 7,
            "sequential inherited child uses remaining owner window");
    nested["kind"] = "parallel";
    nested_parse = patternv4::parse(document(nested).dump());
    require(nested_parse.ok, nested_parse.error.c_str());
    clocks.clear(); scheduler.reset(pattern::compile(nested_parse.root, {}, clocks));
    scheduler->advance();
    require(clocks.at("inherited_child")->length() == 9, "parallel child inherits full owner window");
    // Explicit Inherit derives from children at every level, independently of
    // the enclosing window, and keeps that resolved clock when clipped.
    auto automatic = node("sequential"); automatic["duration"] = "inherit";
    automatic["children"] = J::array({{{"node", node("parallel", 200)}},
                                     {{"node", node("parallel", 300)}}});
    auto automatic_parse = patternv4::parse(document(automatic).dump());
    require(automatic_parse.ok && automatic_parse.root.length == 500, "explicit inherited sum 200+300");
    auto outer = node("parallel", 2000);
    outer["children"] = J::array({{{"node", automatic}}});
    automatic_parse = patternv4::parse(document(outer).dump());
    require(automatic_parse.ok && automatic_parse.root.children[0].length == 500,
            "nested explicit inherit does not use enclosing 2000-frame clock");
    automatic["children"][1]["node"]["duration"] = 400;
    automatic["loop_subpatterns"] = true;
    automatic_parse = patternv4::parse(document(automatic).dump());
    require(automatic_parse.ok && automatic_parse.root.length == 600,
            "child edit recalculates looping inherited parent bound");
    auto upper = automatic; upper["kind"] = "parallel";
    upper["children"] = J::array({{{"node", automatic}}, {{"node", node("parallel", 250)}}});
    upper["repeat"] = 2;
    automatic_parse = patternv4::parse(document(upper).dump());
    require(automatic_parse.ok && automatic_parse.root.length == 600 &&
            automatic_parse.root.children[0].length == 600 && automatic_parse.root.repeat_count == 2,
            "nested bottom-up inherit and parallel max with owner repetition");
    automatic["repeat"] = 2;
    upper["children"][0]["node"] = automatic;
    automatic_parse = patternv4::parse(document(upper).dump());
    require(automatic_parse.ok && automatic_parse.root.length == 1200,
            "inherited child finite repeats included once in parent span");
    auto circular = node(); circular["duration"] = "inherit";
    circular["children"] = J::array({{{"node", inherited_child}}});
    rejected(document(circular));
    invalid = node(); invalid["duration"] = "inherit"; rejected(document(invalid));
    invalid = automatic; invalid["duration"] = "parent"; rejected(document(invalid));
    invalid = random_mode; invalid.erase("loop_subpatterns"); invalid["duration"] = "inherit";
    rejected(document(invalid));
    automatic.erase("repeat"); automatic.erase("loop_subpatterns");
    outer["duration"] = 100;
    outer["children"][0]["node"] = automatic;
    automatic_parse = patternv4::parse(document(outer).dump());
    require(automatic_parse.ok && automatic_parse.root.children[0].length == 600,
            "ancestor clipping preserves resolved inherited clock");
    auto deep_derived = node("parallel", 2);
    for (unsigned level = 0; level < 40; ++level)
      deep_derived = {{"kind", "sequential"}, {"duration", "inherit"},
                      {"children", J::array({{{"node", deep_derived}}})}};
    const auto deep_parse = patternv4::parse(document(deep_derived).dump());
    require(deep_parse.ok && deep_parse.root.length == 2,
            "deep bottom-up derivation remains bounded without duplicate traversal");
    // Fitting requirements and preroll budgets use the actual remaining window.
    auto short_owner = node("sequential", 10);
    auto fitted = node(); fitted.erase("duration");
    fitted["children"][0]["node"]["envelope"] = {{"in", 2}, {"hold", 2}, {"out", 2}};
    short_owner["children"] = J::array({{{"node", node("parallel", 8)}}, {{"node", fitted}}});
    rejected(document(short_owner));
    fitted["duration"] = 6;
    short_owner["children"][1]["node"] = fitted;
    auto explicit_clipped = patternv4::parse(document(short_owner).dump());
    require(explicit_clipped.ok, "explicit fitting duration may be intentionally clipped");
    auto phase_owner = node("sequential", 4000000);
    auto phased = node(); phased.erase("duration"); phased["offset"] = 3999999;
    phase_owner["children"] = J::array({{{"node", node("parallel", 1000000)}}, {{"node", phased}}});
    rejected(document(phase_owner));
    phase_owner["children"][1]["node"]["offset"] = 1;
    rejected(document(phase_owner));
    phase_owner["children"][1]["node"]["offset"] = 2999999;
    require(patternv4::parse(document(phase_owner).dump()).ok, "safe exact remaining-window phase");
    auto variable = node("parallel", 10);
    variable["loop_subpatterns"] = true;
    fitted.erase("duration");
    variable["children"] = J::array({{{"node", fitted}}});
    rejected(document(variable));
    variable["children"][0]["node"]["duration"] = 6;
    require(patternv4::parse(document(variable).dump()).ok, "fixed duration resolves variable envelope");
    variable["children"][0]["node"].erase("duration");
    variable["children"][0]["node"]["cadence"] = 6;
    require(patternv4::parse(document(variable).dump()).ok, "fixed cadence resolves variable envelope");
    variable["children"][0]["node"] = phased;
    rejected(document(variable));
    variable["children"][0]["node"]["cadence"] = 8;
    require(patternv4::parse(document(variable).dump()).ok, "fixed cadence resolves variable phase");
    variable.erase("loop_subpatterns"); variable["cadence"] = 6;
    variable["children"][0]["node"] = fitted;
    rejected(document(variable));
    auto capped = node("sequential", 20);
    capped["children"] = J::array({{{"node", node("parallel", 2)}},
                                   {{"node", node("parallel", 3)}}});
    capped["repeat"] = 2;
    inherit["children"] = J::array({{{"node", capped}}});
    inferred = patternv4::parse(document(inherit).dump());
    require(inferred.ok && inferred.root.length == 10, "inherit uses actual repeated lifetime");
    auto warned = patternv4::parse(document(capped).dump());
    require(warned.ok && !warned.warnings.empty(), "unused span advisory");
    capped["duration"] = 4;
    warned = patternv4::parse(document(capped).dump());
    require(warned.ok && !warned.warnings.empty() &&
            warned.warnings.back().find("clipped") != std::string::npos, "clipping remains advisory");
    // Assert values produced by the actual lowering and evaluator, not lint success.
    pattern::Registers regs;
    for (const double value : {1e-5, 1e-7, 1e20, -1e-12, 1e-320, -1e-320,
                               std::numeric_limits<double>::denorm_min()}) {
      auto numeric = node(); numeric["children"][0]["node"]["opacity"] = value;
      auto parsed = patternv4::parse(document(numeric).dump());
      require(parsed.ok, parsed.error.c_str());
      require(pattern::eval_expr(parsed.render_block[0].alpha, 0, regs, {}, nullptr) == value,
              "scientific scalar changed during lowering");
    }
    auto curve = node("parallel", 4);
    curve["children"][0]["node"]["opacity"] =
        {{"curve", {{"from", 1e-7}, {"to", 3e-7}}}};
    auto parsed_curve = patternv4::parse(document(curve).dump());
    require(parsed_curve.ok, parsed_curve.error.c_str());
    clocks.clear();
    scheduler.reset(pattern::compile(parsed_curve.root, {}, clocks));
    scheduler->advance();
    require(pattern::eval_expr(parsed_curve.render_block[0].alpha, 0, regs, clocks,
                              scheduler.get()) == 1e-7, "scientific curve start");
    scheduler->advance(); scheduler->advance();
    require(std::abs(pattern::eval_expr(parsed_curve.render_block[0].alpha, 0, regs, clocks,
                                       scheduler.get()) - 2e-7) < 1e-22,
            "scientific curve midpoint");
    for (const auto malformed : {"1e", "1e+", "1e-", "1.2.3", "1e2tail"}) {
      bool threw = false;
      try { (void) pattern::eval_expr(malformed, 0, regs, {}, nullptr); }
      catch (const std::exception&) { threw = true; }
      require(threw, "malformed numeric prefix accepted");
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
