#ifndef TRANCE_SRC_COMMON_SESSION_LEGACY_H
#define TRANCE_SRC_COMMON_SESSION_LEGACY_H
#include <string>

namespace trance_pb
{
  class Session;
  class System;
}

// Legacy textproto readers (the pre-JSON .session / system.cfg formats). Shared by
// trance_convert (the one-shot CLI converter) and session.cpp's convert_legacy_session,
// which is what auto-migrates a .session path (including a sibling ./default.session)
// into JSON on load. Playback itself stays JSON-only -- everything downstream of
// load_session only ever sees the converted JSON.
// Both throw std::runtime_error on a missing file or parse failure; neither
// validates -- callers run validate_session/validate_system, matching the spec
// pipeline (docs/session-json-format.md sec 7).
trance_pb::Session load_legacy_session(const std::string& path);
trance_pb::System load_legacy_system(const std::string& path);

#endif
