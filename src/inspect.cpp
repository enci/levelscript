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

// Palette slot of a named union: the "next colors" after its tag's values,
// so a union reads as its own swatch rather than a blend of its members.
int union_slot(compiled const& prog, int tid, std::string const& name) {
    if (tid < 0 || tid >= (int)prog.tag_unions.size()) return -1;
    auto const& us = prog.tag_unions[tid];
    for (int i = 0; i < (int)us.size(); ++i)
        if (us[i].first == name) return (int)prog.tag_values[tid].size() + i;
    return -1;
}

// One colorable occurrence in a pattern cell — what the colored-pattern
// decorations paint. tag >= 0: a tag value or union (value = palette slot);
// -1: '*'; -2: '.'; -3: a number-grid literal (value = the number).
void emit_pattern_tokens(std::string& o, comma_list& cl,
                         compiled const& prog, pattern const& p) {
    if (p.is_where) return;
    int gid = prog.layer_id(p.grid);
    if (gid < 0) return;
    int tid = prog.layers[gid].tag_id;   // -1: number grid
    for (auto const& row : p.cells) {
        for (auto const& c : row) {
            if (c.kind == cell_kind::any) {
                cl.next();
                o += "{\"line\":" + std::to_string(c.loc.line) +
                     ",\"col\":" + std::to_string(c.loc.col) +
                     ",\"len\":1,\"tag\":-1,\"value\":-1}";
                continue;
            }
            if (c.kind == cell_kind::empty) {
                cl.next();
                o += "{\"line\":" + std::to_string(c.loc.line) +
                     ",\"col\":" + std::to_string(c.loc.col) +
                     ",\"len\":1,\"tag\":-2,\"value\":-2}";
                continue;
            }
            if (c.kind == cell_kind::number) {
                cl.next();
                o += "{\"line\":" + std::to_string(c.loc.line) +
                     ",\"col\":" + std::to_string(c.loc.col) +
                     ",\"len\":" + std::to_string(std::to_string(c.number).size()) +
                     ",\"tag\":-3,\"value\":" + std::to_string(c.number) + "}";
                continue;
            }
            if (c.kind != cell_kind::tag_mask || tid < 0) continue;
            for (auto const& a : c.atoms) {
                int vid = prog.value_id(tid, a.name);
                if (vid < 0) vid = union_slot(prog, tid, a.name);
                if (vid < 0) continue;   // unknown names stay unpainted
                cl.next();
                o += "{\"line\":" + std::to_string(a.loc.line) +
                     ",\"col\":" + std::to_string(a.loc.col + (a.negate ? 1 : 0)) +
                     ",\"len\":" + std::to_string(a.name.size()) +
                     ",\"tag\":" + std::to_string(tid) +
                     ",\"value\":" + std::to_string(vid) + "}";
            }
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

// Collect grid names and rule names as hover/definition references
void emit_pattern_refs(std::string& o, comma_list& cl, pattern const& p) {
    if (p.is_where) return;
    if (p.grid.empty()) return;
    cl.next();
    o += "{\"line\":" + std::to_string(p.grid_loc.line) +
         ",\"col\":" + std::to_string(p.grid_loc.col) +
         ",\"len\":" + std::to_string(p.grid.size()) +
         ",\"kind\":\"layer\",\"target\":";
    js(o, p.grid);
    o += "}";
}

void emit_write_term_refs(std::string& o, comma_list& cl, write_term const& t) {
    if (t.what == write_term::kind::leaf) {
        emit_pattern_refs(o, cl, t.pat);
        return;
    }
    for (auto const& it : t.items) emit_write_term_refs(o, cl, it);
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

    o += ",\"refs\":[";
    if (ast) {
        comma_list cl{o};
        // Rule / sequence references in program and sequence statements
        auto stmt_refs = [&](std::vector<program_stmt> const& stmts) {
            for (auto const& s : stmts) {
                if (s.what != program_stmt::kind::apply) continue;
                bool is_seq = false;
                for (auto const& sq : ast->sequences)
                    if (sq.name == s.rule_name) is_seq = true;
                for (auto const& r : ast->rules)
                    if (r.name == s.rule_name) is_seq = false;   // rules win lookups
                cl.next();
                o += "{\"line\":" + std::to_string(s.rule_name_loc.line) +
                     ",\"col\":" + std::to_string(s.rule_name_loc.col) +
                     ",\"len\":" + std::to_string(s.rule_name.size()) +
                     ",\"kind\":\"" + (is_seq ? "sequence" : "rule") + "\",\"target\":";
                js(o, s.rule_name);
                o += "}";
            }
        };
        if (ast->has_program) stmt_refs(ast->program.stmts);
        for (auto const& sq : ast->sequences) stmt_refs(sq.stmts);
        // Grid references in Rule patterns
        for (auto const& r : ast->rules) {
            for (auto const& pr : r.pairs) {
                for (auto const& lp : pr.lhs) emit_pattern_refs(o, cl, lp);
                emit_write_term_refs(o, cl, pr.rhs);
            }
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
            for (int i = 0; i < (int)prog.tag_values[ti].size(); ++i) { 
                vl.next(); 
                o += "{\"name\":";
                js(o, prog.tag_values[ti][i]);
                if (ast && ast->has_layers && ti < (int)ast->tags.size() && i < (int)ast->tags[ti].values.size()) {
                    auto const& ast_val = ast->tags[ti].values[i];
                    o += ",\"loc\":{\"line\":" + std::to_string(ast_val.loc.line) + 
                         ",\"col\":" + std::to_string(ast_val.loc.col) + 
                         ",\"len\":" + std::to_string(ast_val.name.size()) + "}";
                }
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
                    if (ast && ti < (int)ast->tags.size())
                        for (auto const& au : ast->tags[ti].unions)
                            if (au.name == uname)
                                o += ",\"loc\":{\"line\":" + std::to_string(au.loc.line) +
                                     ",\"col\":" + std::to_string(au.loc.col) +
                                     ",\"len\":" + std::to_string(au.name.size()) + "}";
                    o += "}";
                }
            o += "]}";
        }
    }
    o += "],\"layers\":[";
    {
        comma_list cl{o};
        for (int i = 0; i < (int)prog.layers.size(); ++i) {
            auto const& l = prog.layers[i];
            cl.next();
            o += "{\"name\":";
            js(o, l.name);
            o += ",\"type\":";
            js(o, l.tag_id >= 0 ? prog.tag_names[l.tag_id] : "number");
            
            // Output definition location if we have it in AST
            if (ast && ast->has_layers && i < (int)ast->layers.layers.size()) {
                auto const& ast_l = ast->layers.layers[i];
                o += ",\"loc\":{\"line\":" + std::to_string(ast_l.loc.line) + 
                     ",\"col\":" + std::to_string(ast_l.loc.col) + 
                     ",\"len\":" + std::to_string(ast_l.name.size()) + "}";
            }
            
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
        for (int i = 0; i < (int)prog.rules.size(); ++i) { 
            cl.next(); 
            o += "{\"name\":";
            js(o, prog.rules[i].name);
            
            if (ast && i < (int)ast->rules.size()) {
                auto const& ast_r = ast->rules[i];
                o += ",\"loc\":{\"line\":" + std::to_string(ast_r.loc.line) + 
                     ",\"col\":" + std::to_string(ast_r.loc.col) + 
                     ",\"len\":" + std::to_string(ast_r.name.size()) + "}";
            }
            o += "}";
        }
    }
    o += "],\"sequences\":[";
    if (ast) {
        comma_list cl{o};
        for (auto const& sq : ast->sequences) {
            cl.next();
            o += "{\"name\":";
            js(o, sq.name);
            o += ",\"loc\":{\"line\":" + std::to_string(sq.name_loc.line) +
                 ",\"col\":" + std::to_string(sq.name_loc.col) +
                 ",\"len\":" + std::to_string(sq.name.size()) + "}}";
        }
    }
    // completion vocabulary — kept in sync with the sema tables by the tests
    o += "],\"ops\":[\"resize\",\"upscale\",\"trim\",\"mirror\",\"pad\",\"path\"]";
    o += ",\"builtins\":[\"if\",\"min\",\"max\",\"abs\",\"clamp\",\"random\"]";
    o += "}}";
    return o;
}

}  // namespace ls
