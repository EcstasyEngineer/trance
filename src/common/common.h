#ifndef TRANCE_SRC_COMMON_COMMON_H
#define TRANCE_SRC_COMMON_COMMON_H
#include <cstddef>
#include <string>

// The no-arg default session. Named default.json (not default.session.json): it is
// auto-created on first run, the same bootstrap role default.session played in the
// original trance.exe. A legacy ./default.session sibling needs no special case here
// -- load_session auto-converts it to this path (session.cpp's convert_legacy_session).
static const std::string DEFAULT_SESSION_PATH = "default.json";
static const std::string SYSTEM_CONFIG_PATH = "system.json";
static const std::size_t MAXIMUM_STACK = 256;

#endif