#include <trance/visual/pattern_text_v4.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <map>
#include <limits>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using J = nlohmann::json;
constexpr size_t source_limit = 4u * 1024u * 1024u, depth_limit = 64, child_limit = 4096;
const std::map<std::string, std::vector<std::string>> properties = {
  {"image", {"zoom", "baseline", "opacity", "envelope", "animation", "flip"}},
  {"word", {"visible", "chance"}}, {"caption", {}}, {"subtext", {}}, {"spiral", {"speed"}}};
const std::map<std::string, std::string> aliases = {{"baseline", "zoom_baseline"}, {"flip", "flip_probability"},
  {"chance", "selection_probability"}, {"visible", "visibility"}};
const std::map<std::string, std::set<std::string>> sources = {
  {"image", {"primary", "secondary", "either", "alternate"}},
  {"word", {"primary", "secondary", "either"}}, {"caption", {"primary", "either"}}, {"subtext", {"secondary"}}};
bool group(const std::string& s) { return s == "parallel" || s == "sequence" || s == "cycle"; }
std::string alias(const std::string& p) { auto i = aliases.find(p); return i == aliases.end() ? p : i->second; }
bool identifier(const std::string& s) {
  static const std::regex re("[A-Za-z_][A-Za-z_0-9]*"); return std::regex_match(s, re);
}
bool numeric(const std::string& s) {
  static const std::regex re(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"); return std::regex_match(s, re);
}
class Reader {
  const std::string& source;
  size_t position = 0, start = 0;
  std::string token;
  [[noreturn]] void fail(const std::string& what) const {
    size_t line = 1, column = 1;
    for (size_t i = 0; i < start; ++i) { if (source[i] == '\n') { ++line; column = 1; } else ++column; }
    throw std::runtime_error(what + " at line " + std::to_string(line) + ", column " + std::to_string(column));
  }
  void next() {
    while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) ++position;
    start = position;
    if (position == source.size()) { token.clear(); return; }
    static const std::regex re(R"((?:->|[A-Za-z_][A-Za-z_0-9]*|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?|[{}();,:@/]))");
    std::match_results<std::string::const_iterator> match;
    if (!std::regex_search(source.begin() + static_cast<std::ptrdiff_t>(position), source.end(), match, re,
                           std::regex_constants::match_continuous)) fail("Unexpected character");
    token = match.str(); position += token.size();
  }
  void take(const std::string& expected) { if (token != expected) fail("Expected " + expected); next(); }
  std::string id() { if (!identifier(token)) fail("Expected identifier"); auto result = token; next(); return result; }
  J number(bool integer = false, double minimum = -std::numeric_limits<double>::infinity()) {
    if (!numeric(token)) fail("Expected number");
    // strtod preserves finite subnormal/underflow behavior shared with JS Number.
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) fail("Expected complete number");
    if (!std::isfinite(value) || value < minimum || (integer && (value != std::floor(value) || value > 2147483647.0)))
      fail(integer ? "Expected bounded integer" : "Expected finite number");
    next(); return integer ? J(static_cast<int64_t>(value)) : J(value);
  }
  J probability() {
    auto first = number(false, 0); double a = first.get<double>();
    if (token == "/") {
      if (a != std::floor(a) || a > 2147483647.0) fail("Expected integer numerator");
      next(); auto denominator = number(true, 1);
      if (a > denominator.get<double>()) fail("Probability exceeds one");
      return {{"numerator", static_cast<int64_t>(a)}, {"denominator", denominator}};
    }
    if (a > 1) fail("Probability exceeds one"); return first;
  }
  std::vector<J> tuple(size_t count, bool integer = false) {
    take("("); std::vector<J> result;
    for (size_t i = 0; i < count; ++i) { if (i) take(","); result.push_back(number(integer, integer ? 0 : -std::numeric_limits<double>::infinity())); }
    take(")"); return result;
  }
  J curve() {
    auto from = number(); take("->"); auto to = number(); std::string scope = "local";
    if (token == "@") { next(); scope = id(); }
    return {{"curve", {{"from", from}, {"to", to}, {"scope", scope}}}};
  }
  J scalar(const std::string& property) {
    if (token == "sum" && property == "zoom") {
      next(); take("("); auto a = curve(); take(","); auto b = curve(); take(")");
      return {{"sum", J::array({a, b})}};
    }
    if (token == "triangle" && property == "opacity") {
      next(); auto a = tuple(3); return {{"triangle", {{"from", a[0]}, {"peak", a[1]}, {"to", a[2]}}}};
    }
    auto from = number();
    if (token != "->") {
      if (property == "zoom" || property == "baseline") fail("Expected linear curve");
      return from;
    }
    if (property == "opacity") fail("Opacity supports a constant or triangle");
    next(); auto to = number(); std::string scope = "local";
    if (token == "@") { next(); scope = id(); }
    return {{"curve", {{"from", from}, {"to", to}, {"scope", scope}}}};
  }
  J leaf() {
    auto op = id(); auto pi = properties.find(op); if (pi == properties.end()) fail("Unknown output " + op);
    J node = {{"op", op}};
    if (op != "spiral") { auto s = id(); if (!sources.at(op).count(s)) fail("Unsupported output source"); node["source"] = s; }
    while (token != ";") {
      auto p = id(); if (std::find(pi->second.begin(), pi->second.end(), p) == pi->second.end()) fail("Unsupported output property " + p);
      auto key = alias(p); if (node.contains(key)) fail("Duplicate property " + p);
      if (p == "animation") node[key] = {{"every", number(true, 1)}};
      else if (p == "envelope") { auto v = tuple(3, true); node[key] = {{"in", v[0]}, {"hold", v[1]}, {"out", v[2]}}; }
      else if (p == "visible") { auto v = tuple(2); node[key] = J::array({v[0], v[1]}); }
      else if (p == "flip" || p == "chance") node[key] = probability();
      else node[key] = scalar(p);
    }
    if (node.contains("opacity") && node.contains("envelope")) fail("Opacity and envelope are exclusive");
    if (node.contains("flip_probability") && node["source"] != "alternate") fail("Flip requires alternate source");
    next(); return node;
  }
  J composition(size_t depth = 0) {
    if (depth >= depth_limit) fail("Maximum composition depth is 64");
    auto kind = id(); if (!group(kind)) fail("Expected composition");
    J node = {{"kind", kind == "sequence" ? "sequential" : kind == "cycle" ? "random" : "parallel"}, {"children", J::array()}};
    if (kind == "cycle") node["select"] = {{"order", "round_robin"}, {"allow_self", false}, {"first", 0}};
    std::set<std::string> seen;
    while (token != "{") {
      auto clause = id(); if (!seen.insert(clause).second) fail("Duplicate clause " + clause);
      if (clause == "id") node["id"] = id();
      else if (clause == "for") {
        if (token == "sample") { next(); auto v = tuple(3, true); node["duration"] = {{"min", v[0]}, {"max", v[1]}, {"step", v[2]}}; }
        else node["duration"] = number(true, 1);
      } else if (clause == "repeat") node["repeat"] = number(true, 1);
      else if (clause == "phase") node["offset"] = number(true, 0);
      else if (clause == "every") {
        if (token == "ramp") {
          next(); take("("); auto from = number(true, 1); take(","); auto to = number(true, 1); take(",");
          auto steps = number(true, 1); take(","); take("early"); take(")");
          node["cadence"] = {{"ramp", {{"from", from}, {"to", to}, {"steps", steps}, {"ease", "early"}}}};
        } else node["cadence"] = number(true, 1);
      } else if (clause == "cooldown" && kind == "cycle") node["select"]["cooldown"] = number(true, 0);
      else if (clause == "child_mode" && kind != "cycle") {
        auto mode = id(); if (mode != "independent" && mode != "loop" && mode != "wait") fail("Unknown child_mode"); node["child_mode"] = mode;
      } else fail("Unknown composition clause " + clause);
    }
    if (node.value("child_mode", "") == "wait" && (node.contains("duration") || node.contains("cadence") || node.contains("offset") || node.contains("repeat")))
      fail("Wait owner cannot have for, every, phase or repeat");
    next();
    while (token != "}") {
      if (token.empty()) fail("Expected }");
      if (node["children"].size() >= child_limit) fail("Maximum children count is 4096");
      J edge = J::object();
      if (token == "exit") {
        if (kind != "cycle") fail("Exit requires a cycle edge");
        next(); auto p = probability(); take("every"); auto interval = number(true, 1); take(":");
        edge["exit"] = {{"probability", p}, {"check_every", interval}};
      }
      if (group(token)) edge["node"] = composition(depth + 1);
      else { if (kind != "parallel" || edge.contains("exit")) fail("This edge requires a composition"); edge["node"] = leaf(); }
      node["children"].push_back(edge);
    }
    next(); return node;
  }
