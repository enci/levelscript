#pragma once
#include "diagnostic.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace ls {

enum class token_type {
    ident, integer, string, newline, end, bad,
    variable,   // '?name', a pattern variable (section 5.11); text keeps the '?'
    // keywords (spec section 2.4); contextual names such as `symmetry` or
    // `horizontal` lex as ident and are matched by text in the parser
    kw_tag, kw_layers, kw_grid, kw_of, kw_number, kw_rule, kw_sequence,
    kw_all, kw_any, kw_ordered, kw_weight,
    kw_once, kw_scatter, kw_everywhere, kw_grow, kw_settle,   // modes (section 6)
    kw_params, kw_where, kw_when, kw_use,
    // punctuation
    lbrace, rbrace, lbracket, rbracket, lparen, rparen,
    comma, colon, equals, arrow, star, dot,
    percent,   // '%' closing a scatter percentage (section 6)
    // expression operators (section 5.8)
    plus, minus, slash, pipe, bang,
    eq_eq, bang_eq, lt, le, gt, ge, amp_amp, pipe_pipe,
};

// Reserved words (spec section 2.4) - the kw_* block above.
inline bool is_keyword(token_type t) {
    return t >= token_type::kw_tag && t <= token_type::kw_use;
}

struct token {
    token_type  type{token_type::end};
    std::string text;
    long long   int_val{0};
    int         line{1}, col{1};

    bool is(token_type t) const { return type == t; }
};

// Tokenize the whole source. Newlines are tokens (row separators in pattern
// grids; the parser treats them as whitespace everywhere else); lex errors are reported and yield a `bad` token.
// The last token is always `end`.
std::vector<token> lex(std::string_view source, std::string_view file,
                       diagnostics& diags);

}  // namespace ls
