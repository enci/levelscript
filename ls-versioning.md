# LevelScript: Semantic Versioning

## Scheme

Version is `MAJOR.MINOR.PATCH`, kept in lockstep with the spec:

- **MAJOR.MINOR** = language spec version (spec v0.7 → `0.7.x`).
- **PATCH** = implementation-only fixes that do not change the language or the public API.

While MAJOR is `0`, a MINOR bump may break `.ls` files or the embedding API. Go to `1.0.0` when the language is declared stable.

### When to bump

| Change | Bump |
|---|---|
| Spec delta applied (syntax/semantics change) | MINOR, reset PATCH |
| Public C++ or Wren API change | MINOR, reset PATCH |
| Bug fix, perf, internal refactor, no spec/API change | PATCH |

The spec header (`spec.md`) must state the same MAJOR.MINOR as the code.

## Tasks

### 1. Add `version.hpp`

Create `version.hpp` next to the main public header:

```cpp
#pragma once

#define LS_VERSION_MAJOR 0
#define LS_VERSION_MINOR 7
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
```

The three `#define` lines are the single source of truth. Nothing else hardcodes the version.

### 2. Include it from the main header

Add `#include "version.hpp"` to the main public header, near the top.

### 3. `lsc --version`

Make `lsc --version` print `lsc ` + `LS_VERSION_STRING` and exit 0.

### 4. Wren binding

Expose a static getter `LsGenerator.version` that returns `LS_VERSION_STRING`.

### 5. Installer (optional)

In the Inno Setup script, replace any hardcoded `AppVersion` with a value read from `version.hpp`, or with a value passed in by the build script after parsing the three `#define` lines.

## Verify

- Grep the repo for the old hardcoded version strings (`0.7`, `v0.7`, `MGSL`). The only matches left should be in `version.hpp`, `spec.md`, and changelog/history text.
- `lsc --version` prints `lsc 0.7.0`.
- A Wren smoke test prints `LsGenerator.version`.
