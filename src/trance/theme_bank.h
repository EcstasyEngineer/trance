#ifndef TRANCE_SRC_TRANCE_THEME_BANK_H
#define TRANCE_SRC_TRANCE_THEME_BANK_H
#include <common/media/image.h>
#include <common/util.h>
#include <trance/media/async_streamer.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push, 0)
#include <google/protobuf/repeated_field.h>
#pragma warning(pop)

namespace trance_pb
{
  class Program;
  class Session;
  class System;
  class Theme;
}

// ThemeBank keeps two Themes active at all times with a number of images
// in memory each so that a variety of these images can be displayed with no
// load delay. It also asynchronously loads a third theme into memory so that
// the active themes can be swapped out.
class ThemeBank
{
public:
  // `theme_tiers` (from SessionJsonSidecar) describes how each theme's image pool splits
  // into its own content plus inherited ancestor content, as contiguous {source, count}
  // spans in pool order. Pass an empty map for the flat behaviour: every pool is then one
  // tier and selection is a plain shuffle over the union.
  ThemeBank(const std::string& root_path, const trance_pb::Session& session,
            const trance_pb::System& system, const trance_pb::Program& program,
            const std::map<std::string, std::vector<std::pair<std::string, uint32_t>>>&
                theme_tiers = {});

  const std::string& get_root_path() const;
  void set_program(const trance_pb::Program& program);

  // Advance animation frames.
  void advance_frames();
  // Get a random loaded in-memory image, text string, etc.
  //
  // These are called from the main (rendering) thread and can upload
  // images from RAM to video memory on-demand.
  //
  // NEVER BLACK. Both fall back to the other kind of content and then to the lane's last
  // good frame, so a draw op only ever produces nothing when the lane has shown nothing
  // at all yet. The two are a preference, not a partition: a theme that is nothing but
  // gifs (a folder of animations, `theme.size == 0`) serves every `image` draw from its
  // animations, and a theme with no gifs serves every `anim` draw from its stills.
  // Enforcing that here rather than in the grammar is deliberate -- a pattern asks for
  // the LOOK it wants, and which of the two a given folder happens to hold is not
  // something the pattern author can know.
  Image get_image(bool alternate);
  Image get_animation(bool alternate);
  // True when the theme on this lane has no still images at all, so every get_image on
  // it is really a frame of one of its animations.
  //
  // The draw side needs this, not just the load side. An `image` effect CAPTURES one
  // Image into a register and the render block redraws that captured value every frame
  // for the rest of the cut -- which is right for a still and freezes a gif on one frame.
  // Knowing the lane is animation-backed is what lets the renderer re-read the live frame
  // instead. See VisualApiImpl::render_image.
  bool lane_is_animation_only(bool alternate) const;
  const std::string& get_text(bool alternate, bool exclusive);
  const std::string& get_font(bool alternate);
  // Random pick from the active theme's precanned audio pool; empty
  // string when the theme has no audio_path entries. Same shape as get_font --
  // the grammar (not ThemeBank) decides when/how loud to play the result.
  const std::string& get_audio(bool alternate);

  // Allow an animation to change this frame.
  void change_animation(bool alternate);
  // If the next theme has been fully loaded, swap it out for one of the two
  // active themes.
  bool change_themes();
  bool swaps_to_match_theme() const;

  // Called from separate update thread to perform async loading/unloading.
  void async_update();

  // Read-only snapshot of the bank's internal state for the debug overlay.
  // Slots map to the active-theme queue: [0] unloading, [1] primary (used by
  // get_image(false)), [2] alternate (get_image(true)), [3] loading next.
  struct DebugSnapshot {
    struct Slot {
      bool valid;
      std::string name;
      // Images only -- `loaded` is what the cache holds, `total` the theme's whole
      // image pool.
      uint32_t loaded;
      uint32_t total;
      // Animations the theme owns. Reported separately because it is NOT part of the
      // loaded/total ratio: gifs are streamed on demand, never cached like stills. A
      // theme that is a folder of nothing but gifs therefore reads 0/0 images and is
      // perfectly healthy -- which is exactly what a reader took for "nothing loaded,
      // hence the black screen", so the count has to be on screen next to the ratio.
      uint32_t animations;
    };
    std::array<Slot, 4> slots;
    std::vector<std::pair<std::string, uint32_t>> enabled_weights;
    std::string pinned;
    uint32_t image_cache_size;
    uint32_t swaps_to_match;
  };
  DebugSnapshot debug_snapshot() const;

private:
  uint32_t cache_per_theme() const;
  static const std::size_t switch_cooldown = 500;
  static const std::size_t last_image_count = 8;

