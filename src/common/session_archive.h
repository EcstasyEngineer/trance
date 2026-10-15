#ifndef TRANCE_SRC_COMMON_SESSION_ARCHIVE_H
#define TRANCE_SRC_COMMON_SESSION_ARCHIVE_H
#include <string>

// SessionArchive: portable session bundles as a plain zip.
//
// Layout matches docs/session-json-format.md sec 9's "Archive wave" plan of record: the
// zip holds the session JSON at its root as "session.json", plus every file the session
// references (theme image/animation/font paths, custom_visual_pattern files, playlist
// audio_event paths) stored under their existing root-relative paths -- i.e. archiving is
// conceptually `zip -r` of the session root, per the sec 1 path contract (every reference
// is already root-relative, forward-slash, no "..").
//
// Import (unzip) is intentionally NOT implemented in C++. A `.trance` archive is a
// standard zip -- any zip tool (Explorer, Archive Manager, `unzip`, 7-Zip, ...) extracts
// it back into a valid session root in one step, because member names are exactly the
// root-relative paths load_session_json() already expects. That IS the import story: no
// bespoke importer to maintain, no format the player has to also know how to read, and the
// bundle stays inspectable/moddable by hand after extraction. If a GUI "import archive"
// affordance is ever wanted, it should shell out to extraction (or a vetted zip library's
// *reader* on the C++ side) rather than duplicating this writer in reverse.
//
// Compression: stored (MZ_NO_COMPRESSION), not deflated. Session media (images/animations/
// fonts/audio) is already compressed (png/webm/gif/ogg/...) so deflating it again mostly
// just burns CPU on multi-GB session roots for little size win; storing also matches the
// "zip -r of the root" mental model in the spec.
bool export_session_archive(const std::string& session_path, const std::string& archive_path,
                             std::string& error);

#endif
