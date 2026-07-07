#include "lexer.hpp"
#include <unordered_map>

namespace ls {

static bool ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool ident_cont(char c) {
    return ident_start(c) || (c >= '0' && c <= '9');
}
static bool digit(char c) { return c >= '0' && c <= '9'; }

static token_type keyword_or_ident(std::string_view text) {
    static const std::unordered_map<std::string_view, token_type> keywords = {
        {"tag", token_type::kw_tag},       {"layers", token_type::kw_layers},
        {"grid", token_type::kw_grid},     {"of", token_type::kw_of},
        {"number", token_type::kw_number}, {"rule", token_type::kw_rule},
        {"program", token_type::kw_program},
        {"one", token_type::kw_one},       {"all", token_type::kw_all},
        {"some", token_type::kw_some},     {"max", token_type::kw_max},
        {"any", token_type::kw_any},
        {"symmetry", token_type::kw_symmetry},
        {"rotation", token_type::kw_rotation},
        {"weight", token_type::kw_weight},
        {"horizontal", token_type::kw_horizontal},
        {"vertical", token_type::kw_vertical},
        {"none", token_type::kw_none},
    };
    auto it = keywords.find(text);
    return it == keywords.end() ? token_type::ident : it->second;
}

std::vector<token> lex(std::string_view src, std::string_view file,
                       diagnostics& diags) {
    std::vector<token> out;
    int line = 1, col = 1;
    size_t i = 0;
    size_t n = src.size();

    auto push = [&](token_type t, std::string text, int l, int c, long long v = 0) {
        out.push_back({t, std::move(text), v, l, c});
    };

    while (i < n) {
        char c = src[i];

        if (c == ' ' || c == '\t' || c == '\r') { ++i; ++col; continue; }

        if (c == '\n') {
            push(token_type::newline, "\n", line, col);
            ++i; ++line; col = 1;
            continue;
        }

        if (c == '/' && i + 1 < n && src[i + 1] == '/') {   // // comment
            while (i < n && src[i] != '\n') { ++i; ++col; }
            continue;
        }

        int tl = line, tc = col;

        if (ident_start(c)) {
            size_t s = i;
            while (i < n && ident_cont(src[i])) { ++i; ++col; }
            std::string text(src.substr(s, i - s));
            token_type tt = keyword_or_ident(text);   // before the move below
            push(tt, std::move(text), tl, tc);
            continue;
        }

        if (digit(c)) {
            long long v = 0;
            size_t s = i;
            while (i < n && digit(src[i])) { v = v * 10 + (src[i] - '0'); ++i; ++col; }
            push(token_type::integer, std::string(src.substr(s, i - s)), tl, tc, v);
            continue;
        }

        if (c == '=') {
            if (i + 1 < n && src[i + 1] == '>') {
                push(token_type::arrow, "=>", tl, tc);
                i += 2; col += 2;
            } else {
                push(token_type::equals, "=", tl, tc);
                ++i; ++col;
            }
            continue;
        }

        token_type t = token_type::bad;
        switch (c) {
        case '{': t = token_type::lbrace;   break;
        case '}': t = token_type::rbrace;   break;
        case '[': t = token_type::lbracket; break;
        case ']': t = token_type::rbracket; break;
        case '(': t = token_type::lparen;   break;
        case ')': t = token_type::rparen;   break;
        case ',': t = token_type::comma;    break;
        case ':': t = token_type::colon;    break;
        case '*': t = token_type::star;     break;
        case '.': t = token_type::dot;      break;
        default: break;
        }
        if (t == token_type::bad)
            diags.error(file, tl, tc,
                        "unexpected character '" + std::string(1, c) + "'");
        push(t, std::string(1, c), tl, tc);
        ++i; ++col;
    }

    push(token_type::end, "", line, col);
    return out;
}

}  // namespace ls
