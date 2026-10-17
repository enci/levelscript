#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ls {

// Launch the interactive ImGui debugger for the script at `path`, or with no
// script when `path` is empty (one is opened from the window).
// Blocks until the user quits; returns the process exit code (nonzero when
// the initial load of `path` fails — errors go to stderr before any window opens).
// If fixed_seed has a value, Reset always reuses it; otherwise each Reset
// picks a fresh time-based seed (unless the user locks the current one).
int run_debug_ui(std::string const& path, std::optional<uint64_t> fixed_seed,
                 std::string const& entry = "main");

}  // namespace ls
