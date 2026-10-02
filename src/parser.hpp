#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include <optional>
#include <string_view>

namespace ls {

// Lex + parse a whole source file (one module). Returns the AST when it is
// structurally usable; diagnostics carry the errors either way. `mod` is the
// module id stamped into every source_loc (section 2.6; 0 for a lone file).
std::optional<ast_file> parse(std::string_view source, std::string_view file,
                              diagnostics& diags, int mod = 0);

}  // namespace ls
