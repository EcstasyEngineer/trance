#ifndef TRANCE_SRC_COMMON_SESSION_JSON_H
#define TRANCE_SRC_COMMON_SESSION_JSON_H
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// JSON <-> trance_pb mapping for session.json / system.json, per the normative spec
// docs/session-json-format.md. The in-memory model stays trance_pb::Session /
// trance_pb::System; only the on-disk format is JSON. Everything here is exercised
// through session.{h,cpp}'s load_session /
// load_system / save_session / save_system -- callers outside session.cpp shouldn't
// need to include this header directly.

namespace trance_pb
{
  class Session;
  class System;
}

// Per-session sidecar (spec sec 5): information the loader recovers from the JSON that
// has no home in trance_pb, needed so a subsequent save
// writes pattern text back to its original file and re-emits theme `scan` keys instead
// of freezing their expansion into explicit lists.
struct SessionJsonSidecar
{
  // {program name, pattern name} -> pattern file path (root-relative).
  std::map<std::pair<std::string, std::string>, std::string> pattern_file;
  // theme name -> scan directory (root-relative), when the theme used `scan`.
  std::map<std::string, std::string> theme_scan;
  // Scan themes that fold their PARENT directory's theme pool into their own. Composes
  // transitively through the chain: hypno/spam reaches the root's loose files only if
  // hypno inherits too. Resolved at load (resolve_theme_inheritance) by unioning the
  // parent's already-resolved pool, so the runtime never sees the distinction.
  std::set<std::string> theme_inherit;
  // theme name -> media paths (root-relative) held OUT of its scan expansion. Inverted
  // persistence: a scan theme stores what to leave out, so a file dropped into the
  // folder later is included automatically instead of being invisible until someone
  // re-freezes the list. Applies to the theme's OWN expansion only -- an inherited image
  // has to be excluded on the theme that owns it, since that is where the scan producing
  // it runs.
  //
  // Entries are never dropped automatically, not on load and not on save: a name the scan
  // did not produce is equally "deleted" and "not synced yet", and a half-materialized
  // cloud folder would take the whole list with it. They accumulate at one short string
  // per file ever excluded, and re-checking the box in F2 removes one.
  std::map<std::string, std::vector<std::string>> theme_exclude;
  // Media directory the theme SET itself is derived from (root-relative; "." is the
  // session root). Empty means the session's themes are a fixed manifest -- the
  // pre-hierarchy behaviour, and what a hand-written session gets unless it opts in.
  std::string theme_scan_root;
  // With a scan root set, re-derive the theme set on every load: directories that have
  // appeared become themes, directories that are gone stop being themes. ON by default
  // for any session that has a scan root, because the entire point is that content added
  // on disk shows up without editing the session. Turning it off freezes the theme set
  // while leaving each existing theme's own `scan` live.
  bool theme_scan_root_auto = true;
  // theme name -> the tier layout of its resolved image pool, in pool order:
  // {source theme name, how many images that source contributed}. Tier 0 is always the
  // theme's own content; the rest are its ancestor chain, in the order inheritance folded
  // them in. Because the union APPENDS each ancestor's already-resolved pool, these spans
  // are contiguous, so the runtime can split image_path back into tiers by offset without
  // storing per-image tags.
  //
  // This is what lets selection honour the rotation weights INSIDE an inherited pool: a
  // flat union samples by raw file count, so a small folder inheriting a big one is
  // swamped regardless of how the weights are set. Not persisted -- it is re-derived on
  // every load, like the union itself.
  std::map<std::string, std::vector<std::pair<std::string, uint32_t>>> theme_tiers;
  // theme name -> how much media the theme had BEFORE inheritance was folded in. Not
  // persisted; recorded during load purely so the UI can show "304 -> 484 (+180)"
  // against a pool that has already been unioned.
  std::map<std::string, uint32_t> theme_own_count;
};

// Loads a session.json at `path`. `root` is the session's media root (the parent
// directory of `path`, per the path contract) -- pattern `file` references and theme
// `scan` directories are resolved relative to it. Throws std::runtime_error with a
// JSON-path-qualified message on any strict-mode violation (unknown key, bad/escaping
// path, missing pattern file, duplicate pattern name, both-or-neither oneof, wrong
// format/format_version). Does not run validate_session -- callers do that, matching
// load_proto's existing division of labor.
trance_pb::Session load_session_json(const std::string& path, const std::string& root,
                                      SessionJsonSidecar& sidecar);

// Writes `session` to `path` as session.json (spec sec 2/3/5): 2-space indent, keys in
// schema order, default-valued fields omitted. `root` is the media root pattern text is
// read/written relative to. `sidecar` supplies previously-known pattern file paths and
// scan directories; patterns with no sidecar entry get a fresh
// `patterns/<slug>.pattern` (slug rule, spec sec 5) and `sidecar` is updated in place so
// repeated saves in the same process are stable.
void save_session_json(const trance_pb::Session& session, const std::string& path,
                        const std::string& root, SessionJsonSidecar& sidecar);

// Loads system.json at `path`. Throws std::runtime_error on any strict-mode violation.
// Does not run validate_system -- callers do that.
trance_pb::System load_system_json(const std::string& path);

// Writes `system` to `path` as system.json (spec sec 2/6).
void save_system_json(const trance_pb::System& system, const std::string& path);

#endif
