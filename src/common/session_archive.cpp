#include <common/session_archive.h>
#include <common/session.h>
#include <common/session_json.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <miniz/miniz.h>
#pragma warning(pop)

namespace
{
  // Collects every root-relative path the session references: theme media, custom
  // pattern source files (via the sidecar -- trance_pb doesn't carry the file path, only
  // the inlined source_text), and playlist audio_event paths. A std::set both dedupes
  // (multiple custom_visual_pattern entries may share one file; multiple audio_events may
  // reuse one clip) and gives a stable, deterministic archive member order.
  std::set<std::string> collect_referenced_paths(const trance_pb::Session& session,
                                                  const SessionJsonSidecar& sidecar)
  {
    std::set<std::string> paths;

    for (const auto& theme_pair : session.theme_map()) {
      const auto& theme = theme_pair.second;
      for (const auto& p : theme.image_path()) {
        paths.insert(p);
      }
      for (const auto& p : theme.animation_path()) {
        paths.insert(p);
      }
      for (const auto& p : theme.font_path()) {
        paths.insert(p);
      }
    }

    for (const auto& pattern_pair : sidecar.pattern_file) {
      paths.insert(pattern_pair.second);
    }

    for (const auto& item_pair : session.playlist()) {
      for (const auto& event : item_pair.second.audio_event()) {
        if (!event.path().empty()) {
          paths.insert(event.path());
        }
      }
    }

    return paths;
  }
}  // namespace

bool export_session_archive(const std::string& session_path, const std::string& archive_path,
                             std::string& error)
{
  SessionJsonSidecar sidecar;
  trance_pb::Session session;
  try {
    session = load_session(session_path, sidecar);
  } catch (const std::runtime_error& e) {
    error = std::string("failed to load session '") + session_path + "': " + e.what();
    return false;
  }

  auto root = std::filesystem::path{session_path}.parent_path();
  auto paths = collect_referenced_paths(session, sidecar);

  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, archive_path.c_str(), 0)) {
    error = "failed to open archive '" + archive_path + "' for writing: " +
        mz_zip_get_error_string(mz_zip_get_last_error(&zip));
    return false;
  }

  // Session JSON goes at the archive root, per docs/session-json-format.md sec 9: "session
  // file stored as session.json at zip root". We re-save through save_session_json (rather
  // than copying session_path verbatim) so the archived JSON reflects the same in-memory
  // session that was validated on load and whose referenced files we're about to walk --
  // e.g. validate_session()'s repair/prune passes -- and so pattern text lands inlined-by-
  // reference the same way a fresh save always does. `root`/`sidecar` here only affect
  // *where the saver looks things up*, not what's written to session.json itself.
  bool ok = true;
  try {
    save_session_json(session, (root / "session.json").string(), root.string(), sidecar);
  } catch (const std::runtime_error& e) {
    // save_session_json shouldn't normally throw on a session we just successfully loaded
    // and validated, but guard anyway rather than leaving a half-written temp file behind.
    error = std::string("failed to stage session.json for archive: ") + e.what();
    mz_zip_writer_end(&zip);
    return false;
  }
  auto staged_session_json = root / "session.json";
  if (!mz_zip_writer_add_file(&zip, "session.json", staged_session_json.string().c_str(), nullptr,
                               0, MZ_NO_COMPRESSION)) {
    error = "failed to add session.json to archive: " +
        std::string(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    ok = false;
  }
  std::error_code remove_ec;
  std::filesystem::remove(staged_session_json, remove_ec);

  std::size_t index = 0;
  for (const auto& rel_path : paths) {
    ++index;
    if (!ok) {
      break;
    }
    auto full_path = root / rel_path;
    if (!std::filesystem::exists(full_path)) {
      std::cout << "WARNING: referenced file missing, skipping: " << rel_path << std::endl;
      continue;
    }
    std::cout << "[" << index << "/" << paths.size() << "] " << rel_path << std::endl;
    if (!mz_zip_writer_add_file(&zip, rel_path.c_str(), full_path.string().c_str(), nullptr, 0,
                                 MZ_NO_COMPRESSION)) {
      error = "failed to add '" + rel_path +
          "' to archive: " + mz_zip_get_error_string(mz_zip_get_last_error(&zip));
      ok = false;
    }
  }

  if (ok && !mz_zip_writer_finalize_archive(&zip)) {
    error = "failed to finalize archive: " +
        std::string(mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    ok = false;
  }
  mz_zip_writer_end(&zip);
  if (!ok) {
    std::error_code ec;
    std::filesystem::remove(archive_path, ec);
  }
  return ok;
}
