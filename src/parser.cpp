#include "parser.hpp"
#include "lexer.hpp"

namespace ls {

namespace {

struct parser {
    std::vector<token> const& toks;
    std::string_view          file;
    diagnostics&              diags;
    size_t                    pos{0};

    token const& peek(int off = 0) const {
        size_t p = pos + (size_t)off;
        return p < toks.size() ? toks[p] : toks.back();
    }
    token const& eat() {
        token const& t = peek();
        if (pos + 1 < toks.size()) ++pos;
        return t;
    }
    bool at(token_type t) const  { return peek().is(t); }
    bool at_end() const          { return at(token_type::end); }
    bool accept(token_type t)    { if (at(t)) { eat(); return true; } return false; }

    void error_at(token const& t, std::string msg) {
        diags.error(file, t.line, t.col, std::move(msg));
    }
    bool expect(token_type t, char const* what) {
        if (accept(t)) return true;
        error_at(peek(), std::string("expected ") + what +
                 (peek().text.empty() ? "" : ", got '" + peek().text + "'"));
        return false;
    }

    void skip_newlines() { while (at(token_type::newline)) eat(); }
    void skip_seps()     { while (at(token_type::newline) || at(token_type::comma)) eat(); }

    source_loc loc() const { return {peek().line, peek().col}; }

    // ── declarations ─────────────────────────────────────────────────────────

    void parse_tag(ast_file& out) {
        tag_decl d;
        d.loc = loc();
        eat();   // 'tag'
        if (!expect(token_type::ident, "a tagset name")) return;
        d.name = toks[pos - 1].text;
        if (!expect(token_type::lbrace, "'{'")) return;
        skip_seps();
        while (!at(token_type::rbrace) && !at_end()) {
            if (!expect(token_type::ident, "a tag value name")) { eat_bad(); }
            else d.values.push_back(toks[pos - 1].text);
            skip_seps();
        }
        expect(token_type::rbrace, "'}'");
        out.tags.push_back(std::move(d));
    }

    void parse_layers(ast_file& out) {
        out.layers.loc = loc();
        out.has_layers = true;
        eat();   // 'layers'
        if (!expect(token_type::lbrace, "'{'")) return;
        skip_seps();
        while (!at(token_type::rbrace) && !at_end()) {
            layer_decl l;
            if (!expect(token_type::ident, "a grid name")) { eat_bad(); skip_seps(); continue; }
            l.name = toks[pos - 1].text;
            if (!expect(token_type::colon, "':'"))         { skip_seps(); continue; }
            if (!expect(token_type::kw_grid, "'grid'"))    { skip_seps(); continue; }
            if (!expect(token_type::kw_of, "'of'"))        { skip_seps(); continue; }
            if (accept(token_type::kw_number))       l.type = "number";
            else if (expect(token_type::ident, "a tagset name or 'number'"))
                l.type = toks[pos - 1].text;
            else { skip_seps(); continue; }
            out.layers.layers.push_back(std::move(l));
            skip_seps();
        }
        expect(token_type::rbrace, "'}'");
    }

    // ── patterns ─────────────────────────────────────────────────────────────

    bool parse_cell(cell& c) {
        c.loc = loc();
        if (accept(token_type::star)) { c.kind = cell_kind::any;   return true; }
        if (accept(token_type::dot))  { c.kind = cell_kind::empty; return true; }
        if (at(token_type::integer))  { c.kind = cell_kind::number; c.number = eat().int_val; return true; }
        if (at(token_type::ident))    { c.kind = cell_kind::tag;    c.tag    = eat().text;    return true; }
        error_at(peek(), "expected a pattern cell (*, ., a tag value, or an integer)");
        return false;
    }

    bool parse_pattern(pattern& p) {
        p.loc = loc();
        if (!expect(token_type::ident, "a grid name")) return false;
        p.grid = toks[pos - 1].text;
        if (!expect(token_type::lbracket, "'['")) return false;
        skip_newlines();   // '[' may be followed by a newline before the first row
        while (!at(token_type::rbracket) && !at_end()) {
            std::vector<cell> row;
            while (!at(token_type::newline) && !at(token_type::rbracket) && !at_end()) {
                cell c;
                if (!parse_cell(c)) { eat_bad(); continue; }
                row.push_back(std::move(c));
            }
            if (!row.empty()) p.cells.push_back(std::move(row));
            skip_newlines();
        }
        if (!expect(token_type::rbracket, "']'")) return false;
        p.rows = (int)p.cells.size();
        p.cols = p.cells.empty() ? 0 : (int)p.cells[0].size();
        return true;
    }

