#include "debug_ui.hpp"
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

// lsd -- the LevelScript debugger: an interactive ImGui frontend over the
// public generation API (compile once, step application by application).

static void usage(char const* argv0) {
    std::cerr << "Usage: " << argv0 << " [--seed N] <file.ls>\n";
}

int main(int argc, char* argv[]) {
    std::optional<uint64_t> seed;
    std::string path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            seed = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            path = arg;
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            usage(argv[0]);
            return 1;
        }
    }
    if (path.empty()) {
        usage(argv[0]);
        return 1;
    }

    return ls::run_debug_ui(path, seed);
}
