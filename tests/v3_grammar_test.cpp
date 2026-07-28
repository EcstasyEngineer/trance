// Sanity + behavioral test for the v3 intent grammar (docs/spec-grammar-v3.md).
// Asserts the load-bearing invariants of Phase 1: patterns nest and lower to a runnable tree;
// crossfade EMERGES from primitives (copy + cur/prev + complementary alpha, no keyword); and
// registers are lexically pattern-scoped so two sibling crossfades never collide.
//
// Also evaluates (not just string-matches) every lowered render [expr]: parse-time name
// resolution can catch an `over unknown-clock` typo, but it can't catch a malformed expr the
// LOWERING code itself emits (e.g. a bad literal, an unbalanced paren from a template). Running
// the actual expression evaluator (render_eval.h) against live cycler state at several frames
// closes that gap. Only render_eval.h's pure evaluator is used here (eval_expr/eval_cond_expr,
// inline, no VisualRender dependency) -- render_eval.cpp's eval_render() needs the full
// VisualRender type from api.h, which drags SFML and can't build in this headless target; see
// the CMakeLists comment above the v3_grammar_test target for the exact seam.
//
// Headless: parser + compiler + cyclers, no SFML/protobuf. Run via ctest.
#include <trance/visual/builtin_patterns.h>
#include <trance/visual/cyclers.h>
#include <trance/visual/pattern_ast.h>
#include <trance/visual/pattern_compiler.h>
#include <trance/visual/pattern_parser_v3.h>
#include <trance/visual/render_eval.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
  int g_fail = 0;
  void check(bool ok, const std::string& what)
  {
    std::cout << (ok ? "  ok  " : "FAIL  ") << what << "\n";
    if (!ok) ++g_fail;
  }

  patternv3::ParseResult parse(const std::string& src, uint32_t locked = 0)
  {
    return patternv3::parse(src, locked);
  }

  std::vector<const pattern::RenderStmt*> images(const patternv3::ParseResult& pr)
  {
    std::vector<const pattern::RenderStmt*> v;
    for (const auto& s : pr.render_block)
      if (s.op == pattern::RenderStmt::Op::Image) v.push_back(&s);
    return v;
  }
  // The qualified-register prefix, e.g. "_n1" from "_n1$cur".
  std::string prefix(const std::string& reg)
  {
    const auto d = reg.find('$');
    return d == std::string::npos ? reg : reg.substr(0, d);
  }
  bool suffix(const std::string& s, const std::string& tail)
  {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
  }
  std::string progress_clock(const std::string& expr)
  {
    const auto p = expr.find(".progress");
    if (p == std::string::npos) return "";
    std::size_t start = p;
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(expr[start - 1])) || expr[start - 1] == '_')) {
      --start;
    }
    return expr.substr(start, p - start);
  }

  // Find the ramp cadence's lowered Seq node: a Node::Type::Seq whose children are all
  // Action leaves with a non-empty id (the shape parse_ramp_cadence produces; distinct from
  // the top-level `One{init, body}` wrapper or any Par/Rep the rest of the grammar builds).
  const pattern::Node* find_ramp_seq(const pattern::Node& n)
  {
    if (n.type == pattern::Node::Type::Seq && n.children.size() >= 2) {
      bool all_action_ids = true;
      for (const auto& c : n.children) {
        if (c.type != pattern::Node::Type::Action || c.id.empty()) { all_action_ids = false; break; }
      }
      if (all_action_ids) return &n;
    }
    for (const auto& c : n.children) {
      if (const auto* r = find_ramp_seq(c)) return r;
    }
    return nullptr;
  }

  // Collect every Effect of a given Kind anywhere in the lowered tree (both leaf `effects`
  // and, for Burst nodes, `burst_effects`) -- used by the `audio` tests below to find the
  // Effect::Kind::Audio/AudioStop leaves the parser emits without assuming which exact
  // Node::Type wraps them (mirrors how the burst test walks a stack rather than assuming a
  // fixed nesting depth).
  std::vector<const pattern::Effect*> find_effects(const pattern::Node& root,
                                                    pattern::Effect::Kind kind)
  {
    std::vector<const pattern::Effect*> out;
    std::vector<const pattern::Node*> stack{&root};
    while (!stack.empty()) {
      const pattern::Node* n = stack.back();
      stack.pop_back();
      for (const auto& e : n->effects) {
        if (e.kind == kind) out.push_back(&e);
      }
      for (const auto& e : n->burst_effects) {
        if (e.kind == kind) out.push_back(&e);
      }
      for (const auto& c : n->children) stack.push_back(&c);
    }
    return out;
  }

  // Evaluate every [expr] field of one RenderStmt against live cycler state, asserting each
  // numeric result is finite and within a sane tripwire bound (a malformed lowered expr -- e.g.
  // `resolve_ident` hitting a dangling/renamed node id -- silently resolves to 0.0 rather than
  // erroring, so this can't rely on non-zero; it catches NaN/Inf and wildly-out-of-range results
  // like an unterminated paren dragging in the rest of the expr as a bogus product). `where`
  // labels a failing check with the pattern/frame under test.
  void check_stmt_finite(const pattern::RenderStmt& st, const pattern::Registers& regs,
                         const pattern::NodeMap& nodes, const Cycler* root,
                         const std::string& where)
  {
    auto num = [&](const std::string& expr, double dflt, const char* field) {
      double v = pattern::eval_expr(expr, dflt, regs, nodes, root);
      check(std::isfinite(v), where + " " + field + ": evaluates to a finite number");
      check(std::fabs(v) <= 16.0, where + " " + field + ": within sane bounds (|v| <= 16)");
    };
    num(st.alpha, 1.0, "alpha");
    num(st.origin, 0.0, "origin");
    num(st.zoom, 0.0, "zoom");
    num(st.shadow_origin, 0.0, "shadow_origin");
    num(st.shadow_zoom, 0.0, "shadow_zoom");
    num(st.speed, 0.0, "speed");
    // Boolean guards: just confirm they evaluate without any assumption on the result --
    // `when`/anim_gate/anim_alt are conditions, not bounded numerics.
    (void)pattern::eval_cond_expr(st.when, regs, nodes, root);
    (void)pattern::eval_cond_expr(st.anim_gate, regs, nodes, root);
    (void)pattern::eval_cond_expr(st.anim_alt, regs, nodes, root);
  }

  // Compile `root`, drive its cycler tree exactly like Director::update() does (one advance()
  // per frame; see director.cpp), and evaluate every render_block RenderStmt's [expr] fields at
  // frames {0, mid, end-1} -- catching a malformed expr the LOWERING code itself emits, which
  // parse-time name resolution can't see. A fresh compile per call, so each of the three sampled
  // frames starts from the same clean tree rather than compounding advances across samples.
  void eval_render_block_at_frames(const pattern::Node& root,
                                   const std::vector<pattern::RenderStmt>& render_block,
                                   const std::string& label)
  {
    uint32_t length = 0;
    {
      Cycler* probe = pattern::compile(root);
      length = probe ? probe->length() : 0;
      delete probe;
    }
    if (length == 0 || render_block.empty()) {
      return;
    }
    std::vector<uint32_t> frames = {0, length / 2, length - 1};
    for (uint32_t target : frames) {
      pattern::NodeMap node_map;
      Cycler* compiled = pattern::compile(root, pattern::MakeAction{}, node_map);
      check(compiled != nullptr, label + ": compiles for frame-driven eval");
      if (!compiled) {
        continue;
      }
      pattern::Registers regs;
      // advance() N+1 times => frame() == N (frame() is position()-1 mod length()), matching
      // Director::update()'s one-advance-per-frame loop.
      for (uint32_t i = 0; i <= target; ++i) {
        compiled->advance();
      }
      std::string where = label + " frame " + std::to_string(target);
      for (const auto& st : render_block) {
        check_stmt_finite(st, regs, node_map, compiled, where);
      }
      delete compiled;
    }
  }
}

