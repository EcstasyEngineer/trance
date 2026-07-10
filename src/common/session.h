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

void search_resources(trance_pb::Session& session, const std::string& root);
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
// lists; the sidecar-less overload is for read-only callers (playback).
trance_pb::Session load_session(const std::string& path);
trance_pb::Session load_session(const std::string& path, SessionJsonSidecar& sidecar);
void save_session(const trance_pb::Session& session, const std::string& path);
void save_session(const trance_pb::Session& session, const std::string& path,
                   SessionJsonSidecar& sidecar);
trance_pb::Session get_default_session();
void validate_session(trance_pb::Session& session);

#endif