#ifndef TRANCE_SRC_COMMON_SESSION_H
#define TRANCE_SRC_COMMON_SESSION_H
#include <map>
#include <string>
#include <vector>

struct SessionJsonSidecar;

std::string make_relative(const std::string& from, const std::string& to);

namespace trance_pb
{
  class Colour;
  class PlaylistItem_NextItem;
  class Session;
  class System;
  class Theme;
}

bool is_image(const std::string& path);
bool is_animation(const std::string& path);
bool is_font(const std::string& path);
bool is_text_file(const std::string& path);
bool is_audio_file(const std::string& path);

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
void search_audio_files(std::vector<std::string>& files, const std::string& root);

// Loads system.json (session_json.h). A legacy .cfg proto path is rejected with a
// fatal error -- trance.exe no longer reads proto system configs.
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
// lists; the sidecar-less overload is for read-only callers who never save (playback).
// save_session has no sidecar-less form for that reason: a save without the sidecar
// silently rewrites the session with its patterns and scans frozen flat.
trance_pb::Session load_session(const std::string& path);
trance_pb::Session load_session(const std::string& path, SessionJsonSidecar& sidecar);
void save_session(const trance_pb::Session& session, const std::string& path,
                   SessionJsonSidecar& sidecar);
trance_pb::Session get_default_session();
void validate_session(trance_pb::Session& session);

#endif