int main()
{
  // 1. Simplest draw lowers to a runnable tree with a curve-driven zoom.
  {
    auto pr = parse("pattern hello for 240f { image concept zoom (curve 0 -> 0.5) }");
    check(pr.ok, std::string("simplest: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 240, "simplest: tree length 240");
      auto im = images(pr);
      check(im.size() == 1 && !im[0]->zoom.empty(), "simplest: one image draw with a zoom expr");
      delete root;
    }
  }

  // 2. Crossfade emerges from primitives: a copied previous image drawn under a new image whose
  //    top layer fades in. Both zoom halves ride the same beat clock so the copied image keeps
  //    zooming continuously after the handoff -- no crossfade keyword or hidden +0.5 branch.
  {
    auto pr = parse(R"(
pattern flash_text for 1024f {
  pattern life for 128f loop 8 {
    every 64f -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image concept -> cur fade in zoom (curve 0 -> 0.5)
    }
  }
})");
    check(pr.ok, std::string("crossfade: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto im = images(pr);
      check(im.size() == 2, "crossfade: exactly two image layers (cur + prev)");
      if (im.size() == 2) {
        const auto* prev = suffix(im[0]->image_reg, "$prev") ? im[0] : im[1];
        const auto* cur = suffix(im[0]->image_reg, "$cur") ? im[0] : im[1];
        check(suffix(im[0]->image_reg, "$prev") && suffix(im[1]->image_reg, "$cur"),
              "crossfade: previous layer draws before the new fade-in layer");
        check(!cur->alpha.empty() && progress_clock(cur->alpha) == progress_clock(cur->zoom) &&
                  progress_clock(cur->zoom) == progress_clock(prev->zoom),
              "crossfade: fade and both zoom halves ride the same beat clock");
        check(prefix(cur->image_reg) == prefix(prev->image_reg),
              "crossfade: cur and prev share one register scope (the life pattern)");
      }
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 1024, "crossfade: tree length 1024");
      delete root;
    }
  }

  // 3. Register scoping: two sibling crossfades each minting cur/prev must NOT collide.
  {
    auto pr = parse(R"(
pattern twin for 512f {
  pattern a for 128f loop 4 { every 64f -> b { copy cur -> prev  image reward  -> cur fade in } draw prev fade out }
  pattern c for 128f loop 4 { every 64f -> d { copy cur -> prev  image concept -> cur fade in } draw prev fade out }
})");
    check(pr.ok, std::string("siblings: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto im = images(pr);
      check(im.size() == 4, "siblings: four image layers");
      if (im.size() == 4) {
        // a's two layers share a prefix; c's two share a DIFFERENT prefix.
        const std::string pa = prefix(im[0]->image_reg), pc = prefix(im[2]->image_reg);
        check(prefix(im[1]->image_reg) == pa && prefix(im[3]->image_reg) == pc && pa != pc,
              "siblings: the two crossfades occupy DISTINCT register scopes (no collision)");
      }
    }
  }

  // 3b. Spiral speed is a curve-drivable render param; look{} lowers to a SpiralSet effect.
  {
    auto pr = parse(R"(
pattern s for 240f {
  look { spiral type=3 width=6 }
  image concept zoom 0.5
  spiral speed (curve 1 -> 3)
})");
    check(pr.ok, std::string("spiral: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      bool spiral_speed = false;
      for (const auto& st : pr.render_block)
        if (st.op == pattern::RenderStmt::Op::Spiral && !st.speed.empty()) spiral_speed = true;
      check(spiral_speed, "spiral: speed lowers to a render-side [expr]");
    }
  }

  // 3c. The wave warp (and `drunk` sugar) lowers to a Warp render op with params.
  {
    auto pr = parse(R"(
pattern w for 240f {
  warp amplitude (curve 0 -> 0.3) wavelength 0.2 speed 2
  image concept zoom 0.5
})");
    check(pr.ok, std::string("warp: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    bool has_warp = false;
    if (pr.ok)
      for (const auto& st : pr.render_block)
        if (st.op == pattern::RenderStmt::Op::Warp && !st.zoom.empty()) has_warp = true;
    check(has_warp, "warp: lowers to a Warp op carrying an amplitude expr");

    auto pd = parse("pattern d for 240f { drunk (curve 0 -> 0.3) image concept zoom 0.5 }");
    bool drunk_warp = pd.ok;
    if (pd.ok) {
      drunk_warp = false;
      for (const auto& st : pd.render_block)
        if (st.op == pattern::RenderStmt::Op::Warp) drunk_warp = true;
    }
    check(drunk_warp, "drunk: is sugar for a warp op");
  }

  // 4. `over` resolution check fails loud on an unknown clock (no silent-zero).
  {
    auto pr = parse("pattern x for 100f { image concept zoom (curve 0 -> 1 over nope) }");
    check(!pr.ok && pr.error.find("nope") != std::string::npos,
          "resolution: `over nope` is a hard parse error");
  }

  // 4b. Sampled ramp cadence (13.2 / Extension #3): parses, lowers to a Seq of N
  // fixed-length Action segments whose lengths are non-increasing (respecting `ease late` on a
  // shrinking A->B) and sum EXACTLY to the enclosing pattern's span -- no silent truncation.
  {
    auto pr = parse(R"(
pattern ramped for 300f {
  every ramp 40f -> 10f steps 10 ease late -> cut {
    image concept zoom 0.5
  }
})");
    check(pr.ok, std::string("ramp: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      const auto* seq = find_ramp_seq(pr.root);
      check(seq != nullptr, "ramp: lowers to a Seq of fixed-length segments");
      if (seq) {
        check(seq->children.size() == 10, "ramp: segment count matches `steps 10`");
        uint32_t sum = 0;
        bool monotonic = true;
        bool all_ids_distinct = true;
        std::set<std::string> ids;
        for (std::size_t i = 0; i < seq->children.size(); ++i) {
          const auto& c = seq->children[i];
          sum += c.length;
          if (c.length < 1) monotonic = false;  // (also used below as a >=1f floor check)
          if (i > 0 && c.length > seq->children[i - 1].length) monotonic = false;
          if (!ids.insert(c.id).second) all_ids_distinct = false;
        }
        check(sum == 300, "ramp: sampled segment lengths sum exactly to the 300f span");
        check(monotonic, "ramp: segment lengths are non-increasing (ease late, A > B)");
        check(all_ids_distinct, "ramp: every segment gets its own stable node id");
      }
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 300, "ramp: compiled tree length matches the 300f span");
      delete root;
    }
  }

  // 4c. `over PARENT` from inside a ramp body still resolves through the existing scope stack
  // (the enclosing pattern, named, stays addressable even though segments are anonymous ids).
  {
    auto pr = parse(R"(
pattern ramped2 for 200f {
  every ramp 20f -> 10f steps 5 -> cut {
    image concept zoom (curve 0 -> 1 over ramped2)
  }
})");
    check(pr.ok, std::string("ramp over-parent: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      // `parse()` wraps the pattern body as root.children[1] (root.children[0] is the init
      // Action); that child's minted id is what `over ramped2` should resolve to -- compare
      // against the compiled id itself (like the existing crossfade test does), not the
      // surface-syntax name, since names never survive into the render [expr] strings.
      const std::string pat_id =
          pr.root.children.size() > 1 ? pr.root.children[1].id : std::string();
      auto im = images(pr);
      bool found = !pat_id.empty();
      for (const auto* i : im) {
        if (progress_clock(i->zoom) != pat_id) found = false;
      }
      check(found && !im.empty(),
            "ramp over-parent: body modulator anchors to the named enclosing pattern");
    }
  }

  // 4d. `this` (bare, no `over`) inside a ramp body anchors to the segment's OWN clock, and each
  // segment's draws are gated by that segment's own `.active` (not the parent's), which is what
  // makes an un-anchored modulator behave as "the active segment's clock" per 13.2.
  {
    auto pr = parse(R"(
pattern ramped3 for 200f {
  every ramp 20f -> 10f steps 5 -> cut {
    image concept zoom (curve 0 -> 1)
  }
})");
    check(pr.ok, std::string("ramp this-anchor: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      const auto* seq = find_ramp_seq(pr.root);
      auto im = images(pr);
      check(seq && im.size() == seq->children.size(),
            "ramp this-anchor: one image RenderStmt is emitted PER segment");
      if (seq) {
        bool each_own_clock = im.size() == seq->children.size();
        bool each_own_gate = true;
        for (std::size_t i = 0; i < im.size() && i < seq->children.size(); ++i) {
          if (progress_clock(im[i]->zoom) != seq->children[i].id) each_own_clock = false;
          if (im[i]->when.find(seq->children[i].id + ".active") == std::string::npos)
            each_own_gate = false;
        }
        check(each_own_clock, "ramp this-anchor: each segment's bare modulator rides ITS OWN id");
        check(each_own_gate, "ramp this-anchor: each segment's draw is gated by its OWN .active");
      }
    }
  }

  // 4e. Error cases: steps < 2, unknown ease word, and a span too small to fit `steps` segments
  // of >=1f each are all hard parse errors -- no silent truncation/clamping.
  {
    auto steps1 = parse("pattern r for 100f { every ramp 10f -> 5f steps 1 { image concept } }");
    check(!steps1.ok, "ramp error: `steps 1` (< 2) is a hard parse error");

    auto badease = parse(
        "pattern r for 100f { every ramp 10f -> 5f steps 4 ease bogus { image concept } }");
    check(!badease.ok && badease.error.find("bogus") != std::string::npos,
          "ramp error: unknown ease word is a hard parse error naming the bad word");

    auto toosmall = parse("pattern r for 3f { every ramp 10f -> 5f steps 10 { image concept } }");
    check(!toosmall.ok, "ramp error: span too small for `steps` segments of >=1f is a parse error");
  }

  // 4f. Burst surface (13.1): parses, lowers to exactly one Node::Burst carrying the
  // parsed params, base/burst effects land in the right lists, and `NAME.index` (1 during a
  // burst, else 0) is reachable from a render expr through the same NodeMap/resolve_ident path
  // every other named clock uses -- no bespoke plumbing for burst.
  {
    // `over rapid` and the raw `[this.index]` are used INSIDE the burst's own base/burst
    // blocks, matching how every other named clock (`every ... -> beat`, `every ramp ... ->
    // cut`) is documented and tested: the name is only in scope within its own body, not from
    // sibling statements after the block closes. A raw `[expr]` only substitutes bare
    // `this`/`self` (subst_this), same as everywhere else in the grammar -- `over NAME` is the
    // one path that resolves a name other than `this`, so that's what exercises the id lookup;
    // `[this.index]` exercises `burst.index` being reachable via the bracket-expr path too.
    auto pr = parse(R"(
pattern burster for 512f {
  burst -> rapid period 8f chance 1/24 cooldown 32f duration 32f..96f {
    base  { image runtime zoom 0.5 }
    burst { image runtime zoom (curve 1 -> 2 over rapid) alpha [this.index] }
  }
})");
    check(pr.ok, std::string("burst: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      // Find the single Burst node in the lowered tree.
      const pattern::Node* burst_node = nullptr;
      std::vector<const pattern::Node*> stack{&pr.root};
      while (!stack.empty()) {
        const pattern::Node* n = stack.back();
        stack.pop_back();
        if (n->type == pattern::Node::Type::Burst) burst_node = n;
        for (const auto& c : n->children) stack.push_back(&c);
      }
      check(burst_node != nullptr, "burst: lowers to a Node::Burst");
      if (burst_node) {
        check(burst_node->length == 512, "burst: length takes the enclosing pattern's span");
        check(burst_node->burst_period == 8, "burst: period param");
        check(burst_node->burst_chance_den == 24, "burst: chance 1/24 -> chance_den 24");
        // Authored in frames, stored in period TICKS (BurstCycler's FSM only steps on
        // period boundaries): 32f/8f = 4 ticks, 96f/8f = 12 ticks.
        check(burst_node->burst_cooldown == 4, "burst: cooldown 32f -> 4 ticks at period 8f");
        check(burst_node->burst_dur_min == 4 && burst_node->burst_dur_max == 12,
              "burst: duration 32f..96f -> 4..12 ticks at period 8f");
        check(burst_node->effects.size() == 1 && burst_node->burst_effects.size() == 1,
              "burst: base block -> effects, burst block -> burst_effects (one each)");
        check(!burst_node->id.empty(), "burst: `-> rapid` mints a stable node id");
      }
      // `over rapid` resolves the named burst clock; render_eval's resolve_ident reads
      // burst.index the same generic way it reads any other node's .index.
      bool zoom_over_burst = false, index_expr_reachable = false;
      for (const auto& st : pr.render_block) {
        if (st.op != pattern::RenderStmt::Op::Image || !burst_node) continue;
        if (st.zoom.find(burst_node->id + ".progress") != std::string::npos) zoom_over_burst = true;
        if (st.alpha.find(burst_node->id + ".index") != std::string::npos)
          index_expr_reachable = true;
      }
      check(zoom_over_burst, "burst: `over rapid` resolves to the minted burst node id");
      check(index_expr_reachable,
            "burst: `[this.index]` resolves to the minted node id (readable by resolve_ident)");
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 512, "burst: compiled tree length matches the 512f span");
      delete root;

      // Compile through the NodeMap-producing overload and confirm the minted id resolves to
      // an actual live BurstCycler -- the exact seam render_eval's resolve_ident walks at
      // runtime for "<id>.index". This is what would have caught the collapse-clobber bug
      // (pattern_pattern's single-child collapse overwriting the burst node's own id) that a
      // string-level id comparison alone cannot: it proves the id in the render expr and the
      // id actually registered in NodeMap are the SAME live node, not just equal-looking strings.
      pattern::NodeMap node_map;
      Cycler* root2 = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
      bool found_in_map = burst_node && node_map.count(burst_node->id) &&
          std::string(node_map.at(burst_node->id)->type_name()) == "Burst";
      check(found_in_map, "burst: the minted id resolves in NodeMap to a live BurstCycler");
      delete root2;
    }
  }

  // 4g. Burst error cases: missing `period` and a malformed `chance` denominator are hard
  // parse errors (no silent zero/never-fires default).
  {
    auto noperiod = parse(
        "pattern b for 100f { burst chance 1/8 { base { image concept } } }");
    check(!noperiod.ok, "burst error: missing `period` is a hard parse error");

    auto badchance = parse(
        "pattern b for 100f { burst period 4f chance 2/8 { base { image concept } } }");
    check(!badchance.ok, "burst error: `chance` numerator must be literal `1`");
  }

  // 4h. `beats N` phase-locks a length to the entrainment bed's pulse period: with locked=32
  // (the value director.cpp derives from the program's pulse_hz -- see locked_period_frames()),
  // `beats 8` must lower to exactly 8*32 = 256 frames, both for a pattern's own `for beats N`
  // span and for an `every beats 1` cadence nested inside it. `locked` (no count) is the same
  // mechanism at N=1. Bed-less (locked=0) is a hard parse error for both keywords -- this is WHY
  // the shipped built-ins stay frame-based: they must parse with no bed present (see director.cpp
  // build_builtin_patterns() using whatever locked_period_frames() returns for the live program,
  // which is 0 when it has no pulsed layer).
  {
    auto pr = parse(R"(
pattern beat_locked for beats 8 {
  every beats 1 { image concept zoom (curve 0 -> 0.5) }
})",
                     /*locked=*/32);
    check(pr.ok, std::string("beats: parses with a 32f-locked bed") +
                     (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 8 * 32,
            "beats: `for beats 8` lowers to 8*locked = 256 frames");
      delete root;
      auto im = images(pr);
      check(im.size() == 1, "beats: one image draw inside the beats-cadenced pattern");
    }

    // `locked` (bare, no count) is `beats 1`.
    auto pr_locked = parse("pattern one_beat for locked { image concept zoom 0.5 }",
                            /*locked=*/32);
    check(pr_locked.ok, std::string("beats: `locked` (bare) parses with a bed") +
                             (pr_locked.ok ? "" : (" -- " + pr_locked.error)));
    if (pr_locked.ok) {
      Cycler* root = pattern::compile(pr_locked.root);
      check(root && root->length() == 32, "beats: bare `locked` == locked_period_frames (32)");
      delete root;
    }

    // Bed-less program (locked=0, the default when a session has no pulsed entrainment layer):
    // both `beats N` and bare `locked` hard-error rather than silently picking a frame count.
    auto no_bed_beats = parse("pattern p for beats 8 { image concept }");
    check(!no_bed_beats.ok,
          "beats error: `beats N` with locked=0 (no pulsed bed) is a hard parse error");
    check(no_bed_beats.error.find("bed") != std::string::npos ||
              no_bed_beats.error.find("pulsed") != std::string::npos,
          "beats error: the message names the missing pulsed bed, not a generic parse failure");

    auto no_bed_locked = parse("pattern p for locked { image concept }");
    check(!no_bed_locked.ok,
          "beats error: bare `locked` with locked=0 (no pulsed bed) is also a hard parse error");
  }

  // 4i. `audio` (§4.14): parses and lowers to Effect::Kind::Audio carrying the
  // content slot + loop flag; a LITERAL volume folds to Effect::rate (fire-once), not a
  // render-side op.
  {
    auto pr = parse("pattern a for 240f { audio concept loop volume 0.6 }");
    check(pr.ok, std::string("audio: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto audio_effects = find_effects(pr.root, pattern::Effect::Kind::Audio);
      check(audio_effects.size() == 1, "audio: lowers to exactly one Effect::Kind::Audio");
      if (audio_effects.size() == 1) {
        const auto* e = audio_effects[0];
        check(e->slot == pattern::Slot::Primary, "audio: `concept` lowers to Slot::Primary");
        check(e->force, "audio: `loop` lowers to Effect::force = true");
        check(std::fabs(e->rate - 0.6f) < 1e-4f,
              "audio: literal `volume 0.6` folds to Effect::rate at fire time");
      }
      bool has_volume_render = false;
      for (const auto& st : pr.render_block) {
        if (st.op == pattern::RenderStmt::Op::AudioVolume) has_volume_render = true;
      }
      check(!has_volume_render,
            "audio: a LITERAL volume does NOT emit a per-frame RenderStmt (fire-once only)");
    }

    // `reward` / `runtime` resolve to the other two content slots; no `loop`/`volume` is fine
    // (both optional).
    auto pr2 = parse("pattern a for 240f { audio reward }");
    check(pr2.ok, std::string("audio: `reward`, no loop/volume, parses") +
                      (pr2.ok ? "" : (" -- " + pr2.error)));
    if (pr2.ok) {
      auto audio_effects = find_effects(pr2.root, pattern::Effect::Kind::Audio);
      check(audio_effects.size() == 1 && audio_effects[0]->slot == pattern::Slot::Alternate,
            "audio: `reward` lowers to Slot::Alternate");
      check(audio_effects.size() == 1 && !audio_effects[0]->force,
            "audio: no `loop` keyword -> Effect::force stays false");
    }

    auto pr3 = parse("pattern a for 240f { audio runtime }");
    check(pr3.ok, std::string("audio: `runtime` parses") + (pr3.ok ? "" : (" -- " + pr3.error)));
    if (pr3.ok) {
      auto audio_effects = find_effects(pr3.root, pattern::Effect::Kind::Audio);
      check(audio_effects.size() == 1 && audio_effects[0]->slot == pattern::Slot::Runtime,
            "audio: `runtime` lowers to Slot::Runtime (resolved at fire time)");
      // No volume written -> the rate SENTINEL (< 0): fire-time keeps the channel's
      // current (initially full) volume. Guards the bare-`audio`-is-silent regression.
      check(audio_effects.size() == 1 && audio_effects[0]->rate < 0.f,
            "audio: absent volume lowers to the rate<0 sentinel (keep current volume)");
    }

    // Explicit `volume 0` is a REAL mute, distinct from the absent-volume sentinel.
    auto pr4 = parse("pattern a for 240f { audio concept volume 0 }");
    check(pr4.ok, std::string("audio: `volume 0` parses") + (pr4.ok ? "" : (" -- " + pr4.error)));
    if (pr4.ok) {
      auto audio_effects = find_effects(pr4.root, pattern::Effect::Kind::Audio);
      check(audio_effects.size() == 1 && audio_effects[0]->rate == 0.f,
            "audio: explicit `volume 0` lowers to rate == 0 (honored mute, not sentinel)");
    }
  }

  // 4j. `audio ... volume (curve ...)` emits a per-frame RenderStmt{Op::AudioVolume} instead
  // of folding to Effect::rate -- the "constant vs curve" fork §4.14 documents.
  {
    auto pr = parse("pattern a for 240f { audio concept volume (curve 0.2 -> 0.8) }");
    check(pr.ok, std::string("audio volume curve: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto audio_effects = find_effects(pr.root, pattern::Effect::Kind::Audio);
      check(audio_effects.size() == 1 && audio_effects[0]->rate < 0.f,
            "audio volume curve: Effect::rate stays at the sentinel (no fire-once write; "
            "the per-frame RenderStmt owns volume)");
      const pattern::RenderStmt* vol = nullptr;
      for (const auto& st : pr.render_block) {
        if (st.op == pattern::RenderStmt::Op::AudioVolume) vol = &st;
      }
      check(vol != nullptr, "audio volume curve: lowers to a RenderStmt{Op::AudioVolume}");
      check(vol && !vol->speed.empty() && progress_clock(vol->speed) != "",
            "audio volume curve: the volume expr rides the enclosing pattern's clock");
    }
  }

  // 4k. `audio stop` lowers to a standalone Effect::Kind::AudioStop with no content word
  // needed, and produces no RenderStmt (schedule-only, like `copy`).
  {
    auto pr = parse("pattern a for 240f { audio stop }");
    check(pr.ok, std::string("audio stop: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto stops = find_effects(pr.root, pattern::Effect::Kind::AudioStop);
      check(stops.size() == 1, "audio stop: lowers to exactly one Effect::Kind::AudioStop");
      check(pr.render_block.empty(), "audio stop: schedule-only, emits no RenderStmt");
    }
  }

  // 4l. `every (beats 2) { audio concept }` with locked_frames=32: the cadence lowers to a
  // Rep wrapping an Action leaf carrying the Audio effect, and the leaf length matches
  // 2*32 = 64f -- confirming `audio` composes with the SAME beats-cadence machinery `image`/
  // `word` already use, not a bespoke audio-only path.
  {
    auto pr = parse("pattern beat_audio for beats 8 { every beats 2 { audio concept } }",
                     /*locked=*/32);
    check(pr.ok, std::string("audio cadence: parses with a 32f-locked bed") +
                     (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      Cycler* root = pattern::compile(pr.root);
      check(root && root->length() == 8 * 32,
            "audio cadence: pattern span lowers to 8*locked = 256 frames");
      delete root;
      // Find the cadence leaf: an Action node carrying the Audio effect.
      const pattern::Node* leaf = nullptr;
      std::vector<const pattern::Node*> stack{&pr.root};
      while (!stack.empty()) {
        const pattern::Node* n = stack.back();
        stack.pop_back();
        for (const auto& e : n->effects) {
          if (e.kind == pattern::Effect::Kind::Audio) leaf = n;
        }
        for (const auto& c : n->children) stack.push_back(&c);
      }
      check(leaf != nullptr, "audio cadence: the Audio effect lands on an Action leaf");
      check(leaf && leaf->length == 2 * 32,
            "audio cadence: `every beats 2` leaf length is 2*locked = 64f, same math as image");
    }
  }

  // 4m. Error case: an unknown content word after `audio` is a hard parse error naming the
  // bad word (same shape as `image`/`word`'s content-word validation, content_to_slot).
  {
    auto pr = parse("pattern a for 100f { audio bogus }");
    check(!pr.ok && pr.error.find("bogus") != std::string::npos,
          "audio error: unknown content word is a hard parse error naming the bad word");
  }

  // 4n. E1 `show A..B` (§4.15): a visibility window on any draw statement. Lowers to
  // `RenderStmt.when` -- already honored every frame by render_eval -- ANDed with (never
  // replacing) whatever gate push_render/chance already put there. Parser-only, zero runtime
  // change, so the assertions are on the lowered gate string AND on how many frames of a run
  // it actually admits.
  {
    // Fraction form: the second half of each 64f beat.
    auto pr = parse("pattern p for 64f { every 64f -> beat { word concept show 0.5..1 } }");
    check(pr.ok, std::string("show fraction: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      const pattern::RenderStmt* t = nullptr;
      for (const auto& st : pr.render_block)
        if (st.op == pattern::RenderStmt::Op::Text) t = &st;
      check(t && t->when.find(".progress >= 0.5") != std::string::npos &&
                t->when.find(".progress < 1") != std::string::npos,
            "show fraction: lowers to a progress-window condition on RenderStmt.when");
      // The pattern-active gate push_render appends must SURVIVE -- a `show` that clobbered it
      // would make a sequenced phase's draws paint outside their phase.
      check(t && t->when.find(".active") != std::string::npos,
            "show fraction: the enclosing pattern's .active gate is ANDed, not replaced");
      if (t) {
        pattern::NodeMap nm;
        Cycler* c = pattern::compile(pr.root, pattern::MakeAction{}, nm);
        pattern::Registers regs;
        uint32_t on = 0;
        for (uint32_t f = 0; f < 64; ++f) {
          c->advance();
          if (pattern::eval_cond_expr(t->when, regs, nm, c)) ++on;
        }
        delete c;
        check(on == 32, "show fraction: `0.5..1` admits exactly half the 64f clock (got " +
                            std::to_string(on) + ")");
      }
    }

    // Frame-denominated form: the 8f stab the originals used for text.
    auto prf = parse("pattern p for 64f { every 64f -> beat { word concept show 0f..8f } }");
    check(prf.ok, std::string("show frames: parses") + (prf.ok ? "" : (" -- " + prf.error)));
    if (prf.ok) {
      const pattern::RenderStmt* t = nullptr;
      for (const auto& st : prf.render_block)
        if (st.op == pattern::RenderStmt::Op::Text) t = &st;
      check(t && t->when.find(".frame") != std::string::npos,
            "show frames: `0f..8f` lowers against the clock's .frame, not .progress");
      if (t) {
        pattern::NodeMap nm;
        Cycler* c = pattern::compile(prf.root, pattern::MakeAction{}, nm);
        pattern::Registers regs;
        uint32_t on = 0;
        for (uint32_t f = 0; f < 64; ++f) {
          c->advance();
          if (pattern::eval_cond_expr(t->when, regs, nm, c)) ++on;
        }
        delete c;
        check(on == 8, "show frames: `0f..8f` admits exactly 8 frames of the 64f clock (got " +
                           std::to_string(on) + ")");
      }
    }

    // Raw `[expr]` escape: substituted for `this` like any other bracket expr.
    auto pre = parse("pattern p for 64f { every 64f -> beat { image concept show [this.frame < 4] } }");
    check(pre.ok, std::string("show expr: parses") + (pre.ok ? "" : (" -- " + pre.error)));
    if (pre.ok) {
      auto im = images(pre);
      check(im.size() == 1 && im[0]->when.find("this") == std::string::npos &&
                im[0]->when.find(".frame < 4") != std::string::npos,
            "show expr: raw [expr] passes through with `this` substituted to the clock id");
    }

    // Repeated raw `show [expr]`: the composition must parenthesize the INCOMING condition,
    // not just the existing one. `and` binds tighter than `or` (render_eval.h b_or/b_and), so
    // composing an unparenthesized RHS gives `(0) and 1 or 1` -- which is TRUE, silently
    // dropping the first window's gate entirely. `show [0]` alone can never admit a frame, so
    // neither can `show [0] show [...]`, whatever the second window says.
    auto pro = parse("pattern p for 64f { every 64f -> beat { image concept "
                     "show [0] show [1 or 1] } }");
    check(pro.ok, std::string("show expr twice: parses") + (pro.ok ? "" : (" -- " + pro.error)));
    if (pro.ok) {
      auto im = images(pro);
      check(im.size() == 1, "show expr twice: exactly one image draw");
      if (im.size() == 1) {
        pattern::NodeMap nm;
        Cycler* c = pattern::compile(pro.root, pattern::MakeAction{}, nm);
        pattern::Registers regs;
        uint32_t on = 0;
        for (uint32_t f = 0; f < 64; ++f) {
          c->advance();
          if (pattern::eval_cond_expr(im[0]->when, regs, nm, c)) ++on;
        }
        delete c;
        check(on == 0,
              "show expr twice: an `or` in the second window cannot override the first "
              "(got " + std::to_string(on) + " admitted frames, want 0)");
      }
    }

    // `show` composes with `chance` on a text draw: BOTH gates must survive (the chance guard
    // used to be assigned over `when`, which would silently drop the window).
    auto prc = parse("pattern p for 64f { every 64f -> beat { word concept show 0.5..1 chance 0.5 } }");
    check(prc.ok, std::string("show+chance: parses") + (prc.ok ? "" : (" -- " + prc.error)));
    if (prc.ok) {
      const pattern::RenderStmt* t = nullptr;
      for (const auto& st : prc.render_block)
        if (st.op == pattern::RenderStmt::Op::Text) t = &st;
      check(t && t->when.find(".progress >= 0.5") != std::string::npos &&
                t->when.find("_chance") != std::string::npos,
            "show+chance: the window AND the chance guard both survive in `when`");
    }

    // Error cases: a reversed window, mixed denominations, and a frame window that overruns
    // the enclosing clock are all hard parse errors -- no silent clamp.
    auto rev = parse("pattern p for 64f { every 64f { image concept show 1..0.5 } }");
    check(!rev.ok, "show error: a window whose end <= its start is a hard parse error");

    auto mixed = parse("pattern p for 64f { every 64f { image concept show 0f..0.5 } }");
    check(!mixed.ok && mixed.error.find("frames and fractions") != std::string::npos,
          "show error: mixing frames and fractions in one window is a hard parse error");

    auto over = parse("pattern p for 128f { every 64f { image concept show 0f..128f } }");
    check(!over.ok && over.error.find("64f") != std::string::npos,
          "show error: a frame window past the enclosing clock's length names that length");

    auto frac = parse("pattern p for 64f { every 64f { image concept show 0.5..2 } }");
    check(!frac.ok, "show error: a fractional window ending past 1 is a hard parse error");
  }

  // 4o. E2 `env in X hold Y out Z` (§4.16): a piecewise-linear alpha envelope lowering to one
  // compile-time min/max [expr] -- the SAME class as `fade in/out/inout`, zero runtime change.
  // The load-bearing difference from `fade inout` is the true ABSENCE past `in+hold+out`:
  // `fade inout` is a whole-clock triangle that never actually reaches zero except at the
  // endpoints, so a layer beneath it is never alone on screen.
  {
    auto pr = parse("pattern p for 64f { every 64f -> beat { image reward env in 16f hold 16f out 16f } }");
    check(pr.ok, std::string("env: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto im = images(pr);
      check(im.size() == 1 && !im[0]->alpha.empty(),
            "env: lowers to an alpha [expr] on the draw (not a new RenderStmt field)");
      if (im.size() == 1) {
        // Sample the envelope across the clock: rise over 0-16, flat 1 over 16-32 (the hold),
        // fall over 32-48, then EXACTLY zero for the remaining 16f.
        auto alpha_at = [&](uint32_t frame) {
          pattern::NodeMap nm;
          Cycler* c = pattern::compile(pr.root, pattern::MakeAction{}, nm);
          pattern::Registers regs;
          for (uint32_t i = 0; i <= frame; ++i) c->advance();
          double a = pattern::eval_expr(im[0]->alpha, 1.0, regs, nm, c);
          delete c;
          return a;
        };
        check(alpha_at(0) < 0.05, "env: alpha starts at ~0");
        check(alpha_at(8) > 0.4 && alpha_at(8) < 0.6, "env: rises linearly through the `in` leg");
        check(alpha_at(16) > 0.99 && alpha_at(24) > 0.99 && alpha_at(31) > 0.99,
              "env: holds at full alpha across the whole `hold` leg");
        check(alpha_at(40) > 0.4 && alpha_at(40) < 0.6, "env: falls linearly through the `out` leg");
        check(alpha_at(48) == 0.0 && alpha_at(56) == 0.0 && alpha_at(63) == 0.0,
              "env: is EXACTLY absent (alpha 0) for the clock's remainder -- the hold-with-a-hole "
              "shape `fade inout` cannot express");
      }
    }

    // Fractions are accepted alongside frames; omitting `hold` gives a triangle that still
    // has the absent tail.
    auto tri = parse("pattern p for 64f { every 64f -> beat { image reward env in 0.25 out 0.25 } }");
    check(tri.ok, std::string("env triangle: fractions parse") + (tri.ok ? "" : (" -- " + tri.error)));
    if (tri.ok) {
      auto im = images(tri);
      if (im.size() == 1) {
        pattern::NodeMap nm;
        Cycler* c = pattern::compile(tri.root, pattern::MakeAction{}, nm);
        pattern::Registers regs;
        double peak = 0.0, tail = 0.0;
        for (uint32_t f = 0; f < 64; ++f) {
          c->advance();
          double a = pattern::eval_expr(im[0]->alpha, 1.0, regs, nm, c);
          peak = std::max(peak, a);
          if (f >= 32) tail = std::max(tail, a);
        }
        delete c;
        check(peak > 0.99, "env triangle: no `hold` still reaches full alpha at the apex");
        check(tail == 0.0, "env triangle: absent for everything past in+out");
      }
    }

    // Overrun is a parse error: 32+32+32 cannot fit a 64f clock. Silently clipping the release
    // would be the exact "grammar quietly degraded the intent" failure this extension exists
    // to fix.
    auto bad = parse("pattern p for 64f { every 64f { image concept env in 32f hold 32f out 32f } }");
    check(!bad.ok && bad.error.find("overruns") != std::string::npos,
          "env error: in+hold+out past the clock's length is a hard parse error");

    auto nofall = parse("pattern p for 64f { every 64f { image concept env in 16f } }");
    check(!nofall.ok, "env error: a missing `out` leg is a hard parse error");
  }

  // 4p. E3 `line` (§4.17): the `word` statement with SPLIT_LINE -- whole phrases instead of
  // one word at a time. `Effect::split` and `change_text`'s SPLIT_LINE branch both already
  // existed; this is purely the missing author surface.
  {
    auto pr = parse("pattern p for 64f { every 64f { line concept } }");
    check(pr.ok, std::string("line: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto texts = find_effects(pr.root, pattern::Effect::Kind::Text);
      check(texts.size() == 1 && texts[0]->split == 1,
            "line: lowers to a Text effect with split = SPLIT_LINE (1)");
      check(texts.size() == 1 && texts[0]->slot == pattern::Slot::Primary,
            "line: takes the same content vocabulary as `word`");
      bool has_text_stmt = false;
      for (const auto& st : pr.render_block)
        if (st.op == pattern::RenderStmt::Op::Text) has_text_stmt = true;
      check(has_text_stmt, "line: emits the same RenderStmt{Op::Text} `word` does");
    }

    // `word` must still lower to SPLIT_WORD -- `line` adds a verb, it does not change `word`.
    auto w = parse("pattern p for 64f { every 64f { word concept } }");
    if (w.ok) {
      auto texts = find_effects(w.root, pattern::Effect::Kind::Text);
      check(texts.size() == 1 && texts[0]->split == 0,
            "line: `word` is unchanged (still SPLIT_WORD)");
    }

    // `line` composes with the shared draw params (E1 included).
    auto sh = parse("pattern p for 64f { every 64f -> beat { line reward show 0.5..1 zoom 0.9 } }");
    check(sh.ok, std::string("line+show: parses") + (sh.ok ? "" : (" -- " + sh.error)));
    if (sh.ok) {
      const pattern::RenderStmt* t = nullptr;
      for (const auto& st : sh.render_block)
        if (st.op == pattern::RenderStmt::Op::Text) t = &st;
      check(t && !t->when.empty() && t->zoom == "0.900000",
            "line+show: `line` takes the same params/window surface as any other draw");
    }
  }

  // 4q. E4 `alternate` (§4.18): deterministic A/B theme ping-pong on image/anim draws. The
  // parser synthesizes a hidden scalar register + an Effect{Kind::Toggle} fired BEFORE the
  // pull, and points the pull's Effect::slot_reg at it -- `resolved_slot` already reads
  // slot_reg (compiled_visual.cpp) and Toggle already exists, so this is zero runtime change.
  {
    auto pr = parse("pattern p for 128f { every 64f { image alternate zoom 0.5 } }");
    check(pr.ok, std::string("alternate: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      auto toggles = find_effects(pr.root, pattern::Effect::Kind::Toggle);
      auto imgs = find_effects(pr.root, pattern::Effect::Kind::Image);
      check(toggles.size() == 1, "alternate: synthesizes exactly one Toggle effect");
      check(imgs.size() == 1 && !imgs[0]->slot_reg.empty(),
            "alternate: the image pull reads its slot from a register, not a fixed Slot");
      check(toggles.size() == 1 && imgs.size() == 1 && toggles[0]->target == imgs[0]->slot_reg,
            "alternate: the Toggle drives the SAME register the pull selects on");
      check(toggles.size() == 1 && toggles[0]->guard == pattern::Effect::Guard::None,
            "alternate: the bare form flips unconditionally on every firing");
      // Effect ORDER matters: the toggle must fire before the pull, or the first image of
      // each pair reads the previous firing's side.
      const pattern::Node* leaf = nullptr;
      std::vector<const pattern::Node*> stack{&pr.root};
      while (!stack.empty()) {
        const pattern::Node* n = stack.back();
        stack.pop_back();
        for (const auto& e : n->effects)
          if (e.kind == pattern::Effect::Kind::Image) leaf = n;
        for (const auto& c : n->children) stack.push_back(&c);
      }
      check(leaf != nullptr, "alternate: the pull lands on an Action leaf");
      if (leaf) {
        int toggle_at = -1, image_at = -1;
        for (std::size_t i = 0; i < leaf->effects.size(); ++i) {
          if (leaf->effects[i].kind == pattern::Effect::Kind::Toggle) toggle_at = int(i);
          if (leaf->effects[i].kind == pattern::Effect::Kind::Image) image_at = int(i);
        }
        check(toggle_at >= 0 && image_at > toggle_at,
              "alternate: the Toggle is ordered BEFORE the pull it selects for");
      }
    }

    // Two alternating draws in one pattern must keep INDEPENDENT phase (statement-scoped
    // register), not silently share one toggle.
    auto two = parse("pattern p for 128f { every 64f { image alternate -> a } every 32f { image alternate -> b } }");
    check(two.ok, std::string("alternate scope: parses") + (two.ok ? "" : (" -- " + two.error)));
    if (two.ok) {
      auto toggles = find_effects(two.root, pattern::Effect::Kind::Toggle);
      check(toggles.size() == 2 && toggles[0]->target != toggles[1]->target,
            "alternate scope: each statement mints its OWN toggle register (independent phase)");
    }

    // `alternate chance P`: the TOGGLE (not the draw) becomes probabilistic, so the theme
    // HOLDS between flips. This is the form the re-authored accelerate depends on.
    auto ch = parse("pattern p for 128f { every 64f { image alternate chance 0.5 anim } }");
    check(ch.ok, std::string("alternate chance: parses") + (ch.ok ? "" : (" -- " + ch.error)));
    if (ch.ok) {
      auto toggles = find_effects(ch.root, pattern::Effect::Kind::Toggle);
      auto rolls = find_effects(ch.root, pattern::Effect::Kind::Roll);
      auto imgs = find_effects(ch.root, pattern::Effect::Kind::Image);
      check(rolls.size() == 1, "alternate chance: synthesizes the same 100-bucket Roll `chance` uses");
      check(toggles.size() == 1 && toggles[0]->guard == pattern::Effect::Guard::Ge &&
                rolls.size() == 1 && toggles[0]->guard_reg == rolls[0]->target,
            "alternate chance: the guard lands on the TOGGLE (the flip is probabilistic, the "
            "draw still paints every firing)");
      check(imgs.size() == 1 && imgs[0]->guard == pattern::Effect::Guard::None,
            "alternate chance: the image pull itself is NOT gated (unlike a draw's `chance P`)");
      // The animation load must follow the same toggle, so the still and its animation never
      // come from opposite themes.
      auto anims = find_effects(ch.root, pattern::Effect::Kind::Anim);
      check(anims.size() == 1 && imgs.size() == 1 && anims[0]->slot_reg == imgs[0]->slot_reg,
            "alternate chance: a trailing `anim` load rides the same toggle as the pull");
    }

    // The standalone `anim alternate` load (for a burst `enter { }` theme pivot).
    auto sa = parse("pattern p for 128f { every 64f { anim alternate } }");
    check(sa.ok, std::string("anim alternate: parses") + (sa.ok ? "" : (" -- " + sa.error)));
    if (sa.ok) {
      auto anims = find_effects(sa.root, pattern::Effect::Kind::Anim);
      auto toggles = find_effects(sa.root, pattern::Effect::Kind::Toggle);
      check(anims.size() == 1 && toggles.size() == 1 && !anims[0]->slot_reg.empty() &&
                anims[0]->slot_reg == toggles[0]->target,
            "anim alternate: the standalone animation load ping-pongs off its own toggle");
    }

    // `alternate` is image/anim only -- text resolves its slot the same way but the point of
    // the extension is theme ping-pong on the PULL, and a bare error beats a silent no-op.
    auto bad = parse("pattern p for 128f { every 64f { word alternate } }");
    check(!bad.ok && bad.error.find("alternate") != std::string::npos,
          "alternate error: `word alternate` is a hard parse error naming the bad content word");
  }

  // 5. Every shipped v3 built-in parses and lowers to a runnable tree, and every RenderStmt in
  // its render_block evaluates to finite, sane-bounded numbers at frames {0, mid, end-1} with
  // the cycler tree actually advanced -- not just string-matched. This is the gap parse-time
  // `over` resolution can't close: a malformed expr the LOWERING code itself emits (not the
  // author's source text) would silently resolve to 0.0/skip rather than fail to parse.
  {
    const char* names[] = {"", "accelerate", "slow_flash", "sub_text",     "flash_text",
                           "simple",         "super_parallel", "animation", "super_fast"};
    for (uint32_t t = 1; t <= 8; ++t) {
      auto pr = parse(builtin::pattern_source_v3(t));
      check(pr.ok, std::string("builtin ") + names[t] + ": parses" +
                       (pr.ok ? "" : (" -- " + pr.error)));
      if (pr.ok) {
        Cycler* root = pattern::compile(pr.root);
        check(root && root->length() > 0,
              std::string("builtin ") + names[t] + ": lowers to a runnable tree (len=" +
                  std::to_string(root ? root->length() : 0) + ")");
        delete root;
        eval_render_block_at_frames(pr.root, pr.render_block,
                                    std::string("builtin ") + names[t]);
      }
    }
  }

  // 6. Every render_block produced by the test patterns above (sections 1-4f) also evaluates to
  // finite, sane-bounded numbers -- the same tripwire, but against the custom patterns this file
  // exercises (crossfade, sibling scoping, spiral/warp params, ramp cadence, burst), not just the
  // 8 built-ins.
  {
    struct Case { const char* label; const char* src; };
    const Case cases[] = {
        {"simplest", "pattern hello for 240f { image concept zoom (curve 0 -> 0.5) }"},
        {"crossfade", R"(
pattern flash_text for 1024f {
  pattern life for 128f loop 8 {
    every 64f -> beat {
      copy cur -> prev
      draw prev          zoom (curve 0.5 -> 1.0)
      image concept -> cur fade in zoom (curve 0 -> 0.5)
    }
  }
})"},
        {"siblings", R"(
pattern twin for 512f {
  pattern a for 128f loop 4 { every 64f -> b { copy cur -> prev  image reward  -> cur fade in } draw prev fade out }
  pattern c for 128f loop 4 { every 64f -> d { copy cur -> prev  image concept -> cur fade in } draw prev fade out }
})"},
        {"spiral speed", R"(
pattern s for 240f {
  look { spiral type=3 width=6 }
  image concept zoom 0.5
  spiral speed (curve 1 -> 3)
})"},
        {"warp", R"(
pattern w for 240f {
  warp amplitude (curve 0 -> 0.3) wavelength 0.2 speed 2
  image concept zoom 0.5
})"},
        {"drunk", "pattern d for 240f { drunk (curve 0 -> 0.3) image concept zoom 0.5 }"},
        {"ramp cadence", R"(
pattern ramped for 300f {
  every ramp 40f -> 10f steps 10 ease late -> cut {
    image concept zoom 0.5
  }
})"},
        {"ramp over-parent", R"(
pattern ramped2 for 200f {
  every ramp 20f -> 10f steps 5 -> cut {
    image concept zoom (curve 0 -> 1 over ramped2)
  }
})"},
        {"ramp this-anchor", R"(
pattern ramped3 for 200f {
  every ramp 20f -> 10f steps 5 -> cut {
    image concept zoom (curve 0 -> 1)
  }
})"},
        {"burst", R"(
pattern burster for 512f {
  burst -> rapid period 8f chance 1/24 cooldown 32f duration 32f..96f {
    base  { image runtime zoom 0.5 }
    burst { image runtime zoom (curve 1 -> 2 over rapid) alpha [this.index] }
  }
})"},
        {"beats", R"(
pattern beat_locked for beats 8 {
  every beats 1 { image concept zoom (curve 0 -> 0.5) }
})"},
        {"audio", R"(
pattern mantra_pulse for beats 16 {
  every beats 4 { audio concept loop volume (curve 0.2 -> 0.8) }
  every beats 1 { image concept zoom (curve 0 -> 0.4) }
})"},
        // The four parser-only extensions (§4.15-§4.18): their lowered `when`/`alpha` strings
        // are built by templates, so they get the same malformed-expr tripwire everything
        // else in this block gets.
        {"show/env/line/alternate", R"(
pattern extended for 256f {
  every 64f -> beat {
    image alternate chance 0.5 -> cur env in 16f hold 16f out 16f anim every 2nd
    line reward show 0.5..1 zoom 0.8
    word concept show 0f..8f
    caption runtime show [this.frame < 32]
  }
})"},
    };
    for (const auto& c : cases) {
      // `beats`/`locked` lengths need a non-zero locked period to parse at all (see 4h); every
      // other case here is frame-based and indifferent to it, so one shared locked value covers
      // both without a per-case knob.
      auto pr = parse(c.src, /*locked=*/32);
      check(pr.ok, std::string("expr-eval ") + c.label + ": parses" +
                       (pr.ok ? "" : (" -- " + pr.error)));
      if (pr.ok) {
        eval_render_block_at_frames(pr.root, pr.render_block, std::string("expr-eval ") + c.label);
      }
    }
  }

  // 6. Behavioral regressions. Each of these
  //    encodes one user-visible regression against the original hand-written visuals, at the
  //    level that broke: pacing distribution, zoom ranges, layer alphas, burst render gating.
  {
    // 6a. accelerate pacing: the ramp must RUSH off the slow end and DWELL at the fast end.
    //     The `ease late` authoring regression inverted this: 46 cuts, ~3% of time at fast.
    //     Re-specified by the owner (issue #42) to supersede original-parity: a HARDER up-ramp
    //     that arrives at the strobe around the middle of the run and then sustains it, rather
    //     than the previous authoring's 2772f span that only touched fast cuts ~73% in.
    auto pr = parse(builtin::pattern_source_v3(1));
    check(pr.ok, std::string("builtin accelerate: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      // Collect the ramp's sampled segment lengths IN ORDER: every Action leaf carrying an
      // Image effect. Order matters here -- "when does the strobe arrive" is a prefix sum, so
      // walk children front-to-back rather than through a LIFO stack.
      std::vector<uint32_t> segs;
      std::function<void(const pattern::Node&)> walk = [&](const pattern::Node& n) {
        if (n.type == pattern::Node::Type::Action) {
          for (const auto& e : n.effects) {
            if (e.kind == pattern::Effect::Kind::Image) {
              segs.push_back(n.length);
              break;
            }
          }
        }
        for (const auto& c : n.children) walk(c);
      };
      walk(pr.root);
      uint64_t total = 0, fast = 0;
      for (uint32_t s : segs) {
        total += s;
        if (s <= 16) fast += s;
      }
      check(segs.size() >= 120,
            "accelerate: at least 120 cuts (got " + std::to_string(segs.size()) + ")");
      check(total == 2048, "accelerate: segment lengths sum to the 2048f span (got " +
                               std::to_string(total) + ")");
      // The up-ramp titration: a big share of the run must be AT the strobe, not approaching
      // it. The previous authoring sat at 27%.
      check(total > 0 && 100 * fast / total >= 40,
            "accelerate: >= 40% of runtime at fast (<= 16f) cuts -- the harder up-ramp (got " +
                std::to_string(total ? 100 * fast / total : 0) + "%)");
      // ARRIVAL: the frame at which the cut length first drops to <= 16f. The owner spec asks
      // for arrival to FEEL like ~2048-2300f overall, which means the strobe has to land
      // around the middle of the run and hold, not show up in the last quarter.
      uint64_t arrival = total, elapsed = 0;
      for (uint32_t s : segs) {
        if (s <= 16) {
          arrival = elapsed;
          break;
        }
        elapsed += s;
      }
      check(total > 0 && 100 * arrival / total <= 60,
            "accelerate: reaches <= 16f cuts by 60% of the run -- the strobe ARRIVES and then "
            "sustains (got " +
                std::to_string(total ? 100 * arrival / total : 100) + "%)");
      // ...and once there it stays: the tail must be a run of fast cuts, not a single dip.
      uint32_t tail_fast = 0;
      for (auto it = segs.rbegin(); it != segs.rend() && *it <= 16; ++it) ++tail_fast;
      check(tail_fast >= 60,
            "accelerate: the run ENDS in a sustained strobe of >= 60 consecutive <= 16f cuts "
            "(got " +
                std::to_string(tail_fast) + ")");
    }

    // 6a-bis. animation's still layer must have a HOLD and a HOLE. This is the user-reported
    //     anchor regression: the still was authored with `fade inout`, a whole-clock triangle
    //     whose alpha is nonzero at nearly every frame and whose peak lasts an instant. Under
    //     it the animation is never alone on screen, so "the animation is the subject" -- the
    //     entire point of this visual -- stops being true. `env` restores both the plateau at
    //     full alpha and the true absence. Evaluate the still's alpha across a whole 64f turn.
    pr = parse(builtin::pattern_source_v3(7));
    check(pr.ok, std::string("builtin animation: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      // The still lane is the one drawing the `still` register; the base lane draws `cur`.
      const pattern::RenderStmt* still = nullptr;
      for (const auto& st : pr.render_block) {
        if (st.op == pattern::RenderStmt::Op::Image &&
            st.image_reg.find("still") != std::string::npos) {
          still = &st;
        }
      }
      check(still != nullptr, "animation: the still layer draws its own `still` register");
      if (still) {
        check(!still->alpha.empty(),
              "animation: the still layer carries an alpha envelope (not a bare full-alpha draw)");
        pattern::NodeMap node_map;
        Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
        pattern::Registers regs;
        // Sample one steady-state 64f cycle from the middle of the 1024f run, so the reading is
        // not contaminated by the first turn's offset ramp-up.
        std::vector<double> a(64, 0.0);
        for (uint32_t f = 0; f < 512; ++f) compiled->advance();
        for (uint32_t f = 0; f < 64; ++f) {
          compiled->advance();
          a[f] = pattern::eval_expr(still->alpha, 1.0, regs, node_map, compiled);
        }
        delete compiled;

        uint32_t absent = 0, full = 0;
        double peak = 0.0;
        for (double v : a) {
          if (v <= 0.0) ++absent;
          if (v > 0.99) ++full;
          peak = std::max(peak, v);
        }
        // The HOLE: the whole reason to prefer `env` over `fade inout`. A triangle scores 1
        // absent frame here; the restored envelope leaves the animation alone for 33.
        check(absent >= 24,
              "animation: the still is ABSENT for >= 24 frames of each 64f cycle, so the "
              "animation holds the stage alone -- the hole `fade inout` cannot express (got " +
                  std::to_string(absent) + ")");
        // The HOLD: a plateau at full alpha, not an instantaneous peak. `fade inout` on a 64f
        // clock touches 1.0 for exactly one frame.
        check(full >= 8,
              "animation: the still HOLDS at full alpha for >= 8 frames -- a plateau, not the "
              "instantaneous peak of a triangle (got " +
                  std::to_string(full) + ")");
        check(peak > 0.99, "animation: the still's envelope actually reaches full alpha");
        // And it is a genuine envelope, not a square gate: both legs must ramp through
        // intermediate values rather than snapping on and off.
        uint32_t partial = 0;
        for (double v : a)
          if (v > 0.01 && v < 0.99) ++partial;
        check(partial >= 8,
              "animation: the still RAMPS in and out (intermediate alphas on both legs), rather "
              "than snapping on like a `show` gate (got " +
                  std::to_string(partial) + ")");
      }
    }

    // 6b. flash_text zoom cap: image zoom near 1.0 projects onto the near plane and the
    //     mirror-tiled grid degenerates into a garbled mosaic (the "jigsaw" report). Evaluate
    //     every image draw's zoom at every frame of a full run; none may exceed 0.85.
    pr = parse(builtin::pattern_source_v3(4));
    check(pr.ok, std::string("builtin flash_text: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      pattern::NodeMap node_map;
      Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
      pattern::Registers regs;
      double max_zoom = 0.0;
      for (uint32_t f = 0; f < compiled->length(); ++f) {
        compiled->advance();
        for (const auto& st : pr.render_block) {
          if (st.op != pattern::RenderStmt::Op::Image) continue;
          if (!pattern::eval_cond_expr(st.when, regs, node_map, compiled)) continue;
          max_zoom = std::max(max_zoom, pattern::eval_expr(st.zoom, 0.0, regs, node_map, compiled));
        }
      }
      delete compiled;
      check(max_zoom <= 0.85, "flash_text: image zoom stays <= 0.85 over a full run (jigsaw guard)");
      check(max_zoom >= 0.7, "flash_text: zoom still reaches the original's ~0.8 peak");
    }

    // 6c. super_parallel layering: three image lanes with source-over alphas 1 / 0.5 / 0.33 and
    //     two staggered offsets -- three full-alpha layers just show whichever drew last, which
    //     was the "only one image at a time" regression.
    pr = parse(builtin::pattern_source_v3(6));
    check(pr.ok, std::string("builtin super_parallel: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      std::vector<double> alphas;
      pattern::Registers regs;
      pattern::NodeMap node_map;
      Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
      for (const auto& st : pr.render_block) {
        if (st.op == pattern::RenderStmt::Op::Image) {
          alphas.push_back(pattern::eval_expr(st.alpha, 1.0, regs, node_map, compiled));
        }
      }
      delete compiled;
      std::sort(alphas.begin(), alphas.end());
      check(alphas.size() == 3, "super_parallel: three image layers");
      check(alphas.size() == 3 && alphas[0] < 0.4 && alphas[1] < 0.6 && alphas[2] == 1.0,
            "super_parallel: source-over alpha stack ~{1, 0.5, 0.33}");
      uint32_t offsets = 0;
      std::vector<const pattern::Node*> stack{&pr.root};
      while (!stack.empty()) {
        const pattern::Node* n = stack.back();
        stack.pop_back();
        if (n->type == pattern::Node::Type::Off) ++offsets;
        for (const auto& c : n->children) stack.push_back(&c);
      }
      check(offsets == 2, "super_parallel: two offset lanes (32f/64f stagger)");
    }

    // 6d. super_fast burst gating: the base cuts and the burst animation must be FSM-gated so
    //     exactly one paints at any frame -- an ungated always-anim burst draw painted one
    //     animation over the whole pattern (the "no cuts at all" regression). Also: the
    //     animation is picked ONCE per burst (enter block), not re-rolled every period.
    pr = parse(builtin::pattern_source_v3(8));
    check(pr.ok, std::string("builtin super_fast: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      const pattern::RenderStmt* base_stmt = nullptr;
      const pattern::RenderStmt* burst_stmt = nullptr;
      for (const auto& st : pr.render_block) {
        if (st.op != pattern::RenderStmt::Op::Image) continue;
        (st.has_anim ? burst_stmt : base_stmt) = &st;
      }
      check(base_stmt && !base_stmt->has_anim, "super_fast: base draw is a still image");
      check(burst_stmt && burst_stmt->has_anim, "super_fast: burst draw renders the animation");
      const pattern::Node* burst_node = nullptr;
      std::vector<const pattern::Node*> stack{&pr.root};
      while (!stack.empty()) {
        const pattern::Node* n = stack.back();
        stack.pop_back();
        if (n->type == pattern::Node::Type::Burst) burst_node = n;
        for (const auto& c : n->children) stack.push_back(&c);
      }
      check(burst_node && burst_node->burst_enter_effects.size() == 1 &&
                burst_node->burst_enter_effects[0].kind == pattern::Effect::Kind::Anim,
            "super_fast: burst picks its animation once on entry (enter block)");
      if (base_stmt && burst_stmt) {
        pattern::NodeMap node_map;
        Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
        pattern::Registers regs;
        bool exclusive = true, saw_base = false, saw_burst = false;
        for (uint32_t f = 0; f < compiled->length(); ++f) {
          compiled->advance();
          bool b = pattern::eval_cond_expr(base_stmt->when, regs, node_map, compiled);
          bool a = pattern::eval_cond_expr(burst_stmt->when, regs, node_map, compiled);
          exclusive = exclusive && (b != a);
          saw_base = saw_base || b;
          saw_burst = saw_burst || a;
        }
        delete compiled;
        check(exclusive, "super_fast: base/burst draws are mutually exclusive every frame");
        check(saw_base, "super_fast: the still-cut base actually paints");
        // saw_burst is random-chance-driven; over 256 periods at 1/12 it is overwhelmingly
        // likely, and asserting it guards the gate polarity (a flipped gate would never fire).
        check(saw_burst, "super_fast: a burst fired and its animation painted");
      }
    }

    // 6e. Per-image zoom motion (the universal "no zoom" regression): a builtin image draw's
    //     zoom must GROW over one image's on-screen life -- a constant `zoom 0.5` reads as a
    //     static magnification. simple's 64f cadence: zoom near frame 8 << zoom near frame 56.
    pr = parse(builtin::pattern_source_v3(5));
    check(pr.ok, std::string("builtin simple: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      const pattern::RenderStmt* img = nullptr;
      for (const auto& st : pr.render_block) {
        if (st.op == pattern::RenderStmt::Op::Image) img = &st;
      }
      check(img != nullptr, "simple: has an image draw");
      if (img) {
        auto zoom_at = [&](uint32_t frame) {
          pattern::NodeMap node_map;
          Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
          pattern::Registers regs;
          for (uint32_t i = 0; i <= frame; ++i) compiled->advance();
          double z = pattern::eval_expr(img->zoom, 0.0, regs, node_map, compiled);
          delete compiled;
          return z;
        };
        double early_z = zoom_at(8), late_z = zoom_at(56);
        check(late_z > early_z + 0.2,
              "simple: zoom rides the image's own 64f life (got " + std::to_string(early_z) +
                  " -> " + std::to_string(late_z) + ")");
      }
    }
  }

  // 7. Runtime-unit regressions.
  {
    // 7a. Burst cooldown/duration are authored in FRAMES but BurstCycler counts period
    //     TICKS -- the parser must convert (`duration 16f` at period 8f = 2 ticks = 16
    //     frames, not 16 ticks = 128 frames). chance 1/1 makes entry deterministic
    //     (random_chance(1) always hits), so the first burst starts at the first tick.
    auto pr = parse(R"(
pattern p for 512f {
  burst period 8f chance 1/1 cooldown 32f duration 16f {
    base  { image concept }
    burst { word concept }
  }
})");
    check(pr.ok, std::string("burst units: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      pattern::NodeMap node_map;
      Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
      // Find the Burst cycler in the tree.
      const Cycler* burst = nullptr;
      std::vector<const Cycler*> stack{compiled};
      while (!stack.empty()) {
        const Cycler* c = stack.back();
        stack.pop_back();
        if (std::string(c->type_name()) == "Burst") burst = c;
        for (const Cycler* k : c->children()) stack.push_back(k);
      }
      check(burst != nullptr, "burst units: burst cycler found in compiled tree");
      if (burst) {
        uint32_t first_burst_frames = 0, cooldown_frames = 0;
        bool in_first = false, first_done = false;
        for (uint32_t f = 0; f < 512 && !(first_done && burst->index() == 1); ++f) {
          compiled->advance();
          if (burst->index() == 1 && !first_done) {
            in_first = true;
            ++first_burst_frames;
          } else if (in_first) {
            in_first = false;
            first_done = true;
            ++cooldown_frames;
          } else if (first_done) {
            ++cooldown_frames;
          }
        }
        check(first_burst_frames == 16,
              "burst units: `duration 16f` lasts 16 frames (got " +
                  std::to_string(first_burst_frames) + ")");
        check(cooldown_frames >= 32,
              "burst units: `cooldown 32f` holds >= 32 frames before the next burst (got " +
                  std::to_string(cooldown_frames) + ")");
      }
      delete compiled;
    }

    // 7b. OffsetCycler wraps: an `offset` lane's position()/progress must stay inside
    //     [0, length] across many cycles (it previously ran away past length forever,
    //     breaking .progress/.active introspection on offset lanes).
    pr = parse("pattern p for 128f { every 32f offset 16f { image concept } }");
    check(pr.ok, std::string("offset wrap: parses") + (pr.ok ? "" : (" -- " + pr.error)));
    if (pr.ok) {
      pattern::NodeMap node_map;
      Cycler* compiled = pattern::compile(pr.root, pattern::MakeAction{}, node_map);
      const Cycler* off = nullptr;
      std::vector<const Cycler*> stack{compiled};
      while (!stack.empty()) {
        const Cycler* c = stack.back();
        stack.pop_back();
        if (std::string(c->type_name()) == "Offset") off = c;
        for (const Cycler* k : c->children()) stack.push_back(k);
      }
      check(off != nullptr, "offset wrap: offset cycler found in compiled tree");
      bool bounded = true;
      for (uint32_t f = 0; f < 5 * 128 && off; ++f) {
        compiled->advance();
        bounded = bounded && off->position() <= off->length();
      }
      check(bounded, "offset wrap: position stays within [0, length] over 5 cycles");
      delete compiled;
    }
  }

  std::cout << "\n" << (g_fail ? "FAILED: " : "all v3 grammar checks passed (")
            << g_fail << (g_fail ? " failed\n" : " failures)\n");
  return g_fail ? 1 : 0;
}
