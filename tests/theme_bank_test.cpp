// ThemeBank tiered-selection regression test.
//
// WHY THIS EXISTS. Tier-weighted selection shipped delivering 4.44% of its intended
// distribution with 75% of frames stale repeats (commit f32631c). It had been "verified"
// against 300 images inheriting 300 -- equal tier sizes, where tier SIZE and tier WEIGHT
// agree by construction, which is precisely the configuration the defect is invisible in.
// The defect was that get_image picked a tier by WEIGHT while do_load filled the cache
// from a flat, tier-blind shuffler, so residency tracked SIZE: the tier the weights
// favoured was usually not in RAM when it came up, and every miss silently repeated the
// previous frame. Both halves only show when size and weight DISAGREE, so every case here
// uses a small tier inheriting a much larger one.
//
// NOT headless: ThemeBank::get_image uploads through Image::ensure_texture_uploaded, which
// needs a CURRENT OpenGL context (but not a window -- sf::Context is enough). Labelled
// `gpu` in ctest so `ctest -LE gpu` stays green on a GPU-less runner, and it skips loudly
// with success if no context can be made.
//
// No RNG seed exists (util.h's get_mersenne_twister is seeded from std::random_device), so
// every assertion here is a PROPERTY with a statistical band, never a pick sequence. A seed
// API is deliberately not added: it is a durable API change whose main use would be exactly
// the sequence-freezing tests this file must not become.
#include <common/media/image.h>
#include <common/session.h>
#include <common/session_json.h>
#include <common/trance.pb.h>
#include <trance/theme_bank.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#pragma warning(push, 0)
#include <SFML/OpenGL.hpp>
#include <SFML/Window.hpp>
#pragma warning(pop)

namespace
{
  // See test_cache_residency: an observed flake rate of ~0.33% against a defect that
  // produced 66-80%, so 2% separates them by more than an order of magnitude.
  const double kMaxRepeatRate = 0.02;

  int g_fail = 0;
  void check(bool ok, const std::string& what)
  {
    std::cout << (ok ? "  ok  " : "FAIL  ") << what << "\n";
    if (!ok) ++g_fail;
  }

  // Scratch directory under the system temp dir, like session_json_test: safe to run
  // repeatedly and in parallel with the rest of the suite, and never touches the repo.
  std::filesystem::path scratch_root()
  {
    auto p = std::filesystem::temp_directory_path() / "trance_theme_bank_test";
    std::filesystem::create_directories(p);
    return p;
  }

