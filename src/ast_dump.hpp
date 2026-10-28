#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include <string>
#include <string_view>

namespace ls {

// The AST of one parsed module as indented S-expressions, one declaration,
// statement or pattern row per line. Expressions print inline, operators by
// their symbol: `(+ 1 (* x 2))`. With `spans`, every node but a leaf token
// ends with its source range, `@line:col-end_line:end_col`.
std::string dump_ast(ast_file const& ast, bool spans = true);

// Parse `source` alone (its `use`s are not followed) and dump its AST; parse
// errors land in `diags`, and the dump shows what the parser recovered.
std::string dump_ast(std::string_view source, std::string_view name, bool spans,
                     diagnostics& diags);

// An expression as source text, infix, parenthesized only where precedence
// needs it: `?a != ?b && k > 0`.
std::string expr_source(expr const& e);

// A graph block as one line of source text, items separated by commas:
// `plan { ?hub[Hall], ?hub -- ?a }`.
std::string graph_block_source(graph_block const& b);

}  // namespace ls
