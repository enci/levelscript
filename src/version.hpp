#pragma once

// LevelScript version, MAJOR.MINOR.PATCH (see ls-versioning.md).
// MAJOR.MINOR is the language spec version; PATCH is implementation-only.
// These three defines are the single source of truth; the installer script parses them.

#define LS_VERSION_MAJOR 0
#define LS_VERSION_MINOR 8
#define LS_VERSION_PATCH 0

#define LS_STR_(x) #x
#define LS_STR(x)  LS_STR_(x)
#define LS_VERSION_STRING \
    LS_STR(LS_VERSION_MAJOR) "." LS_STR(LS_VERSION_MINOR) "." LS_STR(LS_VERSION_PATCH)

// Comparable integer, e.g. #if LS_VERSION >= 800. Assumes MINOR, PATCH < 100.
#define LS_VERSION (LS_VERSION_MAJOR * 10000 + LS_VERSION_MINOR * 100 + LS_VERSION_PATCH)

namespace ls {
    struct Version { int major, minor, patch; };
    inline constexpr Version version{ LS_VERSION_MAJOR, LS_VERSION_MINOR, LS_VERSION_PATCH };
}
