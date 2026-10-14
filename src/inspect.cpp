#include "inspect.hpp"
#include "modules.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <cstdio>

namespace ls {

namespace {

// Hand-rolled JSON emission — ls_lib stays dependency-free.
void js(std::string& o, std::string_view s) {
    o += '"';
    for (char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            } else {
                o += c;
            }
        }
    }
    o += '"';
}

// `"line":L,"col":C,"len":N` for a span on one line (len 0 otherwise).
std::string span_json(source_loc const& l) {
    int len = l.end_line == l.line ? l.end_col - l.col : 0;
    return "\"line\":" + std::to_string(l.line) + ",\"col\":" + std::to_string(l.col) +
           ",\"len\":" + std::to_string(len);
}

struct comma_list {
    std::string& o;
    bool first{true};
    void next() {
        if (!first) o += ',';
        first = false;
    }
};

// Palette slot of a named union: the "next colors" after its tag's values,
// so a union reads as its own swatch rather than a blend of its members.
int union_slot(compiled const& prog, int tid, std::string const& name) {
    int u = prog.union_id(tid, name);
    return u < 0 ? -1 : (int)prog.tag_values[(size_t)tid].size() + u;
}

// One colorable occurrence in a pattern cell — what the colored-pattern
// decorations paint. tag >= 0: a tag value or union (value = palette slot);
// -1: '*'; -2: '.'; -3: a number-grid literal (value = the number).
void emit_pattern_tokens(std::string& o, comma_list& cl,
                         compiled const& prog, pattern const& p) {
    if (p.is_where) return;
    int gid = prog.layer_id(p.grid.text);
    if (gid < 0) return;
    int tid = prog.layers[gid].tag_id;   // -1: number grid
    // Paints a bare cell; '!' and '|' are painted through to their atoms, and
    // a parenthesized cell is not painted.
    auto paint = [&](auto& self, expr const& e) -> void {
        auto token = [&](int tag, long long value) {
            cl.next();
            o += "{" + span_json(e.loc) + ",\"tag\":" + std::to_string(tag) +
                 ",\"value\":" + std::to_string(value) + "}";
        };
        switch (e.kind) {
        case expr_kind::wildcard:  token(-1, -1); return;
        case expr_kind::empty_lit: token(-2, -2); return;
        case expr_kind::int_lit:   token(-3, e.int_val); return;
        case expr_kind::not_:
        case expr_kind::bit_or:
            for (auto const& a : e.args) self(self, *a);
            return;
        case expr_kind::ident: {
            if (tid < 0) return;
            int vid = prog.value_id(tid, e.ident);
            if (vid < 0) vid = union_slot(prog, tid, e.ident);
            if (vid >= 0) token(tid, vid);   // unknown names stay unpainted
            return;
        }
        default:
            return;
        }
    };
    for (auto const& row : p.cells)
        for (auto const& c : row) paint(paint, *c);
}

// Collect grid names and rule names as hover/definition references
void emit_pattern_refs(std::string& o, comma_list& cl, pattern const& p) {
    if (p.is_where) return;
    if (p.grid.text.empty()) return;
    cl.next();
    o += "{" + span_json(p.grid.loc) + ",\"kind\":\"layer\",\"target\":";
    js(o, p.grid.text);
    o += "}";
}

// `,"loc":{...}` of a declared name, with its module's canonical name, so an
// editor can jump into the file that declares it.
void emit_loc(std::string& o, module_closure const& mods, ident const& name) {
    o += ",\"loc\":{" + span_json(name.loc) + ",\"module\":";
    js(o, mods.names[(size_t)name.loc.mod]);
    o += "}";
}

// Every pattern of the root module's rules: declared rules, then those written
// inline in its sequences' statements.
template <class F>
void for_each_root_pattern(ast_file const& ast, int root, F&& f) {
    auto rule = [&](rule_decl const& r) {
        for (auto const& pr : r.pairs) for_each_pattern(pr, f);
    };
    for (auto const& r : ast.rules)
        if (r.loc.mod == root) rule(r);
    for (auto const& sq : ast.sequences)
        if (sq.loc.mod == root)
            for (auto const& s : sq.stmts)
                if (s.inline_rule) rule(*s.inline_rule);
}

}  // namespace

