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
        {"grid", token_type::kw_grid},     {"graph", token_type::kw_graph},
        {"of", token_type::kw_of},
        {"number", token_type::kw_number}, {"rule", token_type::kw_rule},
        {"sequence", token_type::kw_sequence},
        {"all", token_type::kw_all},       {"any", token_type::kw_any},
        {"ordered", token_type::kw_ordered},
        {"once", token_type::kw_once},
        {"scatter", token_type::kw_scatter},
        {"everywhere", token_type::kw_everywhere},
        {"grow", token_type::kw_grow},
        {"settle", token_type::kw_settle},
        {"params", token_type::kw_params},
        {"where", token_type::kw_where},
        {"when", token_type::kw_when},
        {"use", token_type::kw_use},
        {"weight", token_type::kw_weight},
        {"is", token_type::kw_is},
    };
    auto it = keywords.find(text);
    return it == keywords.end() ? token_type::ident : it->second;
}

std::vector<token> lex(std::string_view src, std::string_view file,
                       diagnostics& diags, std::vector<directive_comment>* directives) {
    std::vector<token> out;
    int line = 1, col = 1;
    size_t i = 0;
    size_t n = src.size();
    int last_code_line = 0;   // 0: no code yet
    size_t pending = directives ? directives->size() : 0;   // own-line comments awaiting code

    // A string token's text drops its two quotes; every other text is as written.
    auto push = [&](token_type t, std::string text, int l, int c, long long v = 0) {
        int end = c + (int)text.size() + (t == token_type::string ? 2 : 0);
        out.push_back({t, std::move(text), v, l, c, end});
        if (t == token_type::newline || t == token_type::end) return;
        last_code_line = l;
        if (directives) {
            for (; pending < directives->size(); ++pending) (*directives)[pending].code_line = l;
        }
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
            size_t s = i;
            int    c0 = col;
            while (i < n && src[i] != '\n') { ++i; ++col; }
            std::string_view text = src.substr(s + 2, i - s - 2);
            if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
            size_t first = text.find_first_not_of(" \t");
            if (directives && first != std::string_view::npos && text[first] == '@') {
                bool trailing = last_code_line == line;
                directives->push_back({line, c0, c0 + 2 + (int)text.size(), std::string(text),
                                       trailing, last_code_line == 0, trailing ? line : 0, 0});
                if (trailing) pending = directives->size();
            }
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

        // VARIABLE: '?' and the name, no whitespace between.
        if (c == '?') {
            if (i + 1 >= n || !ident_start(src[i + 1])) {
                diags.error(file, tl, tc, tl, tc + 1, "expected a variable name right after '?'");
                push(token_type::bad, "?", tl, tc);
                ++i; ++col;
                continue;
            }
            size_t s = i++;
            ++col;
            while (i < n && ident_cont(src[i])) { ++i; ++col; }
            push(token_type::variable, std::string(src.substr(s, i - s)), tl, tc);
            continue;
        }

        // STRING: no escapes, single line - only the path of a `use`.
        // The token's text is the contents, without the quotes.
        if (c == '"') {
            size_t s = i + 1, j = s;
            while (j < n && src[j] != '"' && src[j] != '\n') ++j;
            if (j >= n || src[j] != '"') {
                diags.error(file, tl, tc, tl, tc + (int)(j - i), "unterminated string");
                push(token_type::bad, std::string(src.substr(i, j - i)), tl, tc);
                col += (int)(j - i);
                i = j;
                continue;
            }
            push(token_type::string, std::string(src.substr(s, j - s)), tl, tc);
            col += (int)(j - i + 1);
            i = j + 1;
            continue;
        }

        if (digit(c)) {
            long long v = 0;
            size_t s = i;
            while (i < n && digit(src[i])) { v = v * 10 + (src[i] - '0'); ++i; ++col; }
            push(token_type::integer, std::string(src.substr(s, i - s)), tl, tc, v);
            continue;
        }

        // two-character operators first
        auto two = [&](char c2, token_type t2) {
            if (i + 1 < n && src[i + 1] == c2) {
                push(t2, std::string{c, c2}, tl, tc);
                i += 2; col += 2;
                return true;
            }
            return false;
        };
        if (c == '=') {
            if (two('>', token_type::arrow)) continue;
            if (two('=', token_type::eq_eq)) continue;
            push(token_type::equals, "=", tl, tc);
            ++i; ++col;
            continue;
        }
        if (c == '!' && two('=', token_type::bang_eq)) continue;
        if (c == '<' && two('=', token_type::le)) continue;
        if (c == '>' && two('=', token_type::ge)) continue;
        if (c == '&' && two('&', token_type::amp_amp)) continue;
        if (c == '|' && two('|', token_type::pipe_pipe)) continue;

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
        case '%': t = token_type::percent;  break;
        case '+': t = token_type::plus;     break;
        case '-': t = token_type::minus;    break;
        case '/': t = token_type::slash;    break;   // '//' comments handled above
        case '|': t = token_type::pipe;     break;
        case '!': t = token_type::bang;     break;
        case '<': t = token_type::lt;       break;
        case '>': t = token_type::gt;       break;
        default: break;
        }
        if (t == token_type::bad)
            diags.error(file, tl, tc, tl, tc + 1,
                        "unexpected character '" + std::string(1, c) + "'");
        push(t, std::string(1, c), tl, tc);
        ++i; ++col;
    }

    push(token_type::end, "", line, col);
    return out;
}

}  // namespace ls