  void write_file(const std::filesystem::path& path, const std::string& content)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f{path, std::ios::binary};
    f << content;
  }

  // Fixtures as byte arrays rather than checked-in binaries. The WIDTH is the tier tag:
  // ThemeBank exposes no per-pick provenance, but Image::width() survives the whole
  // load -> upload -> copy path, so writing each tier at a different width is enough to
  // tell which tier a drawn frame came from without instrumenting the class under test.
  const uint32_t kParentWidth = 1;
  const uint32_t kOwnWidth = 2;

  // 1x1 RGBA PNG.
  const unsigned char kPng1x1[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
      0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7,
      0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

  // 2x1 RGBA PNG.
  const unsigned char kPng2x1[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
      0x00, 0xf4, 0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x84, 0x01, 0x11, 0xf7, 0x03, 0xfd, 0xfe,
      0xad, 0xbb, 0x99, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
      0x82};

  void write_png(const std::filesystem::path& path, const unsigned char* data, std::size_t size)
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f{path, std::ios::binary};
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  }

  // A 2-frame 1x1 GIF89a, hand-assembled: header, 1x1 logical screen with a 2-colour
  // global table (black, white), then two frames each wrapped in a graphics-control
  // extension carrying a 10/100s delay, each one LZW-coded pixel. Written out rather than
  // checked in as a binary for the same reason as the PNGs above -- and because the
  // animation-leak case needs a file the real GifStreamer will actually open.
  const unsigned char kGif2Frame[] = {
      'G',  'I',  'F',  '8',  '9',  'a',              // header
      0x01, 0x00, 0x01, 0x00, 0xf0, 0x00, 0x00,       // 1x1, global table of 2, bg 0
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff,             // palette: black, white
      0x21, 0xf9, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, // GCE, delay = 10 (0.1s)
      0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,  // image descriptor
      0x02, 0x02, 0x44, 0x01, 0x00,                   // LZW: clear, index 0, end
      0x21, 0xf9, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, // GCE, delay = 10 (0.1s)
      0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,  // image descriptor
      0x02, 0x02, 0x4c, 0x01, 0x00,                   // LZW: clear, index 1, end
      0x3b};                                          // trailer

  // A real synthetic media tree plus the session that describes it:
  //
  //   <root>/media/          parent_count 1x1 PNGs   -> theme "media"
  //   <root>/media/own/      own_count    2x1 PNGs   -> theme "media/own", inherits "media"
  //
  // Loaded through load_session_json rather than hand-built protos so the tier layout under
  // test is the one the real loader derives (resolve_theme_inheritance), not a
  // reimplementation of it that could agree with a broken runtime. auto_rescan is turned OFF
  // so folder discovery cannot add themes behind the test's back.
  struct Fixture {
    std::filesystem::path root;
    trance_pb::Session session;
    trance_pb::System system;
    SessionJsonSidecar sidecar;
    std::unique_ptr<ThemeBank> bank;
  };

  std::string enabled_theme_json(uint32_t own_weight, uint32_t parent_weight)
  {
    // A tier's weight is the rotation weight of the theme it came FROM, falling back to 1
    // when that theme carries none (theme_bank.cpp set_program). So "own 4 : parent 1" is
    // expressible either by weighting only the child, or by weighting both.
    std::string s = R"({ "theme_name": "media/own", "random_weight": )" +
        std::to_string(own_weight) + " }";
    if (parent_weight) {
      s += R"(, { "theme_name": "media", "random_weight": )" + std::to_string(parent_weight) + " }";
    }
    return s;
  }

  Fixture make_fixture(const std::string& name, int own_count, int parent_count,
                       uint32_t image_cache_size, const std::string& enabled)
  {
    Fixture fx;
    fx.root = scratch_root() / name;
    // remove_all at the START, not the end: a failing case leaves its tree on disk to be
    // inspected.
    std::filesystem::remove_all(fx.root);
    for (int i = 0; i < parent_count; ++i) {
      write_png(fx.root / "media" / ("p" + std::to_string(i) + ".png"), kPng1x1, sizeof(kPng1x1));
    }
    for (int i = 0; i < own_count; ++i) {
      write_png(fx.root / "media" / "own" / ("o" + std::to_string(i) + ".png"), kPng2x1,
                sizeof(kPng2x1));
    }
    write_file(fx.root / "s.session.json", std::string{R"json({
  "format": "trance-session", "format_version": 1,
  "first_playlist_item": "main",
  "playlist": { "main": { "standard": { "program": "p" } } },
  "program_map": { "p": { "global_fps": 120, "enabled_theme": [ )json"} +
                               enabled + R"json( ] } },
  "theme_scan_root": { "dir": ".", "auto_rescan": false },
  "theme_map": {
    "media": { "scan": "media" },
    "media/own": { "scan": { "dir": "media/own", "inherit": true } }
  }
})json");

    fx.session =
        load_session_json((fx.root / "s.session.json").string(), fx.root.string(), fx.sidecar);
    fx.system = get_default_system();
    fx.system.set_image_cache_size(image_cache_size);
    // ThemeBank's constructor loads its initial active themes SYNCHRONOUSLY (the
    // `while (!all_loaded()) do_reconcile(theme)` loop), so nothing here has to race the
    // async loader -- and async_update() is deliberately never called, which keeps the
    // resident set fixed for the duration of a measurement.
    fx.bank = std::make_unique<ThemeBank>(fx.root.string(), fx.session, fx.system,
                                          fx.session.program_map().at("p"), fx.sidecar.theme_tiers);
    return fx;
  }

  // Asserts the loader produced the two-tier layout the runtime is being tested against.
  // Without this a silent change in resolve_theme_inheritance would turn every case below
  // into a vacuous flat-pool test that still passes -- which is the same class of mistake
  // (structurally cannot observe the defect) that let the original bug ship.
  void check_tier_layout(const Fixture& fx, uint32_t own_count, uint32_t parent_count)
  {
    auto it = fx.sidecar.theme_tiers.find("media/own");
    bool ok = it != fx.sidecar.theme_tiers.end() && it->second.size() == 2 &&
        it->second[0] == std::make_pair(std::string{"media/own"}, own_count) &&
        it->second[1] == std::make_pair(std::string{"media"}, parent_count);
    check(ok, "loader derives a 2-tier pool: own " + std::to_string(own_count) +
              " then inherited " + std::to_string(parent_count));
    check(fx.session.theme_map().at("media/own").image_path_size() ==
              static_cast<int>(own_count + parent_count),
          "the inheriting theme's pool is the union of both tiers");
  }

  struct Sample {
    std::size_t picks = 0;
    std::size_t own = 0;      // drawn from tier 0 (the 2x1 fixtures)
    std::size_t repeats = 0;  // same GL texture as the immediately-previous pick
    std::size_t blank = 0;    // no image, or an image with no uploaded texture
    std::set<uint32_t> distinct;

    double own_share() const
    {
      return picks ? double(own) / double(picks) : 0.;
    }
    double repeat_share() const
    {
      return picks ? double(repeats) / double(picks) : 0.;
    }
  };

  // Image::texture() is the per-image GL name, and it is stable identity here: textures are
  // only ever recycled through Image::delete_textures(), which this test never calls, and no
  // unload runs while sampling.
  Sample sample(ThemeBank& bank, std::size_t picks)
  {
    Sample s;
    uint32_t previous = 0;
    for (std::size_t i = 0; i < picks; ++i) {
      Image image = bank.get_image(false);
      ++s.picks;
      if (!image || !image.texture()) {
        ++s.blank;
        continue;
      }
      if (image.width() == kOwnWidth) {
        ++s.own;
      }
      if (previous && image.texture() == previous) {
        ++s.repeats;
      }
      previous = image.texture();
      s.distinct.insert(image.texture());
    }
    return s;
  }

  void report(const std::string& label, const Sample& s)
  {
    std::cout << "  [" << label << "] picks=" << s.picks << " own=" << std::fixed
              << std::setprecision(2) << 100. * s.own_share() << "% repeats=" << 100.
            * s.repeat_share() << "% blank=" << s.blank << " distinct=" << s.distinct.size()
              << std::defaultfloat << "\n";
  }

  // ------------------------------------------------------------------------------------
  // CASE 1 -- CACHE RESIDENCY.
  //
  // Failure mode: the cache is filled with a distribution the draw does not sample from, so
  // most picks land on an index that is in the pool but not in RAM. get_image then repeats
  // the previous frame (or hands back an un-uploaded image), and the same tier is free to
  // miss again on the very next call. Pre-fix this collapsed a 120-image pool to a handful
  // of stale frames; the shipped measurement was 75% repeats.
  //
  // The pool is 7.5x the cache on purpose: with pool <= cache everything is resident and the
  // assertion cannot fail whatever the load path does.
  // ------------------------------------------------------------------------------------
  void test_cache_residency()
  {
    auto fx = make_fixture("residency", 20, 100, 16, enabled_theme_json(4, 0));
    check_tier_layout(fx, 20, 100);

    auto s = sample(*fx.bank, 300);
    report("residency", s);
    check(s.blank == 0, "every pick returns an image with an uploaded GL texture");
    // A near-zero bound, NOT zero. The original claim here was that the 8-deep recency
    // demotion over 16 resident images makes a consecutive repeat impossible -- that was
    // wrong, and this assertion flaked once at 1 repeat in 300 (0.33%). The residency set
    // is not static while sampling, so the ring can briefly leave a tier with a single
    // un-demoted candidate. Bound it at 2% instead of asserting a false invariant: the
    // defect this catches produced 66-80% repeats, so the discriminating power is intact
    // and the flake is absorbed.
    check(s.repeats <= kMaxRepeatRate * 300,
          "consecutive-repeat rate stays under 2% (the defect measured 66-80%)");
    // 16 cache slots, all of which the 8-deep recency ring rotates through over 300 picks.
    // 14 leaves room for two stragglers without leaving room for a collapse.
    check(s.distinct.size() >= 14,
          "at least 14 distinct textures appear over 300 picks (cache is 16)");
  }

  // ------------------------------------------------------------------------------------
  // CASE 2 -- TIER MIX WITH UNEQUAL SIZES. The case the original 300-vs-300 test
  // structurally could not catch.
  //
  // 10 own images inheriting 280, weighted 4:1 in favour of the small tier, cache 21 --
  // the feature's own motivating configuration and the one the 4.44% figure was measured at.
  //
  // Failure mode: the mix is decided by how many files each folder happens to hold instead
  // of by the rotation weights, so a small folder inheriting a big one is drowned. Tier
  // SIZE and tier WEIGHT only disagree when the tiers differ in size, which is why equal
  // tiers can never observe this.
  //
  // BAND. The tier draw is an independent weighted coin per pick, so the own-tier share is
  // Binomial(n=3000, p=0.8): sigma = sqrt(p(1-p)/n) = 0.73 percentage points. The band
  // [70%, 90%] is +/-10 points = 13.7 sigma, generous enough to absorb the fall-through
  // that fires when the chosen tier has nothing resident. A flat, size-proportional union
  // draws the own tier 10/290 = 3.4% of the time -- 105 sigma below the band -- and the
  // shipped defect measured 4.44%. Both are far outside; nothing near the boundary is
  // being adjudicated.
  // ------------------------------------------------------------------------------------
  void test_tier_mix_with_unequal_sizes()
  {
    auto fx = make_fixture("tier_mix", 10, 280, 21, enabled_theme_json(4, 0));
    check_tier_layout(fx, 10, 280);

    auto s = sample(*fx.bank, 3000);
    report("tier_mix 4:1", s);
    check(s.blank == 0, "every pick returns an image with an uploaded GL texture");
    check(s.repeats <= kMaxRepeatRate * 3000,
          "consecutive-repeat rate stays under 2% (the defect measured 80%)");
    check(s.own_share() >= .70 && s.own_share() <= .90,
          "a 10-image tier inheriting 280 at 4:1 takes 70-90% of frames "
          "(size-proportional would be 3.4%)");
  }

  // ------------------------------------------------------------------------------------
  // CASE 3 -- THE WEIGHT IS LIVE.
  //
  // Failure mode: the F2 panel's rotation-weight slider moves and the mix does not follow.
  // set_program refreshes tier_weights from the program's enabled_theme rows, and that is
  // the ONLY thing that connects the user-facing weight to the tier draw; nothing else
  // exercises it. A tier layout captured once at construction would pass both cases above
  // and still leave the slider dead.
  //
  // It also pins the direction: a runtime that simply always preferred tier 0, or that
  // ignored the weights and used pool order, satisfies case 2 and fails here.
  //
  // Same bank, no reload -- so this is genuinely the live re-weight path and not a second
  // construction. Reversing to 1:4 puts the parent tier on 80%; the own share should fall
  // to ~20%. Binomial(3000, 0.2) has the same 0.73-point sigma, so the [10%, 30%] band is
  // 13.7 sigma either side, and the pre-reweight value of ~80% is 82 sigma above it.
  // ------------------------------------------------------------------------------------
  void test_weight_change_moves_the_mix()
  {
    auto fx = make_fixture("reweight", 10, 280, 21, enabled_theme_json(4, 0));
    auto before = sample(*fx.bank, 3000);
    report("reweight 4:1", before);
    check(before.own_share() >= .70 && before.own_share() <= .90,
          "baseline: own tier takes 70-90% at 4:1");

    trance_pb::Program reversed = fx.session.program_map().at("p");
    reversed.clear_enabled_theme();
    auto* own = reversed.add_enabled_theme();
    own->set_theme_name("media/own");
    own->set_random_weight(1);
    auto* parent = reversed.add_enabled_theme();
    parent->set_theme_name("media");
    parent->set_random_weight(4);
    fx.bank->set_program(reversed);

    auto after = sample(*fx.bank, 3000);
    report("reweight 1:4", after);
    check(after.blank == 0, "every pick still returns an uploaded texture after a re-weight");
    check(after.own_share() >= .10 && after.own_share() <= .30,
          "reversing the rotation weights to 1:4 moves the own tier to 10-30% of frames");
  }
  // ------------------------------------------------------------------------------------
  // CASE 4 -- ANIMATIONS STAY IN THEIR OWN THEME.
  //
  // Failure mode (reported live: "a lot of hypno gifs loading in folders that contain
  // none"): ThemeInfo::animation_shuffler expresses membership as "my indices are at
  // priority 1, everyone else's are at 0", and Shuffler::next() draws from the highest
  // OCCUPIED level. A theme with no animations of its own has nothing above 0, so the top
  // level is the entire session's animation list and every pick is some other theme's --
  // which do_load_animation then opens straight off disk. Images have a residency check
  // that mostly absorbs the same fall-through; animations have nothing.
  //
  // Two halves, and the second is the one that keeps the fix honest: returning nothing
  // unconditionally would satisfy the first on its own.
  //
  // No statistical band here. This is a hard invariant -- an animation-less theme must
  // NEVER produce a frame -- so one leak in any number of attempts is a failure.
  // ------------------------------------------------------------------------------------
  struct AnimFixture {
    std::filesystem::path root;
    trance_pb::Session session;
    trance_pb::System system;
    SessionJsonSidecar sidecar;
    std::unique_ptr<ThemeBank> bank;
  };

  // <root>/anim/a.gif   -> theme "anim"  (the only animation in the session)
  // <root>/still/o.png  -> theme "still" (images only)
  // `enabled` names which of the two the program rotates, so the same tree can be run
  // both ways -- the leak is a property of which theme is ACTIVE, not of the tree.
  AnimFixture make_anim_fixture(const std::string& name, const std::string& enabled)
  {
    AnimFixture fx;
    fx.root = scratch_root() / name;
    std::filesystem::remove_all(fx.root);
    write_png(fx.root / "still" / "o.png", kPng2x1, sizeof(kPng2x1));
    write_file(fx.root / "anim" / "a.gif",
               std::string{reinterpret_cast<const char*>(kGif2Frame), sizeof(kGif2Frame)});
    // A theme whose only file is a .png the decoder cannot read. It therefore has a
    // non-zero image COUNT (so it is drawable, stays in the rotation, and is not treated
    // as animation-only) but can never make an image resident -- the one configuration in
    // which a lane genuinely has nothing to hand back. Case 5 needs it: the never-black
    // fallback is only distinguishable from a real pick when a real pick is impossible.
    write_file(fx.root / "broken" / "b.png", "this is not a png");
    write_file(fx.root / "s.session.json", std::string{R"json({
  "format": "trance-session", "format_version": 1,
  "first_playlist_item": "main",
  "playlist": { "main": { "standard": { "program": "p" } } },
  "program_map": { "p": { "global_fps": 120, "enabled_theme": [ )json"} +
                   enabled + R"json( ] } },
  "theme_scan_root": { "dir": ".", "auto_rescan": false },
  "theme_map": { "anim": { "scan": "anim" }, "still": { "scan": "still" },
                 "broken": { "scan": "broken" } }
})json");
    fx.session =
        load_session_json((fx.root / "s.session.json").string(), fx.root.string(), fx.sidecar);
    fx.system = get_default_system();
    fx.bank = std::make_unique<ThemeBank>(fx.root.string(), fx.session, fx.system,
                                          fx.session.program_map().at("p"), fx.sidecar.theme_tiers);
    return fx;
  }

  // ------------------------------------------------------------------------------------
  // CASE 5 -- A LANE'S THEME CHANGE IS DETECTABLE, AND THE NO-FALLBACK FETCH IS HONEST.
  //
  // Failure mode (reported live: "a single image of the unloaded theme gets stuck on"):
  // an `image` effect captures ONE Image into a register and the render block redraws it
  // until that effect fires again. Image is ref-counted, so a frame of a theme that has
  // since been unloaded stays perfectly valid and keeps displaying. The renderer can only
  // notice if the bank tells it the lane's theme changed -- hence lane_generation.
  //
  // The subtle half, and the reason this case exists rather than just the counter: the
  // refresh must not accept get_image's never-black fallback as a fresh pick. That
  // fallback returns the PREVIOUS theme's frame and operator bool cannot tell it from a
  // real one, so stamping it as current would mark the register up-to-date while it still
  // held the dead theme's image -- and it would never retry. get_current_theme_image is
  // the honest fetch: content from the lane's CURRENT theme, or nothing.
  //
  // Provenance is checked by fixture WIDTH (gif 1x1, still 2x1), not by emptiness.
  // ------------------------------------------------------------------------------------
  void test_theme_change_is_detectable()
  {
    auto fx = make_anim_fixture("lane_gen",
                                R"({ "theme_name": "still", "random_weight": 1 },)"
                                R"({ "theme_name": "broken", "random_weight": 1 })");

    auto lane_theme = [&](bool alternate) {
      auto snap = fx.bank->debug_snapshot();
      return snap.slots[alternate ? 2 : 1].name;
    };

    const std::string before_theme = lane_theme(false);
    const uint32_t before_gen = fx.bank->lane_generation(false);

    // A swap is rate-limited (change_themes sets a 500-update cooldown that async_update
    // drains), so this needs a generous pump rather than a few iterations. Runs the full
    // count rather than stopping at the first swap, to sample both themes in the lane.
    std::size_t dishonest = 0;
    std::size_t broken_samples = 0;
    bool swapped = false;
    for (int i = 0; i < 6000; ++i) {
      fx.bank->async_update();
      fx.bank->advance_frames();
      fx.bank->change_themes();
      swapped = swapped || lane_theme(false) != before_theme;

      // Pull on every iteration, the way the renderer does. This is what LOADS the gun:
      // it keeps the lane's last-good frame populated with the good theme's image, so
      // that when the lane later holds "broken" there is something for a dishonest
      // fallback to hand back. Without it the check below is vacuous -- the fallback
      // returns an empty image and looks identical to the honest answer.
      fx.bank->get_image(false);

      // THE assertion. While the lane holds "broken" -- a theme that can never make an
      // image resident -- an honest fetch has exactly one correct answer: nothing. The
      // never-black fallback would answer with "still"'s 2x1 PNG instead, and
      // Image::operator bool() cannot tell those apart, which is precisely why a caller
      // must not infer freshness from it. Sampled by WIDTH so a wrong answer is
      // identified, not merely detected.
      if (lane_theme(false) == "broken") {
        ++broken_samples;
        Image honest = fx.bank->get_current_theme_image(false);
        if (honest) {
          ++dishonest;
        }
      }
    }

    check(swapped, "the primary lane's theme changes when two themes are enabled "
                   "(otherwise the rest of this case proves nothing)");
    check(broken_samples > 0,
          "the lane actually held the unloadable theme at some point (" +
              std::to_string(broken_samples) + " samples) -- without this the assertion "
              "below is vacuous");
    check(fx.bank->lane_generation(false) != before_gen,
          "lane_generation moves when the lane's theme changes, so a holder of a captured "
          "image can tell that what it is holding went stale");
    check(dishonest == 0,
          "on a theme that can produce nothing, get_current_theme_image returns nothing "
          "rather than passing off another theme's last-good frame as a fresh pick "
          "(violations: " + std::to_string(dishonest) + "/" +
              std::to_string(broken_samples) + ")");
  }

  void test_animations_stay_in_their_theme()
  {
    {
      auto fx = make_anim_fixture("anim_leak",
                                  R"({ "theme_name": "still", "random_weight": 1 })");
      check(fx.session.theme_map().at("anim").animation_path_size() == 1,
            "the loader put the .gif in the anim theme (otherwise this proves nothing)");
      check(fx.session.theme_map().at("still").animation_path_size() == 0,
            "the still theme has no animations of its own");
      // Asserted by PROVENANCE, not by emptiness: a stills-only theme asked to animate
      // now legitimately hands back one of its OWN stills (see below), so "returned
      // something" no longer distinguishes the leak. The fixtures are tagged by width --
      // the gif is 1x1 and the still theme's only PNG is 2x1 -- so a width-1 frame under
      // the still theme can only have come from the other theme's folder.
      //
      // async_update() is what asks for the next streamer, so it is what drives
      // do_load_animation. Both lanes, repeatedly: one pass could miss by luck, and a
      // uniform draw over a one-animation pool cannot miss at all once it is reached.
      std::size_t leaked = 0;
      for (int i = 0; i < 50; ++i) {
        fx.bank->async_update();
        fx.bank->advance_frames();
        for (bool alt : {false, true}) {
          Image frame = fx.bank->get_animation(alt);
          if (frame && frame.width() == kParentWidth) {
            ++leaked;
          }
        }
      }
      check(leaked == 0, "a theme with no animations never draws another theme's gif "
                         "(leaked on " + std::to_string(leaked) + "/100 attempts)");

      // ...and it must not go BLACK either. Refusing the foreign gif is only half the
      // rule: an `anim` draw op landing on a theme that owns no animations has to fall
      // back to that theme's own stills. Removing the leak WITHOUT this is what put black
      // screens on screen, and most themes in a real corpus are stills-only, so this is
      // the common path rather than an edge case.
      std::size_t blank = 0;
      for (int i = 0; i < 50; ++i) {
        fx.bank->async_update();
        fx.bank->advance_frames();
        Image frame = fx.bank->get_animation(false);
        if (!frame || !frame.texture()) {
          ++blank;
        }
      }
      check(blank == 0, "asked to animate, a stills-only theme draws its own stills "
                        "instead of nothing (blank on " + std::to_string(blank) + "/50)");
    }
    {
      auto fx = make_anim_fixture("anim_own",
                                  R"({ "theme_name": "anim", "random_weight": 1 })");
      // The other half: the guard rejects FOREIGN animations, not all of them. The
      // AsyncStreamer constructor fills its buffer synchronously, so the frame is there
      // before the first async_update.
      bool got_frame = static_cast<bool>(fx.bank->get_animation(false));
      for (int i = 0; !got_frame && i < 50; ++i) {
        fx.bank->async_update();
        fx.bank->advance_frames();
        got_frame = static_cast<bool>(fx.bank->get_animation(false));
      }
      check(got_frame, "the theme that owns the gif still draws it");

      // The other half of "never black": this theme has NO stills at all, so every
      // `image` draw op has to be served from its gifs. (get_image has done this for a
      // long time; it is asserted here because the fallback chain was rebuilt around it.)
      std::size_t blank = 0;
      for (int i = 0; i < 50; ++i) {
        fx.bank->async_update();
        fx.bank->advance_frames();
        Image frame = fx.bank->get_image(false);
        if (!frame || !frame.texture()) {
          ++blank;
        }
      }
      check(blank == 0, "asked for an image, an animation-only theme draws its gif "
                        "instead of nothing (blank on " + std::to_string(blank) + "/50)");

      auto snap = fx.bank->debug_snapshot();
      check(snap.slots[1].total == 0 && snap.slots[1].animations == 1,
            "the debug snapshot reports 0 images AND 1 animation, so an all-gif theme "
            "cannot read as an empty one");
      check(fx.bank->lane_is_animation_only(false),
            "the lane reports itself animation-backed, so the renderer knows to re-read "
            "the live frame instead of holding the captured one");

      // The frames must actually MOVE, at the rate the file asks for. The fixture gif is
      // two frames at a 10/100s delay each and the program runs at 120 content ticks a
      // second, so one second of ticks is 120/12 = 10 advances. Asserted as a band, but a
      // narrow one: this is arithmetic, not sampling -- there is no RNG in the path. It
      // discriminates against the ways this goes wrong: the old fixed-rate counter ran
      // every animation at a flat 15fps regardless of the file (15 advances), a broken
      // delay advances every tick (120), and a frozen lane never advances (0).
      std::size_t advances = 0;
      uint32_t previous = 0;
      for (int i = 0; i < 120; ++i) {
        fx.bank->advance_frames();
        Image frame = fx.bank->get_animation(false);
        const uint32_t texture = frame ? frame.texture() : 0;
        if (previous && texture && texture != previous) {
          ++advances;
        }
        previous = texture;
      }
      check(advances >= 8 && advances <= 12,
            "a 10-frames-per-second gif advances ~10 frames in a second of content time "
            "(saw " + std::to_string(advances) + ", want 8-12)");
    }
  }
} // namespace

int main()
{
  // A GL CONTEXT is all ThemeBank needs -- no window. get_image -> do_video_upload ->
  // Image::ensure_texture_uploaded issues glGenTextures/glTexImage2D against whatever
  // context is current, and nothing in this test draws.
  sf::Context context;
  if (!context.setActive(true) || !glGetString(GL_VERSION)) {
    std::cout << "\n"
              << "############################################################\n"
              << "## SKIPPED: theme_bank_test needs a current OpenGL context\n"
              << "## and none could be created here (headless / GPU-less box).\n"
              << "## NOTHING WAS TESTED. Run this on a machine with a GPU, or\n"
              << "## exclude it deliberately with `ctest -LE gpu`.\n"
              << "############################################################\n";
    return 0;
  }

  test_cache_residency();
  test_tier_mix_with_unequal_sizes();
  test_weight_change_moves_the_mix();
  test_animations_stay_in_their_theme();
  test_theme_change_is_detectable();

  if (g_fail) {
    std::cout << g_fail << " check(s) failed\n";
    return 1;
  }
  std::cout << "all checks passed\n";
  return 0;
}
