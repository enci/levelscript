#include "directives.hpp"
#include <map>
#include <tuple>

namespace ls {

namespace {

bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool ident_cont(char c) { return ident_start(c) || digit(c); }

enum class value_kind { ident, integer, text };   // text: `@key <text>`, for the test keys

struct key_spec {
    std::string_view key;
    directive_target target;
    value_kind       value;
    char const*      target_name;   // in warnings
    char const*      plural;
};

key_spec const keys[] = {
    {"seed",    directive_target::file,      value_kind::integer, "the file", ""},
    {"name",    directive_target::tag_value, value_kind::ident,   "tag value", "tag values"},
    {"tile",    directive_target::tag_value, value_kind::integer, "tag value", "tag values"},
    {"tileset", directive_target::layer,     value_kind::ident,   "grid entry", "grid entries"},
    {"play",    directive_target::statement, value_kind::integer, "statement", "statements"},
    {"expect",  directive_target::file,      value_kind::text,    "the file", ""},
    {"param",   directive_target::file,      value_kind::text,    "the file", ""},
};

key_spec const* find_key(std::string_view k) {
    for (auto const& s : keys)
        if (s.key == k) return &s;
    return nullptr;
}

// One `@key=value` (or `@key <text>`) of a comment, as written.
struct raw_directive {
    std::string key, value;
    int  col, end_col;   // the whole directive
    bool text{false};
};

// directive_comment ::= '//' SPACE* (directive (SPACE+ directive)* | text_directive) SPACE*
// text_directive    ::= '@' IDENT SPACE+ TEXT       -- TEXT ends before a '//' note
// Empty when the comment does not have that shape.
std::optional<std::vector<raw_directive>> split(directive_comment const& c) {
    std::string_view t = c.text;
    int base = c.col + 2;   // column of t[0]
    std::vector<raw_directive> out;
    size_t i = 0, n = t.size();
    auto space = [&](size_t j) { return j < n && (t[j] == ' ' || t[j] == '\t'); };
    while (space(i)) ++i;
    while (i < n) {
        if (!out.empty()) {   // SPACE+ between directives
            if (!space(i)) return std::nullopt;
            while (space(i)) ++i;
            if (i == n) break;   // trailing SPACE*
        }
        size_t s = i;
        if (t[i] != '@') return std::nullopt;
        ++i;
        size_t k = i;
        if (i >= n || !ident_start(t[i])) return std::nullopt;
        while (i < n && ident_cont(t[i])) ++i;
        std::string key(t.substr(k, i - k));
        if (out.empty() && space(i)) {   // a text directive takes the whole comment
            while (space(i)) ++i;
            size_t v = i, e = t.find("//", i);
            if (e == std::string_view::npos) e = n;
            while (e > v && (t[e - 1] == ' ' || t[e - 1] == '\t')) --e;
            if (e == v) return std::nullopt;
            out.push_back({std::move(key), std::string(t.substr(v, e - v)),
                           base + (int)s, base + (int)e, true});
            return out;
        }
        if (i >= n || t[i] != '=') return std::nullopt;
        size_t v = ++i;
        if (i < n && ident_start(t[i]))
            while (i < n && ident_cont(t[i])) ++i;
        else
            while (i < n && digit(t[i])) ++i;
        if (i == v) return std::nullopt;
        if (i < n && !space(i)) return std::nullopt;   // `@tile=12x`
        out.push_back({std::move(key), std::string(t.substr(v, i - v)),
                       base + (int)s, base + (int)i});
    }
    return out;
}

// `@param` text: IDENT '=' '-'? INTEGER. The name and the number; empty when malformed.
std::optional<std::pair<std::string, std::string>> param_text(std::string const& t) {
    size_t i = 0, n = t.size();
    if (i >= n || !ident_start(t[i])) return std::nullopt;
    while (i < n && ident_cont(t[i])) ++i;
    std::string name = t.substr(0, i);
    if (i >= n || t[i] != '=') return std::nullopt;
    size_t v = ++i;
    if (i < n && t[i] == '-') ++i;
    size_t d = i;
    while (i < n && digit(t[i])) ++i;
    if (i == d || i != n || i - d > 9) return std::nullopt;   // fits an int
    return std::make_pair(name, t.substr(v));
}

// `@expect` text: what is wrong with it; "" when it is one of
//   error | run-ok | stderr-contains TEXT | grid IDENT count(CELL) OP INTEGER
// with CELL a value name, '.' or a number, and OP one of == != < <= > >=.
std::string expect_problem(std::string const& t) {
    if (t == "error" || t == "run-ok") return "";
    if (t.rfind("stderr-contains ", 0) == 0) return "";
    if (t.rfind("grid ", 0) != 0)
        return "expects 'error', 'run-ok', 'stderr-contains <text>' or "
               "'grid <grid> count(<cell>) <op> <n>'";
    size_t i = 5, n = t.size();
    auto skip = [&] { while (i < n && t[i] == ' ') ++i; };
    auto word = [&] {
        size_t s = i;
        if (i < n && ident_start(t[i])) while (i < n && ident_cont(t[i])) ++i;
        return i > s;
    };
    char const* form = "expects 'grid <grid> count(<cell>) <op> <n>'";
    skip();
    if (!word()) return form;
    skip();
    if (t.compare(i, 6, "count(") != 0) return form;
    i += 6;
    if (i < n && t[i] == '.') ++i;
    else if (i < n && digit(t[i])) while (i < n && digit(t[i])) ++i;
    else if (!word()) return form;
    if (i >= n || t[i] != ')') return form;
    ++i;
    skip();
    size_t o = i;
    while (i < n && (t[i] == '=' || t[i] == '!' || t[i] == '<' || t[i] == '>')) ++i;
    std::string op = t.substr(o, i - o);
    if (op != "==" && op != "!=" && op != "<" && op != "<=" && op != ">" && op != ">=")
        return form;
    skip();
    size_t d = i;
    while (i < n && digit(t[i])) ++i;
    return i > d && i == n ? "" : form;
}

// INTEGER no larger than `max`.
std::optional<uint64_t> integer(std::string const& s, uint64_t max) {
    if (s.empty() || !digit(s[0])) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        uint64_t d = (uint64_t)(c - '0');
        if (v > (max - d) / 10) return std::nullopt;
        v = v * 10 + d;
    }
    return v;
}

// A directive's possible target: what starts on one line of one module.
struct item {
    source_loc  loc;
    std::string owner, name;
    int         index{-1};
};

using line_key = std::pair<int, int>;   // (module, line)

}  // namespace

