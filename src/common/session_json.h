#ifndef TRANCE_SRC_COMMON_SESSION_JSON_H
#define TRANCE_SRC_COMMON_SESSION_JSON_H
#include <map>
#include <string>
#include <utility>

// JSON <-> trance_pb mapping for session.json / system.json, per the normative spec
// docs/session-json-format.md. The in-memory model stays trance_pb::Session /
// trance_pb::System this wave (see spec "Scope of this wave"); only the on-disk format
// is JSON. Everything here is exercised through session.{h,cpp}'s load_session /
// load_system / save_session / save_system -- callers outside session.cpp shouldn't
// need to include this header directly.

namespace trance_pb
{
  class Session;
  class System;
}

// Per-session sidecar (spec sec 5): information the loader recovers from the JSON that
// has no home in trance_pb (which is frozen this wave), needed so a subsequent save
// writes pattern text back to its original file and re-emits theme `scan` keys instead
// of freezing their expansion into explicit lists.
struct SessionJsonSidecar
{
  // {program name, pattern name} -> pattern file path (root-relative).
  std::map<std::pair<std::string, std::string>, std::string> pattern_file;
  // theme name -> scan directory (root-relative), when the theme used `scan`.
  std::map<std::string, std::string> theme_scan;
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
