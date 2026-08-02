#ifndef TRANCE_SRC_COMMON_SESSION_LEGACY_H
#define TRANCE_SRC_COMMON_SESSION_LEGACY_H
#include <string>

namespace trance_pb
{
  class Session;
  class System;
}

// Legacy textproto readers (the pre-JSON .session / system.cfg formats). Shared by
// session.cpp's convert_legacy_session and main.cpp's system.cfg startup migration,
// which is what auto-migrates a .session path (including a sibling ./default.session)
// into JSON on load. Playback itself stays JSON-only -- everything downstream of
// load_session only ever sees the converted JSON.
//
// Scope, permanently: these read UPSTREAM-ERA files only. Parsing happens against the
// frozen `legacy.proto` descriptor (the fork-point schema, commit 0e97381), not the
// live `trance.proto`, and the result is translated into the current model explicitly
// in session_legacy.cpp. Fields this fork added -- entrainment, custom_visual_pattern,
// theme audio_path, OPENXR -- are outside the importer's contract by construction and
// a file containing them is rejected, not half-read. docs/session-json-format.md sec 7.
// Both throw std::runtime_error on a missing file or parse failure; neither
// validates -- callers run validate_session/validate_system, matching the spec
// pipeline (docs/session-json-format.md sec 7).
trance_pb::Session load_legacy_session(const std::string& path);
trance_pb::System load_legacy_system(const std::string& path);

#endif