directive const* directive_set::find(std::string_view key, directive_target target,
                                     std::string_view owner, std::string_view name,
                                     int index) const {
    for (auto const& d : all)
        if (d.key == key && d.target == target && d.owner == owner && d.name == name &&
            d.index == index)
            return &d;
    return nullptr;
}

std::optional<uint64_t> directive_set::seed() const {
    if (auto d = find("seed", directive_target::file, "", "")) return d->number;
    return std::nullopt;
}

std::vector<std::string> directive_set::expects() const {
    std::vector<std::string> out;
    for (auto const& d : all)
        if (d.key == "expect") out.push_back(d.value);
    return out;
}

std::vector<std::pair<std::string, int>> directive_set::params() const {
    std::vector<std::pair<std::string, int>> out;
    for (auto const& d : all)
        if (d.key == "param") out.emplace_back(d.name, std::stoi(d.value));
    return out;
}

directive_set read_directives(module_closure const& mods) {
    directive_set out;
    out.modules = mods.names;
    ast_file const& ast = mods.merged;

    // Where each target kind's items start
    std::map<line_key, std::vector<item>> starts[4];
    auto add = [&](directive_target k, item it) {
        starts[(int)k][{it.loc.mod, it.loc.line}].push_back(std::move(it));
    };
    for (auto const& t : ast.tags)
        for (auto const& v : t.values) add(directive_target::tag_value, {v.loc, t.name.text, v.text});
    for (auto const& l : ast.layers.layers)
        if (!l.is_graph) add(directive_target::layer, {l.name.loc, "", l.name.text});
    for (auto const& sq : ast.sequences)
        for (int i = 0; i < (int)sq.stmts.size(); ++i)
            add(directive_target::statement, {sq.stmts[(size_t)i].loc, sq.name.text, "", i});

    // (key, target identity) -> index into out.all, for "the last one applies"
    std::map<std::tuple<std::string, int, std::string, std::string, int>, size_t> seen;
    std::vector<char> dropped;

    for (auto const& c : ast.directive_comments) {
        std::string const& file = mods.names[(size_t)c.mod];
        auto warn_at = [&](int col, int end_col, std::string msg) {
            out.warnings.warning(file, c.line, col, c.line, end_col, std::move(msg));
        };
        auto raw = split(c);
        if (!raw) {
            warn_at(c.col, c.end_col,
                    "malformed directive comment, ignored: write '// @key=value ...' "
                    "(or '// @key <text>' for @expect and @param) with nothing else, "
                    "and prose in a separate comment");
            continue;
        }
        for (auto const& r : *raw) {
            auto warn = [&](std::string msg) { warn_at(r.col, r.end_col, std::move(msg)); };
            std::string at = "'@" + r.key + "'";
            key_spec const* spec = find_key(r.key);
            if (!spec) {
                warn("unknown directive " + at + ", ignored");
                continue;
            }

            directive d;
            d.key    = r.key;
            d.value  = r.value;
            d.target = spec->target;
            d.loc    = {c.line, r.col, c.line, r.end_col, c.mod};

            if ((spec->value == value_kind::text) != r.text) {
                warn(r.text ? at + " takes '=': write '@" + r.key + "=" + r.value + "'; ignored"
                            : at + " takes text: write '@" + r.key + " <text>'; ignored");
                continue;
            }
            if (spec->value == value_kind::text) {
                if (r.key == "param") {
                    auto p = param_text(r.value);
                    if (!p) {
                        warn(at + " expects '<param>=<integer>', got '" + r.value + "'; ignored");
                        continue;
                    }
                    d.name  = p->first;   // keyed by the param: one value each
                    d.value = p->second;
                } else if (auto problem = expect_problem(r.value); !problem.empty()) {
                    warn(at + " " + problem + ", got '" + r.value + "'; ignored");
                    continue;
                }
            } else if (spec->value == value_kind::ident) {
                if (!ident_start(r.value[0])) {
                    warn(at + " takes a name, got '" + r.value + "'; ignored");
                    continue;
                }
            } else {
                uint64_t max = spec->target == directive_target::file ? UINT64_MAX : INT32_MAX;
                auto v = integer(r.value, max);
                if (!v) {
                    warn(at + (digit(r.value[0]) ? " value '" + r.value + "' is out of range"
                                                 : " takes a number, got '" + r.value + "'") +
                         "; ignored");
                    continue;
                }
                d.number = *v;
            }

            if (spec->target == directive_target::file) {
                if (c.trailing || !c.leading) {
                    warn(at + " must be on its own line before the first declaration; ignored");
                    continue;
                }
                if (c.mod != mods.root) {
                    // a test file may be used by another: its test keys are no mistake
                    if (spec->value != value_kind::text)
                        warn(at + " is read from the root file only; ignored in a used module");
                    continue;
                }
            } else {
                auto const& lines = starts[(int)spec->target];
                auto it = c.code_line ? lines.find({c.mod, c.code_line}) : lines.end();
                std::string where = c.trailing ? "on this line"
                                  : c.code_line ? "on line " + std::to_string(c.code_line)
                                                : "after it";
                if (it == lines.end()) {
                    warn(at + " needs a " + spec->target_name + ", and none starts " + where +
                         "; ignored");
                    continue;
                }
                if (it->second.size() > 1) {
                    warn(at + " is ambiguous: " + std::to_string(it->second.size()) + " " +
                         spec->plural + " start " + where + "; ignored");
                    continue;
                }
                item const& tgt = it->second.front();
                d.target_loc = tgt.loc;
                d.owner      = tgt.owner;
                d.name       = tgt.name;
                d.index      = tgt.index;
            }

            auto id = std::make_tuple(d.key, c.mod, d.owner, d.name, d.index);
            if (d.key == "expect") {   // every one applies
                out.all.push_back(std::move(d));
                dropped.push_back(0);
                continue;
            }
            if (auto prev = seen.find(id); prev != seen.end()) {
                warn("duplicate " + at + (d.target == directive_target::file ? "" : " on this " +
                     std::string(spec->target_name)) + "; the last one applies");
                dropped[prev->second] = 1;
            }
            seen[id] = out.all.size();
            out.all.push_back(std::move(d));
            dropped.push_back(0);
        }
    }

    std::vector<directive> kept;
    for (size_t i = 0; i < out.all.size(); ++i)
        if (!dropped[i]) kept.push_back(std::move(out.all[i]));
    out.all = std::move(kept);

    // Two values of one tagset with the same display name
    std::map<std::pair<std::string, std::string>, directive const*> names;
    for (auto const& d : out.all) {
        if (d.key != "name") continue;
        auto [it, fresh] = names.emplace(std::make_pair(d.owner, d.value), &d);
        if (fresh) continue;
        out.warnings.warning(mods.names[(size_t)d.loc.mod], d.loc.line, d.loc.col,
                             d.loc.end_line, d.loc.end_col,
                             "display name '" + d.value + "' of '" + d.name +
                             "' is already the display name of '" + it->second->name +
                             "' in tag '" + d.owner + "'");
    }
    return out;
}

directive_set read_directives(std::string const& source, std::string const& name,
                              resolver const& resolve) {
    diagnostics ignored;
    return read_directives(load_closure(source, name, resolve, ignored));
}

}  // namespace ls
