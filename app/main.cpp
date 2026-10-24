#include "ast_dump.hpp"
#include "directives.hpp"
#include "fs_resolver.hpp"
#include "inspect.hpp"
#include "ls.hpp"
#include "version.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

// levelscript — the LevelScript CLI. A thin client of the public API: everything it
// does (compile, generate, read cells) a game can do the same way.

static void usage(char const* argv0) {
    std::cerr << "Usage: " << argv0
              << " [--seed N] [--entry name] [--param name=value ...] [--inspect] <file.lvs>\n"
              << "       " << argv0 << " --check [--seed N] [--param name=value ...] <file.lvs>\n"
              << "       " << argv0 << " --ast [--no-spans] <file.lvs>\n"
              << "       " << argv0 << " --version\n"
              << "  --seed   the seed (default: the file's // @seed=N, else the clock)\n"
              << "  --entry  the sequence to run (default: main)\n"
              << "  --check  verify the file's // @expect directives, run with its @seed (else 42)\n"
              << "           and @param values; prints each failure, exit 1 if any\n"
              << "  --ast    print the file's syntax tree; --no-spans leaves out source ranges\n";
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

// --check: the file's test directives (spec Appendix B). Runs `entry` with
// --seed, else @seed, else 42, and the @param values (--param overrides
// them), then verifies every @expect:
//   error                          the compile fails
//   stderr-contains TEXT           a compile error contains TEXT - or, with
//                                  run-ok, a warning of the compile or the run
//   run-ok                         the compile succeeds (also without @expect)
//   grid G count(CELL) OP N        cells of G holding CELL (a value, '.' or a number)
static int check_expectations(std::string const& source, std::string const& name,
                              std::optional<uint64_t> seed,
                              std::vector<std::pair<std::string, int>> const& cli_params,
                              std::string const& entry_name) {
    auto dirs = ls::read_directives(source, name, fs_resolver());
    std::cerr << dirs.warnings.format_all();

    bool want_error = false, run_ok = false;
    std::vector<std::string> contains, counts;
    for (auto const& e : dirs.expects()) {
        if (e == "error")                               want_error = true;
        else if (e == "run-ok")                         run_ok = true;
        else if (e.rfind("stderr-contains ", 0) == 0)   contains.push_back(e.substr(16));
        else                                            counts.push_back(e);
    }
    if (!contains.empty() && !run_ok) want_error = true;

    std::string warnings;
    auto gen = ls::generator::compile(source, name, fs_resolver(),
                                      [&](std::string const& w) { warnings += w + "\n"; });
    std::vector<std::string> failures;
    if (want_error) {
        if (gen) failures.push_back("expected a compile error, got none");
        for (auto const& c : contains)
            if (gen.error().find(c) == std::string::npos)
                failures.push_back("expected an error containing '" + c + "'");
    } else if (!gen) {
        failures.push_back("expected a clean compile, got:\n" + gen.error());
    } else if (int entry = gen.sequence(entry_name); entry < 0) {
        if (!counts.empty()) failures.push_back("no sequence '" + entry_name + "' to run");
    } else {
        auto params = dirs.params();
        for (auto const& p : cli_params) params.push_back(p);   // later ones win
        ls::level lv = gen.generate(entry, seed.value_or(dirs.seed().value_or(42)), params);
        for (auto const& c : contains)
            if (warnings.find(c) == std::string::npos)
                failures.push_back("expected a warning containing '" + c + "'");
        for (auto const& e : counts) {
            std::istringstream in(e);
            std::string kw, layer, call, op;
            long long n = 0;
            in >> kw >> layer >> call >> op >> n;
            std::string cell = call.substr(6, call.size() - 7);   // count(CELL)
            ls::grid g = lv[layer];
            if (g.name().empty()) {
                failures.push_back("'" + e + "': no grid '" + layer + "'");
                continue;
            }
            long long got = 0;
            for (int y = 0; y < lv.height(); ++y)
                for (int x = 0; x < lv.width(); ++x) {
                    if (cell == ".")
                        got += g.is_empty(x, y);
                    else if (g.is_number())
                        got += !g.is_empty(x, y) && std::to_string(g.at(x, y)) == cell;
                    else
                        got += g.valueName(g.at(x, y)) == cell;
                }
            bool holds = op == "==" ? got == n : op == "!=" ? got != n : op == "<" ? got < n
                       : op == "<=" ? got <= n : op == ">" ? got > n : got >= n;
            if (!holds) failures.push_back("'" + e + "': got " + std::to_string(got));
        }
    }
    for (auto const& f : failures) std::cout << name << ": FAIL " << f << '\n';
    return failures.empty() ? 0 : 1;
}

int main(int argc, char* argv[]) {
    std::optional<uint64_t> seed;
    std::string path;
    std::string entry_name = "main";   // a tool convention, not the language's
    std::vector<std::pair<std::string, int>> params;
    bool inspect = false, check = false;
    bool ast = false, spans = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version") {
            std::cout << "levelscript " << LS_VERSION_STRING << '\n';
            return 0;
        } else if (arg == "--inspect") {
            inspect = true;
        } else if (arg == "--check") {
            check = true;
        } else if (arg == "--ast") {
            ast = true;
        } else if (arg == "--no-spans") {
            spans = false;
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

    if (ast) {   // this file only, as parsed; parse errors on stderr
        ls::diagnostics diags;
        std::cout << ls::dump_ast(source, name, spans, diags);
        std::cerr << diags.format_all();
        return diags.has_errors() ? 1 : 0;
    }

    if (inspect) {   // editor tooling: JSON report, exit 0 (diagnostics inside)
        std::cout << ls::inspect_json(source, name, fs_resolver()) << '\n';
        return 0;
    }

    if (check) return check_expectations(source, name, seed, params, entry_name);

    auto gen = ls::generator::compile(source, name, fs_resolver());
    if (!gen) {
        std::cerr << gen.error();
        return 1;
    }

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

    // No --seed: the root file's @seed directive, else the clock (Appendix B)
    auto dirs = ls::read_directives(source, name, fs_resolver());
    std::cerr << dirs.warnings.format_all();
    if (!seed) seed = dirs.seed();
    uint64_t s = seed.value_or((uint64_t)
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    print_level(gen.generate(entry, s, params), std::cout);
    return 0;
}