public:
  explicit Reader(const std::string& s): source(s) {}
  J read() { next(); take("v4"); take(";"); auto root = composition(); if (!token.empty()) fail("Unexpected trailing input"); return {{"version", 4}, {"root", root}}; }
};

[[noreturn]] void unsupported(const std::string& s) { throw std::runtime_error("Unsupported V4 text AST: " + s); }
void shape(const J& j, std::initializer_list<const char*> required, std::initializer_list<const char*> optional = {}) {
  if (!j.is_object()) unsupported("expected object"); std::set<std::string> allowed;
  for (auto k : required) { allowed.insert(k); if (!j.contains(k)) unsupported(std::string("missing ") + k); }
  for (auto k : optional) allowed.insert(k);
  for (auto it = j.begin(); it != j.end(); ++it) if (!allowed.count(it.key())) unsupported("field " + it.key());
}
std::string num(const J& j, bool integer = false, double minimum = -std::numeric_limits<double>::infinity()) {
  if (!j.is_number()) unsupported("expected number"); double value = j.get<double>();
  if (!std::isfinite(value) || value < minimum || (integer && (value != std::floor(value) || value > 2147483647.0))) unsupported("invalid number");
  auto text = j.dump();
  if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.resize(text.size() - 2);
  return text;
}
std::string id(const J& j) { if (!j.is_string() || !identifier(j.get<std::string>())) unsupported("invalid identifier"); return j.get<std::string>(); }
std::string probability(const J& j) {
  if (j.is_number()) { if (j.get<double>() > 1) unsupported("probability exceeds one"); return num(j, false, 0); }
  shape(j, {"numerator", "denominator"}); auto a = num(j["numerator"], true, 0), b = num(j["denominator"], true, 1);
  if (j["numerator"].get<double>() > j["denominator"].get<double>()) unsupported("probability exceeds one"); return a + "/" + b;
}
std::string curve(const J& j) {
  shape(j, {"curve"}); auto& c = j["curve"]; shape(c, {"from", "to"}, {"scope"});
  auto scope = c.contains("scope") ? id(c["scope"]) : "local";
  return num(c["from"]) + " -> " + num(c["to"]) + (scope == "local" ? "" : " @" + scope);
}
std::string scalar(const J& j, const std::string& property) {
  if (j.is_number()) { if (property == "zoom" || property == "baseline") unsupported("expected linear curve"); return num(j); }
  if (property == "zoom" && j.contains("sum")) {
    shape(j, {"sum"}); auto& a = j["sum"]; if (!a.is_array() || a.size() != 2) unsupported("sum requires two curves");
    return "sum(" + curve(a[0]) + ", " + curve(a[1]) + ")";
  }
  if (property == "opacity") {
    shape(j, {"triangle"}); auto& t = j["triangle"]; shape(t, {"from", "peak", "to"});
    return "triangle(" + num(t["from"]) + ", " + num(t["peak"]) + ", " + num(t["to"]) + ")";
  }
  return curve(j);
}
std::string leaf(const J& j) {
  if (!j.is_object() || !j.contains("op") || !j["op"].is_string()) unsupported("output");
  auto op = j["op"].get<std::string>(); auto pi = properties.find(op); if (pi == properties.end()) unsupported("output");
  std::set<std::string> allowed = {"op"}; if (op != "spiral") allowed.insert("source");
  for (auto& p : pi->second) allowed.insert(alias(p));
  for (auto it = j.begin(); it != j.end(); ++it) if (!allowed.count(it.key())) unsupported("field " + it.key());
  auto text = op;
  if (op != "spiral") {
    if (!j.contains("source") || !j["source"].is_string() || !sources.at(op).count(j["source"].get<std::string>())) unsupported("output source");
    text += " " + j["source"].get<std::string>();
  }
  if (j.contains("opacity") && j.contains("envelope")) unsupported("opacity and envelope");
  if (j.contains("flip_probability") && j["source"] != "alternate") unsupported("flip source");
  for (auto& p : pi->second) {
    auto key = alias(p); if (!j.contains(key)) continue; auto& v = j[key];
    if (p == "envelope") { shape(v, {"in", "hold", "out"}); text += " envelope(" + num(v["in"], true, 0) + ", " + num(v["hold"], true, 0) + ", " + num(v["out"], true, 0) + ")"; }
    else if (p == "visible") { if (!v.is_array() || v.size() != 2) unsupported("visibility"); text += " visible(" + num(v[0]) + ", " + num(v[1]) + ")"; }
    else if (p == "animation") { shape(v, {"every"}); text += " animation " + num(v["every"], true, 1); }
    else if (p == "flip" || p == "chance") text += " " + p + " " + probability(v);
    else text += " " + p + " " + scalar(v, p);
  }
  return text + ";";
}
void budget(size_t& bytes, const std::string& text) {
  bytes += text.size(); if (bytes > source_limit) unsupported("formatted source exceeds 4 MiB");
}
std::string composition(const J& j, size_t depth, size_t& bytes, const std::string& prefix = "") {
  if (depth >= depth_limit) unsupported("maximum composition depth is 64");
  shape(j, {"kind", "children"}, {"id", "duration", "repeat", "cadence", "offset", "select", "child_mode"});
  std::string kind;
  if (j["kind"] == "parallel") kind = "parallel"; else if (j["kind"] == "sequential") kind = "sequence";
  else if (j["kind"] == "random") kind = "cycle"; else unsupported("composition kind");
  if (!j["children"].is_array()) unsupported("children must be an array");
  if (j["children"].size() > child_limit) unsupported("maximum children count is 4096");
  std::string indent(depth * 2, ' '), text = indent + prefix + kind;
  if (j.contains("id")) text += " id " + id(j["id"]);
  if (j.contains("duration")) {
    auto& d = j["duration"];
    if (d.is_number()) text += " for " + num(d, true, 1);
    else { shape(d, {"min", "max", "step"}); text += " for sample(" + num(d["min"], true, 0) + ", " + num(d["max"], true, 0) + ", " + num(d["step"], true, 0) + ")"; }
  }
  if (j.contains("repeat")) text += " repeat " + num(j["repeat"], true, 1);
  if (j.contains("cadence")) {
    auto& c = j["cadence"];
    if (c.is_number()) text += " every " + num(c, true, 1);
    else {
      shape(c, {"ramp"}); auto& r = c["ramp"]; shape(r, {"from", "to", "steps", "ease"});
      if (r["ease"] != "early") unsupported("cadence easing");
      text += " every ramp(" + num(r["from"], true, 1) + ", " + num(r["to"], true, 1) + ", " + num(r["steps"], true, 1) + ", early)";
    }
  }
  if (j.contains("offset")) text += " phase " + num(j["offset"], true, 0);
  if (kind == "cycle") {
    if (!j.contains("select")) unsupported("missing selector"); auto& s = j["select"];
    shape(s, {"order", "allow_self", "first"}, {"cooldown"});
    if (s["order"] != "round_robin" || s["allow_self"] != false || s["first"] != 0) unsupported("cycle selector");
    if (s.contains("cooldown")) text += " cooldown " + num(s["cooldown"], true, 0);
    if (j.contains("child_mode")) unsupported("cycle child_mode");
  } else if (j.contains("select")) unsupported("selector on non-cycle");
  if (j.contains("child_mode")) {
    auto& m = j["child_mode"]; if (m != "independent" && m != "loop" && m != "wait") unsupported("child_mode");
    if (m != "independent") text += " child_mode " + m.get<std::string>();
    if (m == "wait" && (j.contains("duration") || j.contains("cadence") || j.contains("offset") || j.contains("repeat"))) unsupported("wait owner timing");
  }
  text += " {\n";
  budget(bytes, text);
  for (auto& edge : j["children"]) {
    shape(edge, {"node"}, {"exit"}); auto& n = edge["node"]; std::string p;
    if (edge.contains("exit")) {
      if (kind != "cycle" || !n.contains("kind")) unsupported("exit edge"); auto& e = edge["exit"];
      shape(e, {"probability", "check_every"}); p = "exit " + probability(e["probability"]) + " every " + num(e["check_every"], true, 1) + ": ";
    }
    if (n.contains("kind")) text += composition(n, depth + 1, bytes, p) + "\n";
    else {
      if (kind != "parallel") unsupported("output on non-parallel");
      auto line = indent + "  " + leaf(n) + "\n"; budget(bytes, line); text += line;
    }
  }
  budget(bytes, indent + "}\n"); return text + indent + "}";
}
}

namespace patternv4 {
std::string parse_text_json(const std::string& source) {
  if (source.size() > source_limit) throw std::runtime_error("Source exceeds 4 MiB at line 1, column 1");
  std::string text = source; if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
  return Reader(text).read().dump();
}
std::string format_text_json(const std::string& source) {
  if (source.size() > source_limit) unsupported("source exceeds 4 MiB");
  auto bounded = [](int depth, J::parse_event_t, J&) {
    if (depth > 256) unsupported("JSON nesting exceeds safety limit"); return true;
  };
  auto j = J::parse(source, bounded); shape(j, {"version", "root"}); if (j["version"] != 4) unsupported("version");
  size_t bytes = 4;
  return "v4;\n" + composition(j["root"], 0, bytes) + "\n";
}
}
