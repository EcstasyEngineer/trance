#ifndef TRANCE_SRC_COMMON_SESSION_H
#define TRANCE_SRC_COMMON_SESSION_H
#include <map>
#include <string>

struct SessionJsonSidecar;

namespace trance_pb
{
  class Colour;
  class PlaylistItem_NextItem;
  class Session;
  class System;
  class Theme;
}

bool is_enabled(const trance_pb::PlaylistItem_NextItem& next_item,
                const std::map<std::string, std::string>& variables);

// The theme holding loose files that sit DIRECTLY at the scan root rather than in any
// subdirectory. A reserved name: a path component can never contain a slash, so this
// can't collide with a real directory's theme name. (Same trick the retired
// /wildcards/ pseudo-theme used -- but where /wildcards/ was merged into every other
// theme and then erased, this one is a first-class theme with its own weight.)
extern const char* const kRootThemeName;

// Walks `root` and builds one theme per DIRECTORY THAT DIRECTLY CONTAINS AT LEAST ONE
// file. A file's theme is its immediate parent directory, named by its root-relative
// path: `hypno/spam/x.jpg` is theme "hypno/spam", `hypno/y.jpg` is theme "hypno", and a
// loose file at the root is kRootThemeName. A directory that only contains other
// directories is a pure container and yields no theme, so nothing needs to special-case
// it. (This replaces the old "first path component wins" rule, which flattened every
// nested folder into its top-level ancestor -- all of hypno/* was one "hypno" theme --
// leaving leaf folders unaddressable.)
//
// Themes are NOT merged into one another here. Inheriting a parent directory's loose
// files is a per-theme opt-in resolved at load time (session_json.cpp), not a property
// of the scrape.
//
// Classification is a dispatch, not a filter: known animation/font/text/audio extensions
// go to their own lists and EVERYTHING ELSE is an image, since the decode layer marks
// undecodable files dead once. Only session machinery (.json/.session/.pattern/.cfg/
// .trance), dotfiles and recurring junk (is_scan_ignored) are skipped.
//
// `theme_scan` also reports {theme name -> directory} so a caller saving the result can
// emit a `scan` key instead of freezing the expansion (#36). Unlike the old rule this is
// now reported for EVERY theme unconditionally: with no cross-theme merge, each theme is
// exactly the content of one directory, so each one is faithfully reproducible from its
// folder. It is a required out-param rather than an overload precisely because dropping
// it on the floor is what silently freezes a scan into a literal file list.
void search_resources(trance_pb::Session& session, const std::string& root,
                      std::map<std::string, std::string>& theme_scan);
// Walks ONE directory's own files -- a theme is a directory, so there is nothing to
// recurse into: a subdirectory is its own theme.
void search_resources(trance_pb::Theme& theme, const std::string& root);

// Loads system.json (session_json.h). A legacy .cfg proto path is rejected with a
// fatal error -- main.cpp's startup migrates a sibling legacy system.cfg into
// system.json (load_legacy_system) before this is ever retried.
trance_pb::System load_system(const std::string& path);
void save_system(const trance_pb::System&, const std::string& path);
trance_pb::System get_default_system();
void validate_system(trance_pb::System& session);

// Loads a *.session.json (session_json.h). `root` (the session's media root) is
// derived from `path`'s parent directory. A legacy .session proto path is transparently
// auto-converted: it's read via load_legacy_session, saved as the same-stem .json next
// to it (the original .session is left untouched; an already-existing .json is
// preferred and never overwritten by a conversion), and the written JSON is what
// actually gets loaded. A missing .json path with a same-stem .session sibling gets
// the same treatment. Overload with `sidecar` also
// returns the pattern-file/scan-directory sidecar (session_json.h) needed to save the
// session back without freezing patterns inline or scan expansions into explicit
// lists; every caller in the program uses that form, and the sidecar-less overload
// survives only as convenience for tests that assert on validation and never save.
// save_session has no sidecar-less form: a save without the sidecar silently rewrites
// the session with its patterns and scans frozen flat.
trance_pb::Session load_session(const std::string& path);
trance_pb::Session load_session(const std::string& path, SessionJsonSidecar& sidecar);
void save_session(const trance_pb::Session& session, const std::string& path,
                   SessionJsonSidecar& sidecar);
trance_pb::Session get_default_session();
void validate_session(trance_pb::Session& session);

// The clock every pattern in this engine is authored against, and the default a new
// Program gets for `global_fps`.
//
// global_fps is the CONTENT clock -- how fast the cycler tree ticks -- and NOT the rate
// at which frames reach the screen. The main loop drains however many content ticks the
// elapsed wall-clock bought and then presents at most once (src/trance/main.cpp), so the
// present rate is bounded by vsync / the presentation cap in render.cpp, never by this
// number. Consequently lowering global_fps does not save a single GPU frame; it stretches
// the grammar, whose only duration unit is the raw frame (`every 64f`), so every visual
// literally runs slower. That is why this stays at the rate the shipped built-ins were
// authored and measured against (builtin_patterns_v3.cpp quotes exact frame counts).
//
// It is a named constant rather than a bare literal because the number also has to be
// quoted anywhere odds or rates are expressed per authored frame -- see the visual
// stickiness heuristic in director.cpp, which previously compared against a loose `120`
// that only worked because it happened to equal this.
constexpr unsigned int kAuthoringFps = 120;

#endif