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

    // Two tokens touch (no whitespace between) — bare mask cells `a|b`, `!a`
    // are whitespace-free; a spaced `a | b` is not one cell (spec §2.5).
    bool adjacent(token const& a, token const& b) const {
        return a.line == b.line && a.col + (int)a.text.size() == b.col;
    }

    // ── expressions (spec §5.8; C precedence, lowest to highest) ─────────────

    expr_ptr make_expr(expr_kind k) {
        auto e = std::make_unique<expr>();
        e->kind = k;
        e->loc = loc();
        return e;
    }
    expr_ptr make_binary(expr_kind k, expr_ptr a, expr_ptr b, source_loc l) {
        auto e = std::make_unique<expr>();
        e->kind = k;
        e->loc = l;
        e->args.push_back(std::move(a));
        e->args.push_back(std::move(b));
        return e;
    }

    expr_ptr parse_primary() {
        if (at(token_type::integer)) {
            auto e = make_expr(expr_kind::int_lit);
            e->int_val = eat().int_val;
            return e;
        }
        if (at(token_type::dot)) {
            auto e = make_expr(expr_kind::empty_lit);
            eat();
            return e;
        }
        if (at(token_type::lparen)) {
            eat();
            auto e = parse_expr();
            expect(token_type::rparen, "')'");
            return e;
        }
        // keywords usable as names in expressions: max is also a §6 keyword
        if (at(token_type::ident) || at(token_type::kw_max)) {
            bool is_call = peek(1).is(token_type::lparen);
            auto e = make_expr(is_call ? expr_kind::call : expr_kind::ident);
            e->ident = eat().text;
            if (is_call) {
                eat();   // '('
                if (!at(token_type::rparen)) {
                    e->args.push_back(parse_expr());
                    while (accept(token_type::comma))
                        e->args.push_back(parse_expr());
                }
                expect(token_type::rparen, "')'");
            }
            return e;
        }
        error_at(peek(), "expected an expression");
        eat_bad();
        return make_expr(expr_kind::int_lit);
    }

    expr_ptr parse_unary() {
        if (at(token_type::minus) || at(token_type::bang)) {
            auto k = at(token_type::minus) ? expr_kind::neg : expr_kind::not_;
            source_loc l = loc();
            eat();
            auto e = std::make_unique<expr>();
            e->kind = k;
            e->loc = l;
            e->args.push_back(parse_unary());
            return e;
        }
        return parse_primary();
    }

    expr_ptr parse_mul() {
        auto e = parse_unary();
        while (at(token_type::star) || at(token_type::slash)) {
            auto k = at(token_type::star) ? expr_kind::mul : expr_kind::div_;
            source_loc l = loc(); eat();
            e = make_binary(k, std::move(e), parse_unary(), l);
        }
        return e;
    }
    expr_ptr parse_add() {
        auto e = parse_mul();
        while (at(token_type::plus) || at(token_type::minus)) {
            auto k = at(token_type::plus) ? expr_kind::add : expr_kind::sub;
            source_loc l = loc(); eat();
            e = make_binary(k, std::move(e), parse_mul(), l);
        }
        return e;
    }
    expr_ptr parse_rel() {
        auto e = parse_add();
        while (at(token_type::lt) || at(token_type::le) ||
               at(token_type::gt) || at(token_type::ge)) {
            auto k = at(token_type::lt) ? expr_kind::lt
                   : at(token_type::le) ? expr_kind::le
                   : at(token_type::gt) ? expr_kind::gt : expr_kind::ge;
            source_loc l = loc(); eat();
            e = make_binary(k, std::move(e), parse_add(), l);
        }
        return e;
    }
    expr_ptr parse_eq() {
        auto e = parse_rel();
        while (at(token_type::eq_eq) || at(token_type::bang_eq)) {
            auto k = at(token_type::eq_eq) ? expr_kind::eq : expr_kind::ne;
            source_loc l = loc(); eat();
            e = make_binary(k, std::move(e), parse_rel(), l);
        }
        return e;
    }
    expr_ptr parse_bor() {   // '|' tag union
        auto e = parse_eq();
        while (at(token_type::pipe)) {
            source_loc l = loc(); eat();
            e = make_binary(expr_kind::bit_or, std::move(e), parse_eq(), l);
        }
        return e;
    }
    expr_ptr parse_and() {
        auto e = parse_bor();
        while (at(token_type::amp_amp)) {
            source_loc l = loc(); eat();
            e = make_binary(expr_kind::and_, std::move(e), parse_bor(), l);
        }
        return e;
    }
    expr_ptr parse_expr() {
        auto e = parse_and();
        while (at(token_type::pipe_pipe)) {
            source_loc l = loc(); eat();
            e = make_binary(expr_kind::or_, std::move(e), parse_and(), l);
        }
        return e;
    }

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
            if (!expect(token_type::ident, "a tag value name")) { eat_bad(); skip_seps(); continue; }
            source_loc name_loc = {toks[pos - 1].line, toks[pos - 1].col};
            std::string name = toks[pos - 1].text;
            if (at(token_type::equals)) {   // named union: blocker = wall | door (§3)
                tag_union u;
                u.loc = name_loc;
                u.name = std::move(name);
                eat();   // '='
                do {
                    if (!expect(token_type::ident, "a union member")) break;
                    u.members.push_back(toks[pos - 1].text);
                } while (accept(token_type::pipe));
                d.unions.push_back(std::move(u));
            } else {
                d.values.push_back({name_loc, std::move(name)});
            }
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
            l.loc = {toks[pos - 1].line, toks[pos - 1].col};
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

    // ── params (spec §4.2) ───────────────────────────────────────────────────

    void parse_params(ast_file& out) {
        if (out.has_params)
            error_at(peek(), "only one 'params' block per file");
        out.has_params = true;
        eat();   // 'params'
        if (!expect(token_type::lbrace, "'{'")) return;
        skip_seps();
        while (!at(token_type::rbrace) && !at_end()) {
            param_decl p;
            p.loc = loc();
            if (!expect(token_type::ident, "a param name")) { eat_bad(); skip_seps(); continue; }
            p.name = toks[pos - 1].text;
            if (accept(token_type::colon)) {   // input: name ':' 'number' '=' expr
                if (!expect(token_type::kw_number, "'number'")) { skip_seps(); continue; }
                if (accept(token_type::equals))
                    p.value = parse_expr();
                // a missing default is §7.3 #27 — reported in sema with p.loc
            } else {                           // derived: name '=' expr
                p.is_derived = true;
                if (!expect(token_type::equals, "':' or '='")) { skip_seps(); continue; }
                p.value = parse_expr();
            }
            out.params.push_back(std::move(p));
            skip_seps();
        }
        expect(token_type::rbrace, "'}'");
    }

    // ── patterns ─────────────────────────────────────────────────────────────

    // One whitespace-free mask atom: IDENT or !IDENT (adjacent).
    bool parse_mask_atom(cell& c) {
        mask_atom a;
        a.loc = loc();
        if (at(token_type::bang)) {
            token const& b = eat();
            a.negate = true;
            if (!at(token_type::ident) || !adjacent(b, peek())) {
                error_at(peek(), "expected a tag value right after '!'");
                return false;
            }
        }
        if (!at(token_type::ident)) {
            error_at(peek(), "expected a tag value");
            return false;
        }
        a.name = eat().text;
        c.atoms.push_back(std::move(a));
        return true;
    }

    bool parse_cell(cell& c) {
        c.loc = loc();
        if (accept(token_type::star)) { c.kind = cell_kind::any;   return true; }
        if (accept(token_type::dot))  { c.kind = cell_kind::empty; return true; }
        if (at(token_type::integer))  { c.kind = cell_kind::number; c.number = eat().int_val; return true; }
        if (at(token_type::lparen)) {   // '( expr )' — computed cell (§5.8)
            eat();
            c.kind = cell_kind::expr_cell;
            c.value = parse_expr();
            return expect(token_type::rparen, "')'");
        }
        if (at(token_type::ident) || at(token_type::bang)) {
            c.kind = cell_kind::tag_mask;
            if (!parse_mask_atom(c)) return false;
            // whitespace-free unions: a|b|c (each '|' adjacent on both sides)
            while (at(token_type::pipe) && adjacent(toks[pos - 1], peek())) {
                token const& bar = eat();
                if (!adjacent(bar, peek())) {
                    error_at(peek(), "a bare mask cell is whitespace-free; "
                             "wrap a spaced union in parentheses");
                    return false;
                }
                if (!parse_mask_atom(c)) return false;
            }
            return true;
        }
        error_at(peek(), "expected a pattern cell (*, ., a tag value, an integer, "
                 "or a parenthesized expression)");
        return false;
    }

    bool parse_pattern(pattern& p) {
        p.loc = loc();
        if (at(token_type::kw_where)) {   // where pseudo-layer (§5.9)
            p.is_where = true;
            eat();
        } else {
            if (!expect(token_type::ident, "a grid name")) return false;
            p.grid_loc = {toks[pos - 1].line, toks[pos - 1].col};
            p.grid = toks[pos - 1].text;
        }
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
            (void)bl;
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
            // single-item blocks are allowed (relaxed from MGSL's ≥2 rule —
            // generated/templated rules often produce them)
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
        if (t.items.empty())
            diags.error(file, bl.line, bl.col, "empty combinator block");
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
            skip_seps();   // pairs separate by newline or comma (list_sep)
            while (!at(token_type::rbrace) && !at_end()) {
                rule_pair pr;
                if (!parse_pair(pr)) { recover_to(token_type::rbrace); break; }
                r.pairs.push_back(std::move(pr));
                skip_seps();
            }
            if (r.pairs.empty())
                diags.error(file, bl.line, bl.col, "empty rule body");
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
        if (expect(token_type::ident, "a rule name")) {
            s.rule_name_loc = {toks[pos - 1].line, toks[pos - 1].col};
            s.rule_name = toks[pos - 1].text;
        }
    }

    // One op-call argument value: INTEGER (a leading '-' is accepted so value
    // constraints stay semantic checks), IDENT, or '(' expr ')'.
    bool parse_op_arg_value(op_arg& a) {
        if (accept(token_type::minus)) {
            if (!expect(token_type::integer, "an integer")) return false;
            a.int_val = -toks[pos - 1].int_val;
            return true;
        }
        if (at(token_type::integer)) {
            a.int_val = eat().int_val;
            return true;
        }
        if (at(token_type::lparen)) {
            eat();
            a.what = op_arg::kind::expr;
            a.value = parse_expr();
            return expect(token_type::rparen, "')'");
        }
        // bare identifier — a grid name, tag value, or enum word; the mirror
        // axis words lex as keywords, so they are accepted explicitly
        if (at(token_type::ident) || at(token_type::kw_horizontal) ||
            at(token_type::kw_vertical)) {
            a.what = op_arg::kind::ident;
            a.ident = eat().text;
            return true;
        }
        error_at(peek(), "expected an argument (an integer, a name, or a "
                 "parenthesized expression)");
        return false;
    }

    void parse_op_call(program_stmt& s) {
        s.what = program_stmt::kind::op_call;
        s.op_name = eat().text;   // IDENT
        if (!expect(token_type::lparen, "'('")) return;
        skip_newlines();   // long calls (path) may spread over lines
        while (!at(token_type::rparen) && !at_end()) {
            op_arg a;
            a.loc = loc();
            // named argument: IDENT '=' value
            if (at(token_type::ident) && peek(1).is(token_type::equals)) {
                a.name = eat().text;
                eat();   // '='
            }
            if (!parse_op_arg_value(a)) { eat_bad(); skip_newlines(); continue; }
            s.op_args.push_back(std::move(a));
            if (!accept(token_type::comma)) break;
            skip_newlines();
        }
        skip_newlines();
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
            // optional `when (expr)` guard (§6)
            if (accept(token_type::kw_when)) {
                if (expect(token_type::lparen, "'('")) {
                    s.guard = parse_expr();
                    expect(token_type::rparen, "')'");
                }
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
            case token_type::kw_params:  parse_params(out);  break;
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
