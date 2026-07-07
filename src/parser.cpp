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

    // ── rule attributes: (symmetry=…, rotation=…) ────────────────────────────

    void parse_rotation_value(rule_decl& r) {
        if (accept(token_type::kw_none)) return;
        if (accept(token_type::kw_all)) {
            r.rotation_angles = {90, 180, 270};
            return;
        }
        if (at(token_type::integer)) {
            r.rotation_angles.push_back(eat().int_val);
            return;
        }
        if (accept(token_type::lbrace)) {   // rotation={a, b, …} — the §3 set brackets
            bool got = false;
            while (at(token_type::integer)) {
                r.rotation_angles.push_back(eat().int_val);
                got = true;
                if (!accept(token_type::comma)) break;
            }
            if (!got) error_at(peek(), "empty rotation set");
            expect(token_type::rbrace, "'}'");
            return;
        }
        error_at(peek(), "invalid rotation value; expected none, all, an angle, or {angles}");
        eat_bad();
    }

    void parse_rule_attrs(rule_decl& r) {
        do {
            if (accept(token_type::kw_symmetry)) {
                if (!expect(token_type::equals, "'='")) return;
                // value text captured raw; validated in sema
                if (at(token_type::kw_none) || at(token_type::kw_horizontal) ||
                    at(token_type::kw_vertical) || at(token_type::kw_all) ||
                    at(token_type::ident))
                    r.symmetry = eat().text;
                else {
                    error_at(peek(), "invalid symmetry value");
                    eat_bad();
                }
            } else if (accept(token_type::kw_rotation)) {
                if (!expect(token_type::equals, "'='")) return;
                parse_rotation_value(r);
            } else {
                error_at(peek(), "unknown rule attribute '" + peek().text +
                         "' (expected symmetry or rotation)");
                eat_bad();
            }
        } while (accept(token_type::comma));
        expect(token_type::rparen, "')'");
    }

    // ── match side: a pattern, or { all p1 p2 … } ────────────────────────────

    bool parse_match_side(std::vector<pattern>& lhs) {
        if (at(token_type::lbrace)) {
            source_loc bl = loc();
            eat();   // '{'
            if (at(token_type::kw_any) || at(token_type::kw_ordered)) {
                error_at(peek(), "'{ " + peek().text +
                         " }' is not supported on the match side");
                recover_to(token_type::rbrace);
                return false;
            }
            if (!expect(token_type::kw_all, "'all'")) {
                recover_to(token_type::rbrace);
                return false;
            }
            skip_seps();
            while (!at(token_type::rbrace) && !at_end()) {
                pattern p;
                if (!parse_pattern(p)) { eat_bad(); skip_seps(); continue; }
                lhs.push_back(std::move(p));
                skip_seps();
            }
            expect(token_type::rbrace, "'}'");
            if (lhs.size() < 2)
                diags.error(file, bl.line, bl.col,
                            "a combinator block requires two or more items");
            return !lhs.empty();
        }
        pattern p;
        if (!parse_pattern(p)) return false;
        lhs.push_back(std::move(p));
        return true;
    }

    // ── write side: a recursive write term (spec §5.2) ───────────────────────

    bool parse_write_term(write_term& t, bool weight_allowed) {
        t.loc = loc();

        // optional (weight=N) prefix — legal only as an { any } item
        if (at(token_type::lparen) && peek(1).is(token_type::kw_weight)) {
            source_loc wl = loc();
            eat(); eat();   // '(' 'weight'
            if (!expect(token_type::equals, "'='")) return false;
            if (!expect(token_type::integer, "a weight")) return false;
            t.weight = (int)toks[pos - 1].int_val;
            if (!expect(token_type::rparen, "')'")) return false;
            if (!weight_allowed)
                diags.error(file, wl.line, wl.col,
                            "(weight=N) is only allowed on '{ any }' items");
        }

        if (!at(token_type::lbrace)) {   // leaf
            t.what = write_term::kind::leaf;
            return parse_pattern(t.pat);
        }

        source_loc bl = loc();
        eat();   // '{'
        bool is_any;
        if (accept(token_type::kw_all))      is_any = false;
        else if (accept(token_type::kw_any)) is_any = true;
        else {
            if (at(token_type::kw_ordered))   // 'ordered' is body-level only (§7.3 #29)
                error_at(peek(), "'ordered' is a body-level combinator; it cannot "
                         "appear on the write side");
            else
                error_at(peek(), "expected 'all' or 'any' after '{' on the write side");
            recover_to(token_type::rbrace);
            return false;
        }
        t.what = is_any ? write_term::kind::any : write_term::kind::all;
        skip_seps();
        while (!at(token_type::rbrace) && !at_end()) {
            write_term item;
            if (!parse_write_term(item, /*weight_allowed=*/is_any)) {
                eat_bad();
                skip_seps();
                continue;
            }
            t.items.push_back(std::move(item));
            skip_seps();
        }
        expect(token_type::rbrace, "'}'");
        if (t.items.size() < 2)
            diags.error(file, bl.line, bl.col,
                        "a combinator block requires two or more items");
        return !t.items.empty();
    }

    bool parse_pair(rule_pair& pr) {
        pr.loc = loc();
        if (!parse_match_side(pr.lhs)) return false;
        skip_newlines();
        if (!expect(token_type::arrow, "'=>'")) return false;
        skip_newlines();
        return parse_write_term(pr.rhs, /*weight_allowed=*/false);
    }

    void parse_rule(ast_file& out) {
        rule_decl r;
        r.loc = loc();
        eat();   // 'rule'
        if (!expect(token_type::ident, "a rule name")) return;
        r.name = toks[pos - 1].text;
        if (accept(token_type::lparen)) parse_rule_attrs(r);
        if (!expect(token_type::lbrace, "'{'")) return;
        skip_newlines();

        // Body-level combinator: `rule r { all pair pair … }` — multiple
        // independent sub-rules; `ordered` makes declaration order a priority
        // (spec §5.2). A single-pair body has no combinator.
        if (at(token_type::kw_all) || at(token_type::kw_any) ||
            at(token_type::kw_ordered)) {
            r.body = at(token_type::kw_all)     ? body_combinator::all
                   : at(token_type::kw_ordered) ? body_combinator::ordered
                                                : body_combinator::any;
            source_loc bl = loc();
            eat();
            skip_newlines();
            while (!at(token_type::rbrace) && !at_end()) {
                rule_pair pr;
                if (!parse_pair(pr)) { recover_to(token_type::rbrace); break; }
                r.pairs.push_back(std::move(pr));
                skip_newlines();
            }
            if (r.pairs.size() < 2)
                diags.error(file, bl.line, bl.col,
                            "a body combinator requires two or more sub-rules");
        } else {
            rule_pair pr;
            if (!parse_pair(pr)) { recover_to(token_type::rbrace); return; }
            r.pairs.push_back(std::move(pr));
            skip_newlines();
        }
        expect(token_type::rbrace, "'}'");
        out.rules.push_back(std::move(r));
    }

    // ── program ──────────────────────────────────────────────────────────────

    // policy = snapshot | incremental | stabilize; an unknown value is
    // recorded raw and rejected in sema (§7.3 #30).
    void parse_policy_arg(program_stmt& s) {
        eat();   // 'policy'
        if (!expect(token_type::equals, "'='")) return;
        if (accept(token_type::kw_snapshot))         s.pol = exec_policy::snapshot;
        else if (accept(token_type::kw_incremental)) s.pol = exec_policy::incremental;
        else if (accept(token_type::kw_stabilize))   s.pol = exec_policy::stabilize;
        else {
            s.bad_policy = true;
            s.policy_raw = peek().text;
            eat_bad();
        }
    }

    void parse_apply(program_stmt& s) {
        if (at(token_type::kw_one) || at(token_type::kw_all)) {
            s.strat = at(token_type::kw_one) ? strategy::one : strategy::all;
            eat();
            if (accept(token_type::lparen)) {   // one/all take only a policy
                if (at(token_type::kw_policy)) parse_policy_arg(s);
                else { error_at(peek(), "expected 'policy=' here"); eat_bad(); }
                expect(token_type::rparen, "')'");
            }
        } else {   // 'some' '(' max=N | percent=P (',' policy=…)? ')'
            eat();
            s.strat = strategy::some;
            if (!expect(token_type::lparen, "'('")) return;
            if (accept(token_type::kw_max)) {
                if (!expect(token_type::equals, "'='")) return;
                if (!expect(token_type::integer, "a count")) return;
                s.max_count = (int)toks[pos - 1].int_val;
            } else if (accept(token_type::kw_percent)) {
                s.is_percent = true;
                if (!expect(token_type::equals, "'='")) return;
                if (!expect(token_type::integer, "a percentage")) return;
                s.percent = (int)toks[pos - 1].int_val;
            } else {
                error_at(peek(), "expected 'max=' or 'percent='");
                recover_to(token_type::rparen);
                return;
            }
            if (accept(token_type::comma)) {
                if (at(token_type::kw_policy)) parse_policy_arg(s);
                else { error_at(peek(), "expected 'policy=' here"); eat_bad(); }
            }
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
            if (at(token_type::kw_one) || at(token_type::kw_all) ||
                at(token_type::kw_some)) {
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