std::string inspect_json(std::string const& source, std::string const& name,
                         resolver const& resolve) {
    diagnostics diags;
    module_closure mods = load_closure(source, name, resolve, diags);
    compiled prog;
    bool ok = analyze(mods, prog, diags, /*best_effort=*/true) && !diags.has_errors();
    ast_file const* ast = &mods.merged;
    // tokens and refs are positions in the edited file: the root module only
    auto in_root = [&](source_loc l) { return l.mod == mods.root; };

    std::string o = "{\"ok\":";
    o += ok ? "true" : "false";

    o += ",\"diagnostics\":[";
    {
        comma_list cl{o};
        for (auto const& d : diags.all) {
            cl.next();
            o += "{\"line\":" + std::to_string(d.line) +
                 ",\"col\":" + std::to_string(d.col) + ",\"module\":";
            js(o, d.file);
            o += ",\"severity\":";
            o += d.is_error ? "\"error\"" : "\"warning\"";
            o += ",\"message\":";
            js(o, d.message);
            o += "}";
        }
    }
    o += "]";

    o += ",\"tokens\":[";
    {
        comma_list cl{o};
        for_each_root_pattern(*ast, mods.root, [&](pattern const& p) {
            emit_pattern_tokens(o, cl, prog, p);
        });
    }
    o += "]";

    o += ",\"refs\":[";
    {
        comma_list cl{o};
        // Rule / sequence references in program and sequence statements
        auto stmt_refs = [&](std::vector<program_stmt> const& stmts) {
            for (auto const& s : stmts) {
                if (s.what != program_stmt::kind::apply || s.inline_rule) continue;
                bool is_seq = !lookup(ast->rules, s.rule_name.text) &&   // rules win lookups
                              lookup(ast->sequences, s.rule_name.text);
                cl.next();
                o += "{" + span_json(s.rule_name.loc) +
                     ",\"kind\":\"" + (is_seq ? "sequence" : "rule") + "\",\"target\":";
                js(o, s.rule_name.text);
                o += "}";
            }
        };
        for (auto const& sq : ast->sequences)
            if (in_root(sq.loc)) stmt_refs(sq.stmts);
        // Grid references in rule patterns, named and inline
        for_each_root_pattern(*ast, mods.root, [&](pattern const& p) {
            emit_pattern_refs(o, cl, p);
        });
    }
    o += "]";

    o += ",\"symbols\":{\"modules\":[";
    {
        comma_list cl{o};
        for (int id : mods.order) { cl.next(); js(o, mods.names[(size_t)id]); }
    }
    o += "],\"tags\":[";
    {
        comma_list cl{o};
        for (int ti = 0; ti < (int)prog.tag_names.size(); ++ti) {
            tag_decl const* td = ti < (int)ast->tags.size() ? &ast->tags[(size_t)ti] : nullptr;
            cl.next();
            o += "{\"name\":";
            js(o, prog.tag_names[ti]);
            if (td) emit_loc(o, mods, td->name);
            o += ",\"values\":[";
            comma_list vl{o};
            for (int i = 0; i < (int)prog.tag_values[ti].size(); ++i) {
                vl.next();
                o += "{\"name\":";
                js(o, prog.tag_values[ti][i]);
                if (td && i < (int)td->values.size())
                    emit_loc(o, mods, td->values[(size_t)i]);
                o += "}";
            }
            o += "],\"unions\":[";
            comma_list ul{o};
            if (ti < (int)prog.tag_unions.size())
                for (auto const& [uname, mask] : prog.tag_unions[ti]) {
                    (void)mask;
                    ul.next();
                    o += "{\"name\":";
                    js(o, uname);
                    if (auto au = td ? lookup(td->unions, uname) : nullptr)
                        emit_loc(o, mods, au->name);
                    o += "}";
                }
            o += "]}";
        }
    }
    o += "],\"layers\":[";
    {
        comma_list cl{o};
        for (auto const& l : prog.layers) {
            cl.next();
            o += "{\"name\":";
            js(o, l.name);
            o += ",\"type\":";
            js(o, l.tag_id >= 0 ? prog.tag_names[(size_t)l.tag_id] : "number");
            if (auto al = lookup(ast->layers.layers, l.name))   // first declaration wins
                emit_loc(o, mods, al->name);
            o += "}";
        }
    }
    o += "],\"params\":[";
    {
        comma_list cl{o};
        for (int pi = 0; pi < (int)prog.param_names.size(); ++pi) {
            bool derived = false;
            for (auto const& se : prog.startup_exprs)
                if (se.param == pi && !se.is_default) derived = true;
            cl.next();
            o += "{\"name\":";
            js(o, prog.param_names[(size_t)pi]);
            o += ",\"derived\":";
            o += derived ? "true" : "false";
            if (auto ap = lookup(ast->params, prog.param_names[(size_t)pi]))
                emit_loc(o, mods, ap->name);
            o += "}";
        }
    }
    o += "],\"rules\":[";
    {
        comma_list cl{o};
        for (auto const& r : ast->rules) {
            cl.next();
            o += "{\"name\":";
            js(o, r.name.text);
            emit_loc(o, mods, r.name);
            o += "}";
        }
    }
    o += "],\"sequences\":[";
    {
        comma_list cl{o};
        for (auto const& sq : ast->sequences) {
            cl.next();
            o += "{\"name\":";
            js(o, sq.name.text);
            emit_loc(o, mods, sq.name);
            o += "}";
        }
    }
    // completion vocabulary — kept in sync with the sema tables by the tests
    o += "],\"ops\":[\"resize\",\"upscale\",\"trim\",\"mirror\",\"pad\",\"path\"]";
    o += ",\"builtins\":[\"if\",\"min\",\"max\",\"abs\",\"clamp\",\"random\"]";
    o += "}}";
    return o;
}

}  // namespace ls
