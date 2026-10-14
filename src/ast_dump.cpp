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
        s += e.kind == expr_kind::call ? "call " + e.ident : op_symbol(e.kind);
        for (auto const& a : e.args) s += " " + expr_str(*a);
        return s + at(e.loc) + ")";
    }

    void pattern(ls::pattern const& p) {
        open(p.is_where ? std::string("where") : "pattern " + p.grid.text, &p.loc);
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
            for (auto const& p : pr.lhs) pattern(p);
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
            for (auto const& u : t.unions) {
                std::string s = "(union " + u.name.text;
                for (auto const& m : u.members) s += " " + m;
                line(s + ")");
            }
            close();
        }
        if (ast.has_layers) {
            open("layers", &ast.layers.loc);
            for (auto const& l : ast.layers.layers) line("(grid " + l.name.text + " " + l.type + ")");
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

}  // namespace ls
