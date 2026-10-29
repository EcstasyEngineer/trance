#include <trance/visual/pattern_parser_v4.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
  using J = nlohmann::json;
  using N = pattern::Node;
  using E = pattern::Effect;
  using R = pattern::RenderStmt;
  constexpr uint32_t limit = 2147483647u;
  [[noreturn]] void fail(const std::string& path, const std::string& what)
  {
    throw std::runtime_error(path + ": " + what);
  }
  void object(const J& j, const std::string& p, std::initializer_list<const char*> required,
              std::initializer_list<const char*> optional = {})
  {
    if (!j.is_object())
      fail(p, "expected object");
    std::set<std::string> allowed;
    for (auto k : required) {
      allowed.insert(k);
      if (!j.contains(k))
        fail(p, std::string("missing field ") + k);
    }
    for (auto k : optional)
      allowed.insert(k);
    for (auto it = j.begin(); it != j.end(); ++it)
      if (!allowed.count(it.key()))
        fail(p, "unknown field " + it.key());
  }
  double number(const J& j, const std::string& p, double lo = -1e30, double hi = 1e30)
  {
    if (!j.is_number())
      fail(p, "expected finite number");
    double d = j.get<double>();
    if (!std::isfinite(d) || d < lo || d > hi)
      fail(p, "number outside allowed range");
    return d;
  }
  uint32_t integer(const J& j, const std::string& p, uint32_t lo = 0, uint32_t hi = limit)
  {
    if (!j.is_number_integer() && !j.is_number_unsigned())
      fail(p, "expected integer");
    return static_cast<uint32_t>(number(j, p, double(lo), double(hi)));
  }
  std::string str(const J& j, const std::string& p)
  {
    if (!j.is_string())
      fail(p, "expected string");
    return j.get<std::string>();
  }
  std::string num(double d)
  {
    std::ostringstream s;
    s.precision(17);
    s << d;
    return s.str();
  }
  std::pair<uint32_t, uint32_t> probability(const J& j, const std::string& p)
  {
    uint32_t a, b;
    if (j.is_object()) {
      object(j, p, {"numerator", "denominator"});
      b = integer(j["denominator"], p, 1);
      a = integer(j["numerator"], p, 0, b);
    } else {
      double d = number(j, p, 0, 1);
      b = 1000000;
      a = static_cast<uint32_t>(std::llround(d * b));
      if (std::abs(double(a) / b - d) > 1e-12)
        fail(p,
             "decimal probability needs at most six decimal places; use an exact rational object");
    }
    uint32_t g = std::gcd(a, b);
    return {a / g, b / g};
  }
  // Exact finite completion within a known owner window; no scheduler/RNG probing.
  std::optional<uint64_t> lifetime(const J& j, uint32_t inherited = 0)
  {
    if (!j.is_object() || !j.contains("kind") || !j.contains("children") ||
        !j["children"].is_array() || j.contains("offset")) return {};
    if (j.contains("repeat") && !j["repeat"].is_number_integer()) return {};
    uint32_t cap = inherited;
    if (j.contains("duration")) {
      if (!j["duration"].is_number_integer()) return {};
      cap = integer(j["duration"], "$.duration", 1);
    }
    if (!cap) return {};
    uint64_t extent = cap;
    if (j.value("kind", std::string{}) == "sequential" &&
        !j.value("loop_subpatterns", false) && !j.contains("cadence")) {
      extent = 0;
      for (const auto& edge : j["children"]) {
        if (extent >= cap) break;
        if (!edge.is_object() || !edge.contains("node")) return {};
        auto length = lifetime(edge["node"], cap - static_cast<uint32_t>(extent));
        if (!length) return {};
        extent = std::min<uint64_t>(cap, extent + *length);
      }
    }
    if (j.contains("repeat")) extent *= integer(j["repeat"], "$.repeat", 1);
    if (!extent || extent > limit) return {};
    return extent;
  }
  std::optional<uint32_t> inherited_root_span(const J& j)
  {
    if (!j.contains("children") || !j["children"].is_array() || j["kind"] == "random") return {};
    uint64_t span = 0;
    for (const auto& edge : j["children"]) {
      if (!edge.is_object() || !edge.contains("node")) return {};
      const auto& child = edge["node"];
      if (child.contains("op")) continue;
      const auto length = lifetime(child);
      if (!length) return {};
      if (j["kind"] == "sequential") span += *length;
      else span = std::max(span, *length);
      if (span > limit) return {};
    }
    if (!span) return {};
    return static_cast<uint32_t>(span);
  }
  struct Parser {
    std::set<std::string> ids, generated;
    uint32_t serial = 0;
    std::string scene;
    std::vector<R> render;
    std::vector<std::string> warnings;
    void reserve(const J& j, const std::string& p, uint32_t depth = 0)
    {
      if (depth > 64)
        fail(p, "maximum nesting depth is 64");
      if (!j.is_object())
        fail(p, "expected node object");
      if (j.contains("id")) {
        std::string id = str(j["id"], p + ".id");
        if (id.empty() || !(std::isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_') ||
            !std::all_of(
                id.begin(), id.end(),
                [](unsigned char c) { return c < 128 && (std::isalnum(c) || c == '_'); }) ||
            id == "root" || id == "local" || id == "scene" || id == "self" || id == "this" ||
            !ids.insert(id).second)
          fail(p, "id must be a unique ASCII identifier outside reserved clock aliases");
      }
      if (j.contains("children") && j["children"].is_array())
        for (size_t i = 0; i < j["children"].size(); ++i) {
          const auto& e = j["children"][i];
          if (e.is_object() && e.contains("node") && e["node"].is_object() &&
              !e["node"].contains("op"))
            reserve(e["node"], p + ".children[" + std::to_string(i) + "].node", depth + 1);
        }
    }
    std::string name(const J& j)
    {
      if (j.contains("id"))
        return j["id"].get<std::string>();
      std::string s;
      do {
        s = "v4scope_" + std::to_string(++serial);
      } while (ids.count(s) || ids.count(s + "_local") || generated.count(s));
      generated.insert(s);
      return s;
    }
    std::string scalar(const J& j, const std::string& p, const std::string& local,
                       const std::vector<std::string>& ancestors, bool triangle = false,
                       uint32_t depth = 0)
    {
      if (depth > 32)
        fail(p, "scalar nesting exceeds 32");
      if (!j.is_object())
        return num(number(j, p));
      if (j.contains("sum")) {
        object(j, p, {"sum"});
        auto& a = j["sum"];
        if (!a.is_array() || a.size() < 2 || a.size() > 8)
          fail(p, "sum needs two through eight terms");
        std::string s;
        for (size_t i = 0; i < a.size(); ++i) {
          if (i)
            s += " + ";
          s += "(" + scalar(a[i], p + ".sum", local, ancestors, false, depth + 1) + ")";
        }
        return s;
      }
      if (j.contains("triangle")) {
        if (!triangle)
          fail(p, "triangle is an opacity shape only");
        object(j, p, {"triangle"});
        auto& t = j["triangle"];
        object(t, p, {"from", "peak", "to"});
        std::string a = num(number(t["from"], p)), b = num(number(t["peak"], p)),
                    c = num(number(t["to"], p));
        std::string x = local + ".progress";
        return "(" + x + " < 0.5 ? (" + a + " + (" + b + " - " + a + ") * 2 * " + x + ") : (" + b +
            " + (" + c + " - " + b + ") * (2 * " + x + " - 1)))";
      }
      object(j, p, {"curve"});
      auto& c = j["curve"];
      object(c, p, {"from", "to"}, {"ease", "scope"});
      std::string scope = c.contains("scope") ? str(c["scope"], p) : "local";
      std::string clock = scope == "local" ? local : scope == "scene" ? scene : scope;
      if (scope != "local" && scope != "scene" &&
          std::find(ancestors.begin(), ancestors.end(), scope) == ancestors.end())
        fail(p, "curve scope must be local, scene, or an ancestor id");
      std::string x = clock + ".progress", ease = c.contains("ease") ? str(c["ease"], p) : "linear";
      if (ease == "early")
        x = "(1-(1-" + x + ")^3)";
      else if (ease == "late")
        x = "(" + x + "^3)";
      else if (ease != "linear")
        fail(p, "unknown curve easing");
      return "(" + num(number(c["from"], p)) + " + (" + num(number(c["to"], p)) + " - " +
          num(number(c["from"], p)) + ") * " + x + ")";
    }
    E chance(const J& j, const std::string& p, const std::string& target)
    {
      auto q = probability(j, p);
      E e;
      e.kind = E::Kind::Chance;
      e.target = target;
      e.probability_numerator = q.first;
      e.probability_denominator = q.second;
      return e;
    }
    void output(const J& j, const std::string& p, N& n, const std::vector<std::string>& ancestors,
                uint32_t local_duration)
    {
      object(j, p, {"op"},
             {"source", "flip_probability", "selection_probability", "zoom", "zoom_baseline",
              "opacity", "visibility", "envelope", "animation", "speed"});
      std::string op = str(j["op"], p + ".op"), key = "v4output_" + std::to_string(++serial),
                  local = n.id + "_local";
      R r;
      r.when = n.id + ".active";
      if (op == "spiral") {
        for (auto it = j.begin(); it != j.end(); ++it)
          if (it.key() != "op" && it.key() != "speed")
            fail(p, "spiral only supports speed");
        r.op = R::Op::Spiral;
        if (j.contains("speed"))
          r.speed = scalar(j["speed"], p, local, ancestors);
        render.push_back(r);
        return;
      }
      if (op != "image" && op != "word" && op != "line" && op != "caption" && op != "subtext")
        fail(p, "unknown output operation");
      if (!j.contains("source"))
        fail(p, "media source required");
      std::string source = str(j["source"], p);
      E e;
      e.kind = op == "image" ? E::Kind::Image
          : op == "caption"  ? E::Kind::SmallSub
          : op == "subtext"  ? E::Kind::Subtext
                             : E::Kind::Text;
      e.force = op == "caption";
      e.split = op == "line" ? 1u : 0u;
      e.target = key;
      if (source == "primary")
        e.slot = pattern::Slot::Primary;
      else if (source == "secondary")
        e.slot = pattern::Slot::Secondary;
      else if (source == "either") {
        E roll;
        roll.kind = E::Kind::Roll;
        roll.target = key + "_side";
        roll.choices = {0, 1};
        n.effects.push_back(roll);
        e.slot_reg = roll.target;
      } else if (source == "alternate" && op == "image") {
        E flip;
        flip.kind = E::Kind::Toggle;
        flip.target = key + "_side";
        if (j.contains("flip_probability")) {
          E c = chance(j["flip_probability"], p, key + "_flip");
          n.effects.push_back(c);
          flip.guard = E::Guard::Truthy;
          flip.guard_reg = c.target;
        }
        n.effects.push_back(flip);
        e.slot_reg = flip.target;
      } else
        fail(p, "unknown media source (alternate is image-only)");
      if (j.contains("flip_probability") && source != "alternate")
        fail(p, "flip_probability requires alternate source");
      if (j.contains("selection_probability")) {
        E c = chance(j["selection_probability"], p, key + "_pick");
        n.effects.push_back(c);
        e.guard = E::Guard::Truthy;
        e.guard_reg = c.target;
        if (op != "image")
          r.when += " and " + c.target;
      }
      n.effects.push_back(e);
      if (op == "image") {
        r.op = R::Op::Image;
        r.image_reg = key;
        n.image_slot = e.slot;
        n.image_label = "image";
      } else if (op == "word" || op == "line") {
        r.op = R::Op::Text;
        r.origin = "0.75";
        r.zoom = "0.75";
      } else if (op == "caption") {
        r.op = R::Op::SmallText;
        r.alpha = "0.2";
        r.origin = "0.5";
      } else {
        r.op = R::Op::Subtext;
        r.alpha = "0.25";
        r.origin = "0.375";
      }
      for (auto field : {"zoom", "zoom_baseline", "opacity", "speed"})
        if (j.contains(field)) {
          std::string f = field;
          if ((f == "speed") || (f != "opacity" && op != "image") ||
              (f == "opacity" && op != "image" && op != "caption" && op != "subtext"))
            fail(p, f + " unsupported on " + op);
          std::string v = scalar(j[field], p + "." + f, local, ancestors, f == "opacity");
          if (f == "zoom")
            r.zoom = v;
          else if (f == "zoom_baseline")
            r.origin = v;
          else
            r.alpha = v;
        }
      if (j.contains("animation")) {
        if (op != "image")
          fail(p, "animation requires image");
        auto& a = j["animation"];
        object(a, p, {"every"});
        uint32_t every = integer(a["every"], p, 1);
        E load = e;
        load.kind = E::Kind::Anim;
        load.guard = E::Guard::None;
        load.guard_reg.clear();
        n.effects.push_back(load);
        r.has_anim = true;
        r.anim_image_reg = key;
        if (every > 1) {
          E pulse;
          pulse.kind = E::Kind::Pulse;
          pulse.target = key + "_count";
          pulse.mod_literal = static_cast<int32_t>(every);
          pulse.flag = key + "_anim";
          n.effects.push_back(pulse);
          r.anim_gate = pulse.flag;
        }
      }
      if (j.contains("visibility")) {
        auto& v = j["visibility"];
        if (!v.is_array() || v.size() != 2)
          fail(p, "visibility needs [start,end]");
        double a = number(v[0], p, 0, 1), b = number(v[1], p, 0, 1);
        if (a >= b)
          fail(p, "visibility interval must increase");
        r.when +=
            " and " + local + ".progress >= " + num(a) + " and " + local + ".progress < " + num(b);
      }
      if (j.contains("envelope")) {
        if (op != "image" && op != "caption" && op != "subtext")
          fail(p, "envelope unsupported on output");
        if (j.contains("opacity"))
          fail(p, "choose opacity or envelope");
        auto& v = j["envelope"];
        object(v, p, {"in", "hold", "out"});
        uint32_t a = integer(v["in"], p, 1), b = integer(v["hold"], p), c = integer(v["out"], p, 1);
        if (!local_duration || uint64_t(a) + b + c > local_duration)
          fail(p, "envelope needs known fitting local duration; choose Fixed frames (explicit duration) or fixed cadence for variable inheritance");
        std::string f = local + ".frame";
        r.alpha = "max(0,min(min(" + f + "/" + num(a) + ",(" + num(double(a) + b + c) + "-" + f +
            ")/" + num(c) + "),1))";
      }
      render.push_back(r);
    }
    N node(const J& j, const std::string& p, std::vector<std::string> ancestors, uint32_t inherited,
           uint32_t depth = 0, bool variable_window = false)
    {
      if (depth > 64)
        fail(p, "maximum nesting depth is 64");
      object(j, p, {"kind", "children"},
             {"id", "duration", "repeat", "cadence", "offset", "select", "loop_subpatterns"});
      N n;
      n.id = name(j);
      if (scene.empty())
        scene = n.id;
      ancestors.push_back(n.id);
      std::string kind = str(j["kind"], p);
      n.phase = j.contains("id") ? n.id : kind;
      if (kind == "parallel")
        n.occurrence_kind = N::Composition::Parallel;
      else if (kind == "sequential")
        n.occurrence_kind = N::Composition::Sequential;
      else if (kind == "random")
        n.occurrence_kind = N::Composition::Random;
      else
        fail(p, "only parallel, sequential, random are composition kinds");
      if (j.contains("loop_subpatterns")) {
        if (kind == "random") fail(p + ".loop_subpatterns", "belongs to parallel or sequential only");
        if (!j["loop_subpatterns"].is_boolean()) fail(p + ".loop_subpatterns", "expected boolean");
        n.loop_subpatterns = j["loop_subpatterns"].get<bool>();
      }
      if (j.contains("duration")) n.length = integer(j["duration"], p + ".duration", 1);
      if (!n.length && !inherited) {
        const auto span = inherited_root_span(j);
        if (!span) fail(p + ".duration", "Inherit needs a parent duration or finite timed subpatterns");
        n.length = *span;
      }
      uint32_t span = n.length ? n.length : inherited;
      // Explicit duration makes this occurrence's clock independent of the
      // available ancestor window, even when that ancestor clips its output.
      variable_window = variable_window && !n.length;
      if (j.contains("repeat")) {
        if (j["repeat"].is_string() && j["repeat"] == "forever")
          n.repeat_forever = true;
        else
          n.repeat_count = integer(j["repeat"], p, 1);

      }
      if (j.contains("offset"))
        n.offset = integer(j["offset"], p);
      if (j.contains("cadence")) {
        auto& c = j["cadence"];
        if (c.is_object()) {
          object(c, p, {"ramp"});
          auto& r = c["ramp"];
          object(r, p, {"from", "to", "steps", "ease"});
          n.cadence_ramp_from = integer(r["from"], p, 1);
          n.cadence_ramp_to = integer(r["to"], p, 1);
          n.cadence_ramp_steps = integer(r["steps"], p, 1, 65536);
          auto ease = str(r["ease"], p);
          n.cadence_ramp_ease = ease == "linear" ? 0
              : ease == "early"                  ? 1
              : ease == "late"                   ? 2
                                                 : -1;
          if (n.cadence_ramp_ease < 0)
            fail(p, "unknown ramp easing");
          if (!j.contains("duration") || n.cadence_ramp_steps > n.length)
            fail(p, "ramp needs integer duration with one frame per step");
        } else
          n.cadence = integer(c, p, 1);
      }
      if (n.offset) {
        if (variable_window && !n.cadence)
          fail(p + ".offset", "phase with a variable inherited window needs Fixed frames (explicit duration) or fixed cadence");
        const uint32_t period = n.cadence ? n.cadence : span;
        if ((n.cadence_ramp_steps && span > 1000000u) ||
            (!n.cadence_ramp_steps && period && (period - n.offset % period) % period > 1000000u))
          fail(p, "phase offset requires more than one million pre-roll frames");
      }
      auto& children = j["children"];
      if (!children.is_array() || children.empty() || children.size() > 4096)
        fail(p, "nonempty children array with at most 4096 entries required");
      if (kind == "random") {
        if (!j.contains("select"))
          fail(p, "random requires select policy");
        auto& s = j["select"];
        object(s, p, {"order", "allow_self", "first"}, {"cooldown"});
        auto order = str(s["order"], p);
        if (order != "random" && order != "round_robin")
          fail(p, "invalid selection order");
        n.select_random = order == "random";
        if (!s["allow_self"].is_boolean())
          fail(p, "allow_self needs boolean");
        n.select_allow_self = s["allow_self"].get<bool>();
        if (!n.select_random && n.select_allow_self)
          fail(p, "round_robin cannot allow self");
        n.select_first = integer(s["first"], p, 0, static_cast<uint32_t>(children.size() - 1));
        if (s.contains("cooldown"))
          n.select_cooldown = integer(s["cooldown"], p);
      } else if (j.contains("select"))
        fail(p, "selection policy belongs to random composition");
      uint32_t local = n.cadence ? n.cadence : n.cadence_ramp_steps ? 0 : span;
      const bool changing_body = variable_window || n.loop_subpatterns || kind == "random" ||
          n.offset || n.cadence_ramp_steps || (n.cadence && span % n.cadence);
      std::optional<uint64_t> sequence_cursor = 0;
      for (size_t i = 0; i < children.size(); ++i) {
        auto& edge = children[i];
        std::string q = p + ".children[" + std::to_string(i) + "]";
        object(edge, q, {"node"}, {"exit"});
        auto& child = edge["node"];
        if (!child.is_object())
          fail(q, "node must be object");
        N::Exit exit;
        if (edge.contains("exit")) {
          if (kind != "random")
            fail(q, "chance exit belongs to random child edges");
          auto& e = edge["exit"];
          object(e, q, {"probability", "check_every"});
          auto prob = probability(e["probability"], q);
          exit.enabled = true;
          exit.numerator = prob.first;
          exit.denominator = prob.second;
          exit.check_every = integer(e["check_every"], q, 1);
        }
        if (child.contains("op")) {
          if (kind != "parallel" || edge.contains("exit"))
            fail(q, "outputs belong directly to parallel only");
          // Envelope size is a compile-time fitting requirement. Dynamic
          // inherited clocks still drive curves, but cannot promise that fit.
          output(child, q + ".node", n, ancestors,
                 variable_window && !n.cadence ? 0 : local);
        } else {
          uint32_t available = local ? local : span;
          if (kind == "sequential" && sequence_cursor && *sequence_cursor < available)
            available -= static_cast<uint32_t>(*sequence_cursor);
          const bool changing_child = changing_body || (kind == "sequential" && !sequence_cursor);
          N k = node(child, q + ".node", ancestors, available, depth + 1, changing_child);
          if (kind == "sequential" && sequence_cursor) {
            const auto length = lifetime(child, available);
            if (length) *sequence_cursor += *length;
            else sequence_cursor.reset();
          }
          if ((kind == "sequential" || (kind == "random" && !exit.enabled)) &&
              k.repeat_forever)
            fail(q, "child needs finite duration and repeat, or random chance exit");
          n.children.push_back(std::move(k));
          n.child_exits.push_back(exit);
        }
      }
      if (!n.repeat_forever && n.repeat_count > 1 && !lifetime(j, inherited) &&
          uint64_t(span) * n.repeat_count > limit)
        fail(p, "repeated span overflows runtime frame range");
      // Advisory only: intentional clipping is valid. Emit exact, local spans
      // only when cadence/phase/repetition cannot change these boundaries.
      if (!n.loop_subpatterns && kind != "random" && !j.contains("cadence") &&
          !j.contains("offset") && span && (j.contains("duration") || !depth)) {
        uint64_t cursor = 0;
        bool exact = true;
        for (size_t i = 0; i < children.size(); ++i) {
          const auto& child = children[i]["node"];
          if (child.contains("op")) continue;
          auto extent = lifetime(child, kind == "sequential" && cursor < span
              ? span - static_cast<uint32_t>(cursor) : span);
          if (!extent) { exact = false; break; }
          const uint64_t begin = kind == "sequential" ? cursor : 0;
          const uint64_t end = begin + *extent;
          const auto path = p + ".children[" + std::to_string(i) + "].node";
          if (end > span)
            warnings.push_back(path + ": clipped child span [" + std::to_string(span) +
                "," + std::to_string(end) + ") by owner duration");
          else if (kind == "parallel" && end < span)
            warnings.push_back(path + ": inactive child span [" + std::to_string(end) +
                "," + std::to_string(span) + ") within owner");
          cursor = end;
        }
        if (exact && kind == "sequential" && cursor && cursor < span)
          warnings.push_back(p + ": sequence completes at " + std::to_string(cursor) +
              "; unused owner span [" + std::to_string(cursor) + "," + std::to_string(span) + ")");
      }
      return n;
    }
  };
} // namespace
namespace patternv4
{
  ParseResult parse(const std::string& source, uint32_t locked_period_frames)
  {
    (void) locked_period_frames;
    ParseResult result;
    try {
      if (source.size() > 4u * 1024u * 1024u)
        fail("$", "source exceeds four MiB");
      std::vector<std::set<std::string>> members;
      auto callback = [&](int depth, J::parse_event_t event, J& value) {
        if (depth > 256)
          fail("$", "JSON nesting exceeds safety limit");
        if (event == J::parse_event_t::object_start)
          members.emplace_back();
        else if (event == J::parse_event_t::object_end)
          members.pop_back();
        else if (event == J::parse_event_t::key &&
                 !members.back().insert(value.get<std::string>()).second)
          fail("$", "duplicate object member");
        return true;
      };
      auto j = J::parse(source, callback);
      object(j, "$", {"version", "root"});
      if (integer(j["version"], "$.version") != 4)
        fail("$.version", "expected version 4");
      Parser p;
      p.reserve(j["root"], "$.root");
      result.root = p.node(j["root"], "$.root", {}, 0);
      if (!result.root.length || result.root.repeat_forever)
        fail("$.root", "root needs a finite duration and repeat");
      for (const auto& id : p.ids)
        if (p.ids.count(id + "_local") || p.generated.count(id + "_local"))
          fail("$.root", "id collides with internal local clock alias");
      result.render_block = std::move(p.render);
      result.warnings = std::move(p.warnings);
      result.name = result.root.id;
      result.ok = true;
    } catch (const std::exception& e) {
      result.error = e.what();
    }
    return result;
  }
} // namespace patternv4
