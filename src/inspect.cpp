#include "inspect.hpp"
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

struct comma_list {
    std::string& o;
    bool first{true};
    void next() {
        if (!first) o += ',';
        first = false;
    }
};

// One tag-value occurrence in a pattern cell — what the colored-pattern
// decorations paint.
void emit_pattern_tokens(std::string& o, comma_list& cl,
                         compiled const& prog, pattern const& p) {
    if (p.is_where) return;
    int gid = prog.layer_id(p.grid);
    if (gid < 0) return;
    int tid = prog.layers[gid].tag_id;
    if (tid < 0) return;   // number grid: no value coloring
    for (auto const& row : p.cells)
        for (auto const& c : row) {
            if (c.kind != cell_kind::tag_mask) continue;
            for (auto const& a : c.atoms) {
                int vid = prog.value_id(tid, a.name);
                if (vid < 0) continue;   // unions/unknowns stay unpainted
                cl.next();
                o += "{\"line\":" + std::to_string(a.loc.line) +
                     ",\"col\":" + std::to_string(a.loc.col + (a.negate ? 1 : 0)) +
                     ",\"len\":" + std::to_string(a.name.size()) +
                     ",\"tag\":" + std::to_string(tid) +
                     ",\"value\":" + std::to_string(vid) + "}";
            }
        }
}

void emit_write_term_tokens(std::string& o, comma_list& cl,
                            compiled const& prog, write_term const& t) {
    if (t.what == write_term::kind::leaf) {
        emit_pattern_tokens(o, cl, prog, t.pat);
        return;
    }
    for (auto const& it : t.items) emit_write_term_tokens(o, cl, prog, it);
}

}  // namespace

std::string inspect_json(std::string const& source, std::string const& name) {
    diagnostics diags;
    auto ast = parse(source, name, diags);
    compiled prog;
    bool ok = false;
    if (ast)
        ok = analyze(*ast, prog, diags, name, /*best_effort=*/true);

    std::string o = "{\"ok\":";
    o += ok ? "true" : "false";

    o += ",\"diagnostics\":[";
    {
        comma_list cl{o};
        for (auto const& d : diags.all) {
            cl.next();
            o += "{\"line\":" + std::to_string(d.line) +
                 ",\"col\":" + std::to_string(d.col) +
                 ",\"severity\":";
            o += d.is_error ? "\"error\"" : "\"warning\"";
            o += ",\"message\":";
            js(o, d.message);
            o += "}";
        }
    }
    o += "]";

    o += ",\"tokens\":[";
    if (ast) {
        comma_list cl{o};
        for (auto const& r : ast->rules)
            for (auto const& pr : r.pairs) {
                for (auto const& lp : pr.lhs)
                    emit_pattern_tokens(o, cl, prog, lp);
                emit_write_term_tokens(o, cl, prog, pr.rhs);
            }
    }
    o += "]";

    o += ",\"symbols\":{\"tags\":[";
    {
        comma_list cl{o};
        for (int ti = 0; ti < (int)prog.tag_names.size(); ++ti) {
            cl.next();
            o += "{\"name\":";
            js(o, prog.tag_names[ti]);
            o += ",\"values\":[";
            comma_list vl{o};
            for (auto const& v : prog.tag_values[ti]) { vl.next(); js(o, v); }
            o += "],\"unions\":[";
            comma_list ul{o};
            if (ti < (int)prog.tag_unions.size())
                for (auto const& [uname, mask] : prog.tag_unions[ti]) {
                    (void)mask;
                    ul.next();
                    js(o, uname);
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
            js(o, l.tag_id >= 0 ? prog.tag_names[l.tag_id] : "number");
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
            js(o, prog.param_names[pi]);
            o += ",\"derived\":";
            o += derived ? "true" : "false";
            o += "}";
        }
    }
    o += "],\"rules\":[";
    {
        comma_list cl{o};
        for (auto const& r : prog.rules) { cl.next(); js(o, r.name); }
    }
    // completion vocabulary — kept in sync with the sema tables by the tests
    o += "],\"ops\":[\"resize\",\"upscale\",\"trim\",\"mirror\",\"pad\",\"path\"]";
    o += ",\"builtins\":[\"if\",\"min\",\"max\",\"abs\",\"clamp\",\"random\"]";
    o += "}}";
    return o;
}

}  // namespace ls
