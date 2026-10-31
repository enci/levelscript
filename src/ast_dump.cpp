#include "ast_dump.hpp"
#include "parser.hpp"

namespace ls {

namespace {

char const* op_symbol(expr_kind k) {
    switch (k) {
    case expr_kind::neg:    return "neg";
    case expr_kind::not_:   return "!";
    case expr_kind::add:    return "+";
    case expr_kind::sub:    return "-";
    case expr_kind::mul:    return "*";
    case expr_kind::div_:   return "/";
    case expr_kind::lt:     return "<";
    case expr_kind::le:     return "<=";
    case expr_kind::gt:     return ">";
    case expr_kind::ge:     return ">=";
    case expr_kind::eq:     return "==";
    case expr_kind::ne:     return "!=";
    case expr_kind::and_:   return "&&";
    case expr_kind::or_:    return "||";
    case expr_kind::bit_or: return "|";
    case expr_kind::is_:    return "is";
    case expr_kind::paren:  return "paren";
    default:                return "?";
    }
}

struct printer {
    bool        spans;
    std::string out;
    int         depth{0};

    std::string at(source_loc const& l) const {
        if (!spans) return "";
        return " @" + std::to_string(l.line) + ":" + std::to_string(l.col) + "-" +
               std::to_string(l.end_line) + ":" + std::to_string(l.end_col);
    }

    // A line at the current depth. `open` starts a node, its span on the
    // first line, whose children follow indented; `close` ends it on the last.
    void line(std::string const& s) { out += std::string((size_t)depth * 2, ' ') + s + '\n'; }
    void open(std::string const& head, source_loc const* l = nullptr) {
        line("(" + head + (l ? at(*l) : ""));
        ++depth;
    }
    void close() {
        --depth;
        out.pop_back();   // the last child's newline
        out += ")\n";
    }

    std::string expr_str(expr const& e) const {
        switch (e.kind) {
        case expr_kind::int_lit:   return std::to_string(e.int_val);
        case expr_kind::empty_lit: return ".";
        case expr_kind::wildcard:  return "*";
        case expr_kind::ident:
        case expr_kind::variable:  return e.ident;
        default: break;
        }
        std::string s = "(";
        s += e.kind == expr_kind::call     ? "call " + e.ident
           : e.kind == expr_kind::selector ? "selector " + e.ident
                                           : std::string(op_symbol(e.kind));
        for (auto const& a : e.args) s += " " + expr_str(*a);
        return s + at(e.loc) + ")";
    }

    void pattern(ls::pattern const& p) {
        open(p.is_where ? std::string("where") : "pattern " + p.grid.text, &p.loc);
        if (p.sized)
            line("(size " + std::to_string(p.size_cols) + " " + std::to_string(p.size_rows) +
                 at(p.size_loc) + ")");
        if (!p.extent.text.empty()) line("(extent " + p.extent.text + at(p.size_loc) + ")");
        for (auto const& row : p.cells) {
            std::string s = "(row";
            for (auto const& c : row) s += " " + expr_str(*c);
            line(s + ")");
        }
        close();
    }

    void write(write_term const& t) {
        if (t.weight != 1) open("weight " + std::to_string(t.weight), &t.loc);
        if (t.what == write_term::kind::leaf) {
            pattern(t.pat);
        } else {
            open(t.what == write_term::kind::all ? "all" : "any", &t.loc);
            for (auto const& it : t.items) write(it);
            close();
        }
        if (t.weight != 1) close();
    }

    void rule(rule_decl const& r) {
        open(r.name.text.empty() ? std::string("rule") : "rule " + r.name.text, &r.loc);
        if (r.symmetry != "none") line("(symmetry " + r.symmetry + ")");
        if (!r.rotation_angles.empty()) {
            std::string s = "(rotation";
            for (long long a : r.rotation_angles) s += " " + std::to_string(a);
            line(s + ")");
        }
        static char const* const bodies[] = {"none", "all", "any", "ordered"};
        if (r.body != body_combinator::none) line(std::string("(body ") + bodies[(int)r.body] + ")");
        for (auto const& pr : r.pairs) {
            open("pair", &pr.loc);
            open("match");
            if (pr.is_graph) {
                open("graph " + pr.block.graph.text, &pr.block.loc);
                for (auto const& it : pr.block.items) {
                    switch (it.what) {
                    case graph_item::kind::node: line("(node " + it.a.text + at(it.loc) + ")"); break;
                    case graph_item::kind::edge:
                        line("(edge " + it.a.text + " " + it.b.text + at(it.loc) + ")");
                        break;
                    case graph_item::kind::test:
                        line("(test " + it.a.text + " " + expr_str(*it.test) + at(it.loc) + ")");
                        break;
                    }
                }
                close();
            }
            size_t gi = 0;
            for (; gi < pr.guards.size() && pr.guards[gi].after == 0; ++gi)
                line("(when " + expr_str(*pr.guards[gi].expr) + at(pr.guards[gi].loc) + ")");
            for (size_t pi = 0; pi < pr.lhs.size(); ++pi) {
                pattern(pr.lhs[pi]);
                for (; gi < pr.guards.size() && pr.guards[gi].after == (int)pi + 1; ++gi)
                    line("(when " + expr_str(*pr.guards[gi].expr) + at(pr.guards[gi].loc) + ")");
            }
            close();
            open("write");
            write(pr.rhs);
            close();
            close();
        }
        close();
    }