  // Data for each possible theme.
  struct ThemeInfo {
    // Number of images.
    const std::size_t size;
    // Whether the theme is enabled in the theme shuffler.
    bool enabled;
    // Used for synchronizing image loads.
    std::mutex load_mutex;
    // Used for synchronizing theme changes.
    std::atomic<std::size_t> loaded_size;
    // Indexes of images that this theme has caused to be loaded.
    std::vector<std::size_t> loaded_index;
    // Shuffler for loading images; maps onto all_images.
    Shuffler load_shuffler;
    // Shuffler for picking loaded images; also maps onto all_images. Stays the union of
    // every tier, so loading, recency bookkeeping and the no-tiers case all use it.
    Shuffler image_shuffler;
    // Shuffler for choosing animations. Maps on to all_animations.
    Shuffler animation_shuffler;
    // All font paths for this theme.
    std::vector<std::string> font_paths;
    // All precanned audio paths for this theme.
    std::vector<std::string> audio_paths;
    // All texts for this theme.
    std::vector<std::string> text_lines;
    // Lookup from text to index.
    std::unordered_map<std::string, std::vector<std::size_t>> text_lookup;
    // Shuffler for choosing text lines. Maps on to text_lines above.
    Shuffler text_shuffler;
    // Theme name (for the debug overlay). Value-initialized by the aggregate
    // construction in the constructor and assigned immediately afterwards.
    std::string name;

    // --- Tiered selection. Declared LAST on purpose: ThemeInfo is aggregate-initialized
    // positionally in the constructor, so a field inserted higher up silently shifts
    // every initializer after it. Trailing members are value-initialized, which is
    // exactly right here (empty = "this theme has no tiers").
    //
    // One shuffler per TIER of the pool (tier 0 = the theme's own images, the rest its
    // ancestor chain), each covering only that tier's indices. Empty when the theme
    // inherits nothing, in which case image_shuffler is used directly.
    //
    // Why tiers exist: a plain union samples by raw file count, so a 10-image folder
    // inheriting a 280-image parent shows the parent ~97% of the time no matter what the
    // rotation weights say -- directory size silently overrides intent. Picking a tier by
    // weight FIRST and an image within it second makes the weights mean what they look
    // like they mean.
    std::vector<Shuffler> tier_shufflers;
    // Parallel to tier_shufflers: the source theme each tier came from, so set_program
    // can re-read its rotation weight without rebuilding anything.
    std::vector<std::string> tier_sources;
    // Parallel to tier_shufflers: each tier's current weight, refreshed by set_program.
    std::vector<uint32_t> tier_weights;
    // Parallel to tier_shufflers: which _all_images indices each tier owns.
    //
    // Load/unload/failure bookkeeping MUST be applied only to the tiers that actually
    // contain the index. The flat image_shuffler keeps loaded images exactly one priority
    // level above unloaded ones, and Shuffler::next() returns a member of the HIGHEST
    // level -- so a stray +1 on an index a tier does not own would leave it above that
    // tier's own failed members (which sit at 0 after their base priority is stripped)
    // and let another tier's image be drawn from it.
    std::vector<std::unordered_set<std::size_t>> tier_members;
    // Parallel to tier_shufflers: the same split applied to the LOAD side.
    //
    // Residency has to be built with the distribution selection SAMPLES with. Loading flat
    // over the merged pool makes the resident set proportional to tier SIZE while
    // selection is proportional to tier WEIGHT, so the tier the weights favour is usually
    // not in RAM when it is picked. Measured on 10 images inheriting 280 at 4:1 with a
    // 21-image cache: 0.9 of the 10 resident, 75% of picks finding nothing resident, and
    // the own tier taking 4% of frames instead of the intended 80%.
    std::vector<Shuffler> tier_load_shufflers;
    // Parallel to tier_shufflers: how many of the tier's members are currently resident,
    // so a fully-resident tier can be held out of the weighted load pick instead of
    // spending cache slots re-loading what it already has.
    std::vector<std::size_t> tier_loaded_count;
    // Whether the theme has anything at all to put on screen (images, animations or text).
    // Assigned right after the aggregate construction, like `name`.
    bool drawable = false;

