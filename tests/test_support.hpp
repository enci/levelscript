#pragma once
#include "diagnostic.hpp"
#include "ls.hpp"
#include "modules.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <string>
#include <vector>

namespace ts {

// Parse + analyze a source fragment as a lone module, keeping the
// diagnostics inspectable. The one test harness.
struct compile_result {
    ls::diagnostics diags;
    ls::compiled    prog;
    bool            ok{false};

    explicit compile_result(std::string const& src) {
        auto ast = ls::parse(src, "test", diags);
        if (ast && !diags.has_errors())
            ok = ls::analyze(ls::single_module(std::move(*ast), "test"), prog, diags);
    }

    bool has_error(std::string const& needle) const {
        for (auto const& d : diags.all)
            if (d.is_error && d.message.find(needle) != std::string::npos)
                return true;
        return false;
    }
};

inline ls::generator make(std::string const& src) {
    return ls::generator::compile(src, "test");
}


// ── fixtures ─────────────────────────────────────────────────────────────────

// One inline rule the size of the whole grid, which fits only at (0, 0), so
// `once` writes exactly `rows` into `grid`. Each row is space-separated cells.
inline std::string stamp(std::string const& grid, std::vector<std::string> const& rows) {
    std::string any, cells;
    for (size_t r = 0; r < rows.size(); ++r) {
        std::string stars;
        for (size_t i = 0, n = 0; i <= rows[r].size(); ++i)
            if (i == rows[r].size() || rows[r][i] == ' ') {
                if (n > 0) stars += stars.empty() ? "*" : " *";
                n = 0;
            } else {
                ++n;
            }
        any   += (r ? ", " : "") + stars;
        cells += (r ? ", " : "") + rows[r];
    }
    return "once { " + grid + "[" + any + "] => " + grid + "[" + cells + "] }";
}

inline int width_of(std::vector<std::string> const& rows) {
    int w = 1;
    for (char c : rows[0]) w += c == ' ';
    return w;
}

// A program with `decls`, whose main resizes to fit `rows` and stamps them
// into `grid`, then runs `after`.
inline std::string stamped(std::string const& decls, std::string const& grid,
                           std::vector<std::string> const& rows, std::string const& after = "") {
    return decls + "\nsequence main {\n    resize(" + std::to_string(width_of(rows)) + ", " +
           std::to_string(rows.size()) + ")\n    " + stamp(grid, rows) + "\n    " + after +
           "\n}\n";
}

// One character per cell of `grid`, rows joined by '/': '.' for an empty
// cell, else the first letter of its value name (number grids: the number).
inline std::string show(ls::level const& lv, std::string const& name) {
    auto g = lv[name];
    std::string out;
    for (int y = 0; y < lv.height(); ++y) {
        if (y) out += '/';
        for (int x = 0; x < lv.width(); ++x) {
            if (g.is_empty(x, y)) out += '.';
            else if (g.is_number()) out += std::to_string(g.at(x, y));
            else out += g.valueName(g.at(x, y))[0];
        }
    }
    return out;
}

}  // namespace ts