    void statement(program_stmt const& s) {
        std::string guard = s.guard ? " (when " + expr_str(*s.guard) + ")" : "";
        if (s.what == program_stmt::kind::op_call) {
            std::string l = "(op " + s.op_name;
            for (auto const& a : s.op_args)
                l += " (arg " + (a.name.empty() ? "" : a.name + " ") + expr_str(*a.value) + ")";
            line(l + guard + at(s.loc) + ")");
            return;
        }
        std::string head = mode_name(s.mode);
        if (s.count)
            head += std::string(s.count_percent ? " (percent " : " (count ") + expr_str(*s.count) + ")";
        if (!s.inline_rule) {
            line("(" + head + " " + s.rule_name.text + guard + at(s.loc) + ")");
            return;
        }
        open(head, &s.loc);
        rule(*s.inline_rule);
        if (s.guard) line(guard.substr(1));
        close();
    }

    void file(ast_file const& ast) {
        for (auto const& u : ast.uses) line("(use \"" + u.path + "\"" + at(u.loc) + ")");
        for (auto const& t : ast.tags) {
            open("tag " + t.name.text, &t.loc);
            for (auto const& v : t.values) line("(value " + v.text + ")");
            for (auto const& u : t.unions)
                line("(union " + u.name.text + " " + expr_source(*u.value) + ")");
            close();
        }
        if (ast.has_layers) {
            open("layers", &ast.layers.loc);
            for (auto const& l : ast.layers.layers) {
                std::string s = std::string(l.is_graph ? "(graph " : "(grid ") + l.name.text + " " + l.type;
                for (auto const& a : l.args) s += " (" + a.name.text + " " + expr_str(*a.mask) + ")";
                line(s + ")");
            }
            close();
        }
        for (auto const& p : ast.params) {
            std::string s = std::string(p.is_derived ? "(derived " : "(input ") + p.name.text;
            if (p.value) s += " " + expr_str(*p.value);
            line(s + ")");
        }
        for (auto const& r : ast.rules) rule(r);
        for (auto const& sq : ast.sequences) {
            open("sequence " + sq.name.text, &sq.loc);
            for (auto const& s : sq.stmts) statement(s);
            close();
        }
    }
};

}  // namespace

std::string dump_ast(ast_file const& ast, bool spans) {
    printer p{spans, {}};
    p.file(ast);
    return p.out;
}

std::string dump_ast(std::string_view source, std::string_view name, bool spans,
                     diagnostics& diags) {
    auto ast = parse(source, name, diags);
    return ast ? dump_ast(*ast, spans) : "";
}

namespace {

// Binding strength, as the parser's precedence table; 9 = unary, 10 = atom.
int precedence(expr_kind k) {
    switch (k) {
    case expr_kind::or_:    return 1;
    case expr_kind::and_:   return 2;
    case expr_kind::is_:    return 3;
    case expr_kind::bit_or: return 4;
    case expr_kind::eq: case expr_kind::ne: return 5;
    case expr_kind::lt: case expr_kind::le: case expr_kind::gt: case expr_kind::ge: return 6;
    case expr_kind::add: case expr_kind::sub: return 7;
    case expr_kind::mul: case expr_kind::div_: return 8;
    case expr_kind::neg: case expr_kind::not_: return 9;
    default: return 10;
    }
}

// `e`, parenthesized when it binds looser than `min`.
std::string operand(expr const& e, int min) {
    std::string s = expr_source(e);
    return precedence(e.kind) < min ? "(" + s + ")" : s;
}

}  // namespace

std::string expr_source(expr const& e) {
    switch (e.kind) {
    case expr_kind::int_lit:   return std::to_string(e.int_val);
    case expr_kind::empty_lit: return ".";
    case expr_kind::wildcard:  return "*";
    case expr_kind::ident:
    case expr_kind::variable:  return e.ident;
    case expr_kind::paren:     return "(" + expr_source(*e.args[0]) + ")";
    case expr_kind::neg:       return "-" + operand(*e.args[0], 9);
    case expr_kind::not_:      return "!" + operand(*e.args[0], 9);
    case expr_kind::call:
    case expr_kind::selector: {
        bool call = e.kind == expr_kind::call;
        std::string s = e.ident + (call ? "(" : "[");
        for (size_t i = 0; i < e.args.size(); ++i)
            s += (i ? ", " : "") + expr_source(*e.args[i]);
        return s + (call ? ")" : "]");
    }
    default: {   // binary, left-associative: the right operand binds tighter
        int p = precedence(e.kind);
        return operand(*e.args[0], p) + " " + op_symbol(e.kind) + " " +
               operand(*e.args[1], p + 1);
    }
    }
}

std::string graph_block_source(graph_block const& b) {
    // a test is a whitespace-free mask: `Hall|Inn`, not the infix `Hall | Inn`
    auto mask = [](auto& self, expr const& e) -> std::string {
        if (e.kind == expr_kind::bit_or) return self(self, *e.args[0]) + "|" + self(self, *e.args[1]);
        return expr_source(e);
    };
    std::string s = b.graph.text + " {";
    for (size_t i = 0; i < b.items.size(); ++i) {
        auto const& it = b.items[i];
        s += (i ? ", " : " ") + it.a.text;
        if (it.what == graph_item::kind::edge) s += " -- " + it.b.text;
        if (it.what == graph_item::kind::test) s += "[" + mask(mask, *it.test) + "]";
    }
    return s + " }";
}

}  // namespace ls
