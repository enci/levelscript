#pragma once
#include "diagnostic.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace ls {

enum class token_type {
    ident, integer, newline, end, bad,
    // keywords
    kw_tag, kw_layers, kw_grid, kw_of, kw_number, kw_rule, kw_program,
    kw_one, kw_all, kw_some, kw_max, kw_any,
    kw_symmetry, kw_rotation, kw_weight,
    kw_horizontal, kw_vertical, kw_none,
    // punctuation
    lbrace, rbrace, lbracket, rbracket, lparen, rparen,
    comma, colon, equals, arrow, star, dot,
};

struct token {
    token_type  type{token_type::end};
    std::string text;
    long long   int_val{0};
    int         line{1}, col{1};

    bool is(token_type t) const { return type == t; }
};

// Tokenize the whole source. Newlines are tokens (rows in pattern bodies,
// separators elsewhere); lex errors are reported and yield a `bad` token.
// The last token is always `end`.
std::vector<token> lex(std::string_view source, std::string_view file,
                       diagnostics& diags);

}  // namespace ls
