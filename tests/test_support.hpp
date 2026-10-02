#pragma once
#include "diagnostic.hpp"
#include "ls.hpp"
#include "modules.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <string>

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

}  // namespace ts
