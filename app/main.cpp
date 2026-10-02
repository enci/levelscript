#include "fs_resolver.hpp"
#include "inspect.hpp"
#include "ls.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

// lsc — the LevelScript CLI. A thin client of the public API: everything it
// does (compile, generate, read cells) a game can do the same way.

static void usage(char const* argv0) {
    std::cerr << "Usage: " << argv0
              << " [--seed N] [--entry name] [--param name=value ...] [--inspect] <file.ls>\n"
              << "  --entry  the sequence to run (default: main)\n";
}

static void print_level(ls::level const& lv, std::ostream& out) {
    for (int i = 0; i < lv.layer_count(); ++i) {
        out << "=== " << lv.layer_name(i) << " ===\n";
        ls::grid g = lv.layer(i);
        for (int y = 0; y < lv.height(); ++y) {
            for (int x = 0; x < lv.width(); ++x) {
                int v = g.at(x, y);
                if (v < 0)               out << '.';
                else if (g.is_number())  out << v;
                else                     out << g.valueName(v)[0];
                if (x + 1 < lv.width()) out << ' ';
            }
            out << '\n';
        }
        out << '\n';
    }
}

int main(int argc, char* argv[]) {
    std::optional<uint64_t> seed;
    std::string path;
    std::string entry_name = "main";   // a tool convention, not the language's (§6)
    std::vector<std::pair<std::string, int>> params;
    bool inspect = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--inspect") {
            inspect = true;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = (uint64_t)std::stoull(argv[++i]);
        } else if (arg == "--entry" && i + 1 < argc) {
            entry_name = argv[++i];
        } else if (arg == "--param" && i + 1 < argc) {
            std::string kv = argv[++i];
            auto eq = kv.find('=');
            if (eq == std::string::npos) {
                std::cerr << "Error: --param expects name=value, got '" << kv << "'\n";
                return 1;
            }
            params.emplace_back(kv.substr(0, eq), std::stoi(kv.substr(eq + 1)));
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
    if (path.empty()) { usage(argv[0]); return 1; }

    std::string name = canonical_module_name(path);
    std::string source;
    if (!read_text_file(name, source)) {
        std::cerr << "Error: cannot open '" << path << "'\n";
        return 1;
    }

    if (inspect) {   // editor tooling: JSON report, exit 0 (diagnostics inside)
        std::cout << ls::inspect_json(source, name, fs_resolver()) << '\n';
        return 0;
    }

    auto gen = ls::generator::compile(source, name, fs_resolver());
    if (!gen) {
        std::cerr << gen.error();
        return 1;
    }
    if (!gen.warnings().empty()) std::cerr << gen.warnings();

    int entry = gen.sequence(entry_name);
    if (entry < 0) {
        std::cerr << name << ": error: no sequence '" << entry_name << "' to run";
        if (gen.sequence_count() == 0) {
            std::cerr << " (the file declares no sequences)\n";
        } else {
            std::cerr << "; pick one with --entry:";
            for (int i = 0; i < gen.sequence_count(); ++i)
                std::cerr << (i ? ", " : " ") << gen.sequence_name(i);
            std::cerr << '\n';
        }
        return 1;
    }

    uint64_t s = seed.value_or((uint64_t)
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    print_level(gen.generate(entry, s, params), std::cout);
    return 0;
}
