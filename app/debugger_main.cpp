#include "debug_ui.hpp"
#include <cstdint>
#include <iostream>
#include <optional> 
#include <string>
#include "version.hpp"

// levelscript-debugger -- the LevelScript debugger: an interactive ImGui frontend over the
// public run API (compile once, step application by application).

static void usage(char const* argv0) {
    std::cerr << "Usage: " << argv0 << " [--seed N] [--entry name] <file.lvs>\n"
              << "  --entry  the sequence to run (default: main)\n";
}

int main(int argc, char* argv[]) {
    std::optional<uint64_t> seed;
    std::string path;
    std::string entry = "main";   // a tool convention (section 6), changeable in the UI

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version") {
            std::cout << "levelscript-debugger " << LS_VERSION_STRING << '\n';
            return 0;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--entry" && i + 1 < argc) {
            entry = argv[++i];
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

    return ls::run_debug_ui(path, seed, entry);
}
