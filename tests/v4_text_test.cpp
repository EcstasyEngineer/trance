#include <trance/visual/builtin_patterns.h>
#include <trance/visual/pattern_parser.h>
#include <trance/visual/pattern_text_v4.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/render_eval.h>
#include <common/util.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace {
using J = nlohmann::json;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::vector<double> trace(const pattern::ParseResult& parsed, uint32_t count) {
  get_mersenne_twister().seed(7411);
  pattern::NodeMap clocks;
  std::vector<double> values;
  unsigned frame = 0, action_index = 0;
  std::unique_ptr<Cycler> root(pattern::compile(parsed.root, [&](const pattern::Node&) {
    const auto index = action_index++;
    return [&, index] { values.push_back(-1); values.push_back(frame); values.push_back(index); };
  }, clocks));
  pattern::Registers regs;
  for (; frame < count; ++frame) {
    root->advance();
    for (const auto& stmt : parsed.render_block) {
      for (const auto& expression : {stmt.alpha, stmt.origin, stmt.zoom, stmt.shadow_origin,
          stmt.shadow_zoom, stmt.speed, stmt.when, stmt.anim_gate}) {
        if (!expression.empty()) values.push_back(pattern::detail::Eval(expression, regs, clocks, root.get()).run());
      }
    }
    values.push_back(root->position()); values.push_back(root->complete());
  }
  return values;
}
void reject(const std::string& text) {
  const auto parsed = pattern::parse_source(text);
  require(!parsed.ok, "invalid text accepted: " + text);
  require(parsed.error.find("line ") != std::string::npos && parsed.error.find("column ") != std::string::npos,
          "syntax error lacks source location: " + parsed.error);
}
}
int main() {
  try {
    std::ifstream fixture_file(TRANCE_V4_TEXT_FIXTURES);
    require(bool(fixture_file), "missing shared text fixtures");
    J goldens; fixture_file >> goldens;
    require(goldens.is_array() && goldens.size() == 8, "expected eight shared text fixtures");
    for (uint32_t type = 1; type <= 8; ++type) {
      const auto source = builtin::pattern_source_v4(type);
      const auto text = patternv4::format_text_json(source);
      const auto lowered = patternv4::parse_text_json(text);
      require(J::parse(patternv4::parse_text_json(goldens[type - 1].get<std::string>())) == J::parse(source),
              "literal text fixture differs from native AST " + std::to_string(type));
      require(J::parse(source) == J::parse(lowered), "AST roundtrip changed builtin " + std::to_string(type));
      require(patternv4::format_text_json(lowered) == text, "unstable canonical text");
      const auto json_program = pattern::parse_source(source), text_program = pattern::parse_source(text);
      require(json_program.ok, json_program.error); require(text_program.ok, text_program.error);
      require(json_program.warnings == text_program.warnings, "frontend warnings changed");
      require(trace(json_program, 4096) == trace(text_program, 4096), "native schedule/value trace changed");
    }
    auto exponent = J::parse(patternv4::parse_text_json("v4; parallel for 32 { spiral speed 1e-7 -> -2.5E+3; }"));
    require(exponent["root"]["children"][0]["node"]["speed"]["curve"]["from"] == 1e-7, "exponent incorrectly tokenized");
    require(pattern::parse_source("v4; parallel for 32 child_mode loop { parallel for 2 { image either; } }").ok,
            "loop clause did not reach native parser");
    require(pattern::parse_source("v4; parallel child_mode wait { parallel for 2 { image either; } }").ok,
            "wait clause did not reach native parser");
    for (const auto* source : {"", "v3; parallel {}", "v4; parallel {} junk", "v4; parallel {", "v4; parallel for 2 for 4 {}",
         "v4; parallel for 1e999 {}", "v4; parallel for 2bad {}", "v4; parallel { spiral speed 1e+; }",
         "v4; parallel { spiral speed 1.2.3; }", "v4; parallel { word primary chance 1/0; }",
         "v4; parallel { image primary flip 0.5; }", "v4; parallel { image either zoom 0.5; }",
         "v4; parallel { image either opacity 0 -> 1; }", "v4; parallel { line primary; }",
         "v4; sequence { image either; }", "v4; parallel child_mode wait repeat 2 {}",
         "v4; parallel every ramp(8,2,4,late) {}", "v4; cycle child_mode loop {}"}) reject(source);
    auto unsupported = J::parse(builtin::pattern_source_v4(1)); unsupported["root"]["weight"] = 1;
    auto before = unsupported.dump(); bool failed = false;
    try { patternv4::format_text_json(unsupported.dump()); } catch (const std::exception&) { failed = true; }
    require(failed && unsupported.dump() == before, "unsupported AST was changed or silently dropped");
    unsupported = J::parse(builtin::pattern_source_v4(8)); unsupported["root"]["children"][0]["node"]["select"]["order"] = "random";
    failed = false; try { patternv4::format_text_json(unsupported.dump()); } catch (const std::exception&) { failed = true; }
    require(failed, "random selector silently formatted as cycle");
    reject(std::string(4u * 1024u * 1024u + 1, ' '));
    std::string deep = "v4; "; for (unsigned i = 0; i < 66; ++i) deep += "parallel { "; deep += std::string(66, '}'); reject(deep);
    std::string wide = "v4; parallel { "; for (unsigned i = 0; i < 4097; ++i) wide += "spiral; "; reject(wide + "}");
    J deep_node = {{"kind", "parallel"}, {"children", J::array()}};
    for (unsigned i = 0; i < 66; ++i) deep_node = {{"kind", "parallel"}, {"children", J::array({{{"node", deep_node}}})}};
    failed = false; try { patternv4::format_text_json(J({{"version", 4}, {"root", deep_node}}).dump()); } catch (const std::exception&) { failed = true; }
    require(failed, "formatter allowed excessive nesting");
    std::cout << "PASS: eight exact AST/text roundtrips, native fixed-seed schedule/value traces, strict diagnostics and formatter refusal\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
