#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include "ls.hpp"   // module_source, resolver
#include <string>
#include <string_view>
#include <vector>

namespace ls {

// One compile's module closure (spec section 2.6), loaded and merged.
//
// Module ids are load order (what every source_loc::mod refers to); `order`
// is the canonical order, a depth-first post-order walk from the root (root
// last). `merged` holds every module's declarations in canonical order, so
// the analyzer sees one file whose layer, param and sequence order is already
// the canonical one.
struct module_closure {
    std::vector<std::string>       names;   // canonical name, by module id
    std::vector<std::vector<char>> sees;    // [from][decl]: itself + direct uses
    std::vector<int>               order;   // module ids, canonical order
    ast_file                       merged;
    int                            root{0};
};

// Load the root and everything it uses, through `resolve` (may be empty: then
// every `use` is unresolved, section 7.3, check 9). Reports checks 9, 40 and 41.
module_closure load_closure(std::string_view root_source, std::string const& root_name,
                            resolver const& resolve, diagnostics& diags);

// The closure of a lone file - no `use` resolution; for tests and tools that
// analyze one module on its own.
module_closure single_module(ast_file ast, std::string const& name);

}  // namespace ls
