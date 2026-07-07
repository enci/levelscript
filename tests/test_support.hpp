#pragma once
#include "diagnostic.hpp"
#include "ls.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <string>

namespace ts {

// Parse + analyze a source fragment, keeping the diagnostics inspectable.
// The one test harness (the MGSL repo grew four divergent copies of this).
struct compile_result {
    ls::diagnostics diags;
    ls::compiled    prog;
    bool            ok{false};

    explicit compile_result(std::string const& src) {
        auto ast = ls::parse(src, "test", diags);
        if (ast && !diags.has_errors())
            ok = ls::analyze(*ast, prog, diags, "test");
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

}  // namespace ts
