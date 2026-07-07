#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include <optional>
#include <string_view>

namespace ls {

// Lex + parse a whole source file. Returns the AST when it is structurally
// usable; diagnostics carry the errors either way.
std::optional<ast_file> parse(std::string_view source, std::string_view file,
                              diagnostics& diags);

}  // namespace ls