    void parse_rule(ast_file& out) {
        rule_decl r;
        r.loc = loc();
        eat();   // 'rule'
        if (!expect(token_type::ident, "a rule name")) return;
        r.name = toks[pos - 1].text;
        if (!expect(token_type::lbrace, "'{'")) return;
        skip_newlines();
        if (!parse_pattern(r.lhs)) { recover_to(token_type::rbrace); return; }
        skip_newlines();
        if (!expect(token_type::arrow, "'=>'")) { recover_to(token_type::rbrace); return; }
        skip_newlines();
        if (!parse_pattern(r.rhs)) { recover_to(token_type::rbrace); return; }
        skip_newlines();
        expect(token_type::rbrace, "'}'");
        out.rules.push_back(std::move(r));
    }

    // ── program ──────────────────────────────────────────────────────────────

    void parse_apply(program_stmt& s) {
        if (accept(token_type::kw_one))      s.strat = strategy::one;
        else if (accept(token_type::kw_all)) s.strat = strategy::all;
        else {   // 'some'
            eat();
            s.strat = strategy::some;
            if (!expect(token_type::lparen, "'('")) return;
            if (!expect(token_type::kw_max, "'max'")) return;
            if (!expect(token_type::equals, "'='")) return;
            if (!expect(token_type::integer, "a count")) return;
            s.max_count = (int)toks[pos - 1].int_val;
            if (!expect(token_type::rparen, "')'")) return;
        }
        if (expect(token_type::ident, "a rule name"))
            s.rule_name = toks[pos - 1].text;
    }

    void parse_op_call(program_stmt& s) {
        s.what = program_stmt::kind::op_call;
        s.op_name = eat().text;   // IDENT
        if (!expect(token_type::lparen, "'('")) return;
        while (!at(token_type::rparen) && !at_end()) {
            op_arg a;
            a.loc = loc();
            if (!expect(token_type::integer, "an integer argument")) { eat_bad(); continue; }
            a.int_val = toks[pos - 1].int_val;
            s.op_args.push_back(a);
            if (!accept(token_type::comma)) break;
        }
        expect(token_type::rparen, "')'");
    }

    void parse_program(ast_file& out) {
        out.program.loc = loc();
        out.has_program = true;
        eat();   // 'program'
        if (!expect(token_type::lbrace, "'{'")) return;
        skip_newlines();
        while (!at(token_type::rbrace) && !at_end()) {
            program_stmt s;
            s.loc = loc();
            if (at(token_type::kw_one) || at(token_type::kw_all) || at(token_type::kw_some)) {
                parse_apply(s);
            } else if (at(token_type::ident) && peek(1).is(token_type::lparen)) {
                parse_op_call(s);
            } else {
                error_at(peek(), "expected a statement (a strategy + rule, or an operation call)");
                eat_bad();
                skip_newlines();
                continue;
            }
            out.program.stmts.push_back(std::move(s));
            skip_newlines();
        }
        expect(token_type::rbrace, "'}'");
    }

    // ── recovery ─────────────────────────────────────────────────────────────

    void eat_bad() { if (!at_end()) eat(); }
    void recover_to(token_type t) {
        while (!at(t) && !at_end()) eat();
        accept(t);
    }

    // ── entry ────────────────────────────────────────────────────────────────

    void run(ast_file& out) {
        skip_newlines();
        while (!at_end()) {
            switch (peek().type) {
            case token_type::kw_tag:     parse_tag(out);     break;
            case token_type::kw_layers:  parse_layers(out);  break;
            case token_type::kw_rule:    parse_rule(out);    break;
            case token_type::kw_program:
                if (out.has_program)
                    error_at(peek(), "only one 'program' block per file");
                parse_program(out);
                break;
            default:
                error_at(peek(), "expected a declaration (tag, layers, rule, program)");
                eat_bad();
                break;
            }
            skip_newlines();
        }
    }
};

}  // namespace

std::optional<ast_file> parse(std::string_view source, std::string_view file,
                              diagnostics& diags) {
    auto toks = lex(source, file, diags);
    ast_file out;
    parser p{toks, file, diags};
    p.run(out);
    return out;
}

}  // namespace ls