    // Which _all_images / _all_animations indices this theme actually owns (the resolved
    // pool, inheritance already folded in). Assigned after construction, like `name`.
    //
    // Shuffler cannot answer this itself: membership is encoded as "priority above the
    // 0 everything starts at", so a theme whose members have all been decreased to 0 --
    // or that never had any -- makes next() draw from the whole pool instead. These sets
    // are what the selection paths check the answer against, so a theme can never draw
    // another theme's content. See do_load_animation.
    std::unordered_set<std::size_t> image_members;
    std::unordered_set<std::size_t> animation_members;
  };

  // Data for each possible image.
  struct ImageInfo {
    const std::string path;
    // Reference count; corresponds to ThemeInfo::loaded_index.
    uint32_t use_count;
    std::unique_ptr<Image> image;
    // The file failed to load once; never retried and never handed to the
    // draw (image_shuffler) path, so a bad file can't poison a slot with a
    // blank texture or burn CPU on repeated decode attempts.
    bool failed = false;
  };

  void advance_theme();
  bool all_loaded() const;
  bool all_unloaded() const;

  // The still half of get_image: the tiered/flat shuffle over this theme's own RESIDENT
  // images, plus the recency bookkeeping a successful pick owes the other themes. Returns
  // an empty Image when the theme has no images or none of them are loaded yet. Does NOT
  // fall back to anything -- that is what lets get_animation use it as its own fallback
  // without the two calling each other in a circle.
  Image get_still_image(bool alternate);
  // The raw current frame of a lane's animation streamer, uploaded. Empty when the lane
  // has no animation (a theme with no gifs of its own) or is between them.
  Image get_animation_frame(bool alternate);

  // Called from the async_update thread and can load images from files
  // into RAM as necessary.
  void do_swap(std::size_t active_theme_index);
  void do_reconcile(ThemeInfo& theme);
  void do_load(ThemeInfo& theme);
  void do_unload(ThemeInfo& theme);
  std::unique_ptr<Streamer> do_load_animation(bool alternate);
  void do_video_upload(const Image& image) const;
  void do_purge();

  const std::string _root_path;
  // Data for all images.
  std::vector<ImageInfo> _all_images;
  std::vector<std::size_t> _last_images;
  std::vector<std::string> _all_animations;
  // Parallel to _all_animations: file failed to load once, never retried
  // (AsyncStreamer::async_update asks for a replacement streamer every 10ms
  // tick, so a retryable failure means reopening/reparsing the file ~100x/s).
  // Async-thread-only (do_load_animation + constructor).
  std::vector<bool> _animation_dead;
  std::string _last_text;
  // Last valid image handed out per lane ([0] primary, [1] alternate), held so
  // a theme with nothing currently drawable repeats the previous image instead
  // of flashing an empty (black) frame. Render-thread-only.
  std::array<Image, 2> _last_good_image;

  std::unique_ptr<AsyncStreamer> _streamer;
  std::unique_ptr<AsyncStreamer> _alt_streamer;
  bool _animation_theme_changed = false;
  bool _alt_animation_theme_changed = false;
  bool _change_animation = false;
  bool _alt_change_animation = false;

  // Maps theme name to index in theme vector.
  std::unordered_map<std::string, std::size_t> _theme_map;
  std::unordered_map<std::string, uint32_t> _enabled_theme_weights;
  std::string _pinned_theme;
  // Vector of themes.
  std::vector<std::unique_ptr<ThemeInfo>> _themes;
  // Currently-active themes in queue.
  std::array<std::atomic<ThemeInfo*>, 4> _active_themes;

  const uint32_t _image_cache_size;
  uint32_t _swaps_to_match_theme;
  uint32_t _updates;
  uint32_t _global_fps;
  std::atomic<uint32_t> _cooldown;

  mutable std::mutex _purge_mutex;
  mutable std::vector<std::shared_ptr<sf::Image>> _purgeable_images;
};

#endif