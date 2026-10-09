#include "parser.hpp"
#include "lexer.hpp"

namespace ls {

namespace {

struct parser {
    std::vector<token> const& toks;
    std::string_view          file;
    diagnostics&              diags;
    size_t                    pos{0};
    int                       mod{0};   // module id stamped into every loc
    // Newlines are whitespace everywhere except between a pattern's brackets,
    // where they separate rows (spec sections 2, 5.3). Outside a grid the
    // newline tokens are invisible to peek/eat.
    bool                      in_grid{false};

    size_t skip_nl(size_t p) const {
        if (!in_grid)
            while (p + 1 < toks.size() && toks[p].is(token_type::newline)) ++p;
        return p;
    }
    token const& peek(int off = 0) const {
        size_t p = skip_nl(pos);
        for (int i = 0; i < off; ++i) p = skip_nl(p + 1);
        return p < toks.size() ? toks[p] : toks.back();
    }
    token const& eat() {
        pos = skip_nl(pos);
        token const& t = toks[pos];
        if (pos + 1 < toks.size()) ++pos;
        return t;
    }
    bool at(token_type t) const  { return peek().is(t); }
    bool at_end() const          { return at(token_type::end); }
    bool accept(token_type t)    { if (at(t)) { eat(); return true; } return false; }

    // Contextual names (spec section 2.4): lexed as ident, matched by text.
    bool at_word(std::string_view w) const { return at(token_type::ident) && peek().text == w; }
    bool accept_word(std::string_view w)   { if (at_word(w)) { eat(); return true; } return false; }

    void error_at(token const& t, std::string msg) {
        diags.error(file, t.line, t.col, std::move(msg));
    }
    bool expect(token_type t, char const* what) {
        if (accept(t)) return true;
        if (t == token_type::ident && is_keyword(peek().type))
            error_at(peek(), std::string("expected ") + what + ", got '" + peek().text +
                     "' - a reserved keyword cannot be used as a name");
        else
            error_at(peek(), std::string("expected ") + what +
                     (peek().text.empty() ? "" : ", got '" + peek().text + "'"));
        return false;
    }

    void skip_newlines() { while (at(token_type::newline)) eat(); }   // grid rows only
    // list_sep ::= ','  - optional between list items (section 3)
    void list_sep()      { accept(token_type::comma); }

    source_loc loc() const { return {peek().line, peek().col, mod}; }
    // location of the token just consumed
    source_loc prev_loc() const { return {toks[pos - 1].line, toks[pos - 1].col, mod}; }

    // Two tokens touch (no whitespace between) — bare mask cells `a|b`, `!a`
    // are whitespace-free; a spaced `a | b` is not one cell (spec section 2.5).
    bool adjacent(token const& a, token const& b) const {
        return a.line == b.line && a.col + (int)a.text.size() == b.col;
    }

    // ── expressions (spec section 5.8; C precedence, lowest to highest) ─────────────

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
        if (at(token_type::ident)) {
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
        while (!at(token_type::rbrace) && !at_end()) {
            if (!expect(token_type::ident, "a tag value name")) { eat_bad(); list_sep(); continue; }
            source_loc name_loc = prev_loc();
            std::string name = toks[pos - 1].text;
            if (at(token_type::equals)) {   // named union: blocker = wall | door (section 3)
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
            list_sep();
        }
        expect(token_type::rbrace, "'}'");
        out.tags.push_back(std::move(d));
    }

    void parse_layers(ast_file& out) {
        if (out.has_layers)   // section 7.3, check 43
            error_at(peek(), "only one 'layers' block per module");
        out.layers.loc = loc();
        out.has_layers = true;
        eat();   // 'layers'
        if (!expect(token_type::lbrace, "'{'")) return;
        while (!at(token_type::rbrace) && !at_end()) {
            layer_decl l;
            if (!expect(token_type::ident, "a grid name")) { eat_bad(); list_sep(); continue; }
            l.loc = prev_loc();
            l.name = toks[pos - 1].text;
            if (!expect(token_type::colon, "':'"))         { list_sep(); continue; }
            if (!expect(token_type::kw_grid, "'grid'"))    { list_sep(); continue; }
            if (!expect(token_type::kw_of, "'of'"))        { list_sep(); continue; }
            if (accept(token_type::kw_number))       l.type = "number";
            else if (expect(token_type::ident, "a tagset name or 'number'"))
                l.type = toks[pos - 1].text;
            else { list_sep(); continue; }
            out.layers.layers.push_back(std::move(l));
            list_sep();
        }
        expect(token_type::rbrace, "'}'");
    }

    // ── params (spec section 4.2) ───────────────────────────────────────────────────

    void parse_params(ast_file& out) {
        if (out.has_params)
            error_at(peek(), "only one 'params' block per module");
        out.has_params = true;
        eat();   // 'params'
        if (!expect(token_type::lbrace, "'{'")) return;
        while (!at(token_type::rbrace) && !at_end()) {
            param_decl p;
            p.loc = loc();
            if (!expect(token_type::ident, "a param name")) { eat_bad(); list_sep(); continue; }
            p.name = toks[pos - 1].text;
            if (accept(token_type::colon)) {   // input: name ':' 'number' '=' expr
                if (!expect(token_type::kw_number, "'number'")) { list_sep(); continue; }
                if (accept(token_type::equals))
                    p.value = parse_expr();
                // a missing default is section 7.3, check 27 — reported in sema with p.loc
            } else {                           // derived: name '=' expr
                p.is_derived = true;
                if (!expect(token_type::equals, "':' or '='")) { list_sep(); continue; }
                p.value = parse_expr();
            }
            out.params.push_back(std::move(p));
            list_sep();
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
        if (at(token_type::lparen)) {   // '( expr )' — computed cell (section 5.8)
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
        if (at(token_type::kw_where)) {   // where pseudo-layer (section 5.9)
            p.is_where = true;
            eat();
        } else {
            if (!expect(token_type::ident, "a grid name")) return false;
            p.grid_loc = prev_loc();
            p.grid = toks[pos - 1].text;
        }
        if (!expect(token_type::lbracket, "'['")) return false;
        in_grid = true;    // newlines separate rows until ']'
        skip_newlines();   // '[' may be followed by a newline before the first row
        // '=>' and '}' can never be cells: an unclosed pattern stops there and
        // reports the missing ']' instead of a cell error per token
        auto body_ends = [&] {
            return at(token_type::rbracket) || at(token_type::arrow) ||
                   at(token_type::rbrace) || at_end();
        };
        // row_sep ::= ',' WS* NEWLINE? | NEWLINE. A comma inside '( ... )' is
        // consumed by the cell's expression, so only a depth-0 comma gets here.
        while (!body_ends()) {
            std::vector<cell> row;
            while (!at(token_type::newline) && !at(token_type::comma) && !body_ends()) {
                cell c;
                if (!parse_cell(c)) { eat_bad(); continue; }
                row.push_back(std::move(c));
            }
            if (at(token_type::comma)) {
                token const& comma = eat();
                if (row.empty())
                    error_at(comma, "empty pattern row before ','");
                accept(token_type::newline);   // ',' + newline is a single break
                skip_newlines();
                if (body_ends())
                    error_at(peek(), "expected a pattern row after ','");
            }
            if (!row.empty()) p.cells.push_back(std::move(row));
            skip_newlines();
        }
        in_grid = false;
        if (!expect(token_type::rbracket, "']'")) return false;
        p.rows = (int)p.cells.size();
        p.cols = p.cells.empty() ? 0 : (int)p.cells[0].size();
        return true;
    }

    // ── rule attributes: (symmetry=…, rotation=…) ────────────────────────────

    void parse_rotation_value(rule_decl& r) {
        if (accept_word("none")) return;
        if (accept(token_type::kw_all)) {
            r.rotation_angles = {90, 180, 270};
            return;
        }
        if (at(token_type::integer)) {
            r.rotation_angles.push_back(eat().int_val);
            return;
        }
        if (accept(token_type::lbrace)) {   // rotation={a, b, …} — the section 3 set brackets
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

    // attr_value ::= IDENT | INTEGER | 'all' | '{' INTEGER (',' INTEGER)* '}'
    void skip_attr_value() {
        if (accept(token_type::lbrace)) { recover_to(token_type::rbrace); return; }
        if (at(token_type::ident) || at(token_type::integer) || at(token_type::kw_all))
            eat();
    }

    void parse_rule_attrs(rule_decl& r) {
        do {
            if (accept_word("symmetry")) {
                if (!expect(token_type::equals, "'='")) return;
                // value text captured raw; validated in sema
                if (at(token_type::kw_all) || at(token_type::ident))
                    r.symmetry = eat().text;
                else {
                    error_at(peek(), "invalid symmetry value");
                    eat_bad();
                }
            } else if (accept_word("rotation")) {
                if (!expect(token_type::equals, "'='")) return;
                parse_rotation_value(r);
            } else {
                // generic `attr ::= IDENT '=' attr_value` (section 5.1): parse it
                // whole so an unknown name is one diagnostic, not a cascade
                error_at(peek(), "unknown rule attribute '" + peek().text +
                         "' (expected symmetry or rotation)");
                eat_bad();
                if (accept(token_type::equals)) skip_attr_value();
            }
        } while (accept(token_type::comma));
        expect(token_type::rparen, "')'");
    }

    // ── match side: pattern+ (spec section 5.2) ──────────────────────────────────

    bool at_pattern_head() const {
        return (at(token_type::ident) || at(token_type::kw_where)) &&
               peek(1).is(token_type::lbracket);
    }

    // Consecutive patterns before '=>' conjoin; there is no match-side
    // combinator and no comma between the patterns. Braces may group them for
    // the reader: `{ p1 p2 } => ...` means `p1 p2 => ...`.
    bool parse_match_side(std::vector<pattern>& lhs) {
        if (!at(token_type::lbrace)) return parse_match_patterns(lhs);
        eat();   // '{'
        if (at(token_type::kw_all) || at(token_type::kw_any) || at(token_type::kw_ordered)) {
            error_at(peek(), "match-side braces only group patterns; they take no '" +
                     peek().text + "'");
            eat();
        }
        if (at(token_type::rbrace)) {
            error_at(peek(), "empty match-side group");
            eat();
            return false;
        }
        if (!parse_match_patterns(lhs)) { recover_to(token_type::rbrace); return false; }
        return expect(token_type::rbrace, "'}'");
    }

    bool parse_match_patterns(std::vector<pattern>& lhs) {
        do {
            pattern p;
            if (!parse_pattern(p)) return false;
            lhs.push_back(std::move(p));
            if (at(token_type::comma)) {
                error_at(peek(), "patterns on the match side are not separated by commas");
                eat();
            }
        } while (at_pattern_head());
        return true;
    }

    // ── write side: a recursive write term (spec section 5.2) ───────────────────────

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
            if (at(token_type::kw_ordered))   // 'ordered' is body-level only (section 7.3, check 29)
                error_at(peek(), "'ordered' is a body-level combinator; it cannot "
                         "appear on the write side");
            else
                error_at(peek(), "expected 'all' or 'any' after '{' on the write side");
            recover_to(token_type::rbrace);
            return false;
        }
        t.what = is_any ? write_term::kind::any : write_term::kind::all;
        while (!at(token_type::rbrace) && !at_end()) {
            write_term item;
            if (!parse_write_term(item, /*weight_allowed=*/is_any)) {
                eat_bad();
                list_sep();
                continue;
            }
            t.items.push_back(std::move(item));
            list_sep();
        }
        expect(token_type::rbrace, "'}'");
        if (t.items.empty())
            diags.error(file, bl.line, bl.col, "empty combinator block");
        return !t.items.empty();
    }

    bool parse_pair(rule_pair& pr) {
        pr.loc = loc();
        if (!parse_match_side(pr.lhs)) return false;
        if (!expect(token_type::arrow, "'=>'")) return false;
        return parse_write_term(pr.rhs, /*weight_allowed=*/false);
    }

    void parse_rule(ast_file& out) {
        rule_decl r;
        r.loc = loc();
        eat();   // 'rule'
        if (!expect(token_type::ident, "a rule name")) return;
        r.name_loc = prev_loc();
        r.name = toks[pos - 1].text;
        if (parse_rule_rest(r)) out.rules.push_back(std::move(r));
    }

    // rule_attrs? '{' rule_body '}' - shared by named and inline rules (section 5.1)
    bool parse_rule_rest(rule_decl& r) {
        if (accept(token_type::lparen)) parse_rule_attrs(r);
        if (!expect(token_type::lbrace, "'{'")) return false;

        // Body-level combinator: `rule r { all pair pair … }` — multiple
        // independent sub-rules; `ordered` makes declaration order a priority
        // (spec section 5.2). A single-pair body has no combinator.
        if (at(token_type::kw_all) || at(token_type::kw_any) ||
            at(token_type::kw_ordered)) {
            r.body = at(token_type::kw_all)     ? body_combinator::all
                   : at(token_type::kw_ordered) ? body_combinator::ordered
                                                : body_combinator::any;
            source_loc bl = loc();
            eat();
            while (!at(token_type::rbrace) && !at_end()) {
                rule_pair pr;
                if (!parse_pair(pr)) { recover_to(token_type::rbrace); break; }
                r.pairs.push_back(std::move(pr));
                list_sep();   // optional comma between pairs (section 3)
            }
            if (r.pairs.empty())
                diags.error(file, bl.line, bl.col, "empty rule body");
        } else {
            rule_pair pr;
            if (!parse_pair(pr)) { recover_to(token_type::rbrace); return false; }
            r.pairs.push_back(std::move(pr));
        }
        expect(token_type::rbrace, "'}'");
        return true;
    }

    // ── program ──────────────────────────────────────────────────────────────

    bool at_mode() const {
        return at(token_type::kw_once) || at(token_type::kw_scatter) ||
               at(token_type::kw_everywhere) || at(token_type::kw_grow) ||
               at(token_type::kw_settle);
    }

    // Is the '(' at peek(off) closed by a ')' directly followed by '%'?
    bool paren_then_percent(int off) const {
        int depth = 0;
        for (;; ++off) {
            token const& t = peek(off);
            if (t.is(token_type::end)) return false;
            if (t.is(token_type::lparen)) ++depth;
            else if (t.is(token_type::rparen) && --depth == 0)
                return peek(off + 1).is(token_type::percent);
        }
    }

    // count         ::= '(' expr ')'                      (grow, settle)
    // scatter_count ::= expr | percent
    // percent       ::= (INTEGER | IDENT | '(' expr ')') '%'
    // The '%' binds to an atom only, so it always applies to the whole count:
    // `n * 2%` is an error, `(n * 2)%` a percentage (section 6).
    void parse_count(program_stmt& s, bool percent_ok) {
        eat();   // '('
        s.count_loc = loc();
        bool atom_percent =
            ((at(token_type::integer) || at(token_type::ident)) &&
             peek(1).is(token_type::percent)) ||
            (at(token_type::lparen) && paren_then_percent(0));
        s.count = parse_expr();
        if (at(token_type::percent)) {
            if (!percent_ok)
                error_at(peek(), "only 'scatter' takes a percentage");
            else if (!atom_percent)
                error_at(peek(), "'%' follows a number, a param name, or a parenthesized "
                         "expression; write '(...)%' for a computed percentage");
            else
                s.count_percent = true;
            eat();
        }
        expect(token_type::rparen, "')'");
    }

    // `(` IDENT `=` begins rule attributes, never a count (section 6)
    bool at_rule_attrs() const {
        return at(token_type::lparen) && peek(1).is(token_type::ident) &&
               peek(2).is(token_type::equals);
    }

    // apply_stmt ::= mode target   (section 6)
    // mode   ::= 'once' | 'scatter' '(' scatter_count ')' | 'everywhere'
    //          | 'grow' ('(' expr ')')? | 'settle' ('(' expr ')')?
    // target ::= IDENT | rule_attrs? '{' rule_body '}'
    void parse_apply(program_stmt& s) {
        token const& m = eat();
        switch (m.type) {
        case token_type::kw_once:       s.mode = apply_mode::once;       break;
        case token_type::kw_scatter:    s.mode = apply_mode::scatter;    break;
        case token_type::kw_everywhere: s.mode = apply_mode::everywhere; break;
        case token_type::kw_grow:       s.mode = apply_mode::grow;       break;
        default:                        s.mode = apply_mode::settle;     break;
        }
        bool counted = s.mode == apply_mode::scatter || s.mode == apply_mode::grow ||
                       s.mode == apply_mode::settle;
        if (at(token_type::lparen) && !at_rule_attrs()) {
            if (counted) {
                parse_count(s, s.mode == apply_mode::scatter);
            } else {
                error_at(peek(), "'" + m.text + "' takes no count" +
                         (s.mode == apply_mode::once ? "; use 'grow(N)' for N steps"
                                                     : "; use 'scatter(N)' for up to N applications"));
                recover_to(token_type::rparen);
            }
        } else if (s.mode == apply_mode::scatter) {
            error_at(peek(), "'scatter' takes a count, e.g. 'scatter(5)' or 'scatter(50%)'");
        }
        parse_target(s);
    }

    void parse_target(program_stmt& s) {
        if (at(token_type::lbrace) || at_rule_attrs()) {   // an inline rule
            auto r = std::make_unique<rule_decl>();
            r->loc = loc();
            r->name_loc = r->loc;
            if (parse_rule_rest(*r)) s.inline_rule = std::move(r);
            return;
        }
        if (expect(token_type::ident, "a rule or sequence name, or an inline rule '{ ... }'")) {
            s.rule_name_loc = prev_loc();
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
        // bare identifier — a grid name, tag value, or enum word
        if (at(token_type::ident)) {
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
        while (!at(token_type::rparen) && !at_end()) {
            op_arg a;
            a.loc = loc();
            // named argument: IDENT '=' value
            if (at(token_type::ident) && peek(1).is(token_type::equals)) {
                a.name = eat().text;
                eat();   // '='
            }
            if (!parse_op_arg_value(a)) { eat_bad(); continue; }
            s.op_args.push_back(std::move(a));
            if (!accept(token_type::comma)) break;
        }
        expect(token_type::rparen, "')'");
    }

    // statement_list (section 6) - the body of `program` and of every `sequence`.
    // `where` names the enclosing sequence for diagnostics ("" = program).
    void parse_statement_list(std::vector<program_stmt>& out, std::string const& where) {
        while (!at(token_type::rbrace) && !at_end()) {
            program_stmt s;
            s.loc = loc();
            if (at_mode()) {
                parse_apply(s);
            } else if (at(token_type::ident) && peek(1).is(token_type::lparen)) {
                parse_op_call(s);
            } else {
                if (where.empty())
                    error_at(peek(), "expected a statement (a mode + rule, or an operation call)");
                else
                    error_at(peek(), "expected a statement in sequence '" + where +
                             "'; a rule is applied with a mode, by name or inline, "
                             "e.g. 'everywhere fill' or 'everywhere { g[.] => g[a] }'");
                // one diagnostic per bad line, not one per token
                skip_line(peek().line);
                continue;
            }
            // optional `when (expr)` guard (section 6)
            if (accept(token_type::kw_when)) {
                if (expect(token_type::lparen, "'('")) {
                    s.guard = parse_expr();
                    expect(token_type::rparen, "')'");
                }
            }
            out.push_back(std::move(s));
        }
    }

    // use_decl ::= 'use' STRING   (section 2.6) - only at the head of a file
    void parse_use(ast_file& out, bool after_decls) {
        source_loc l = loc();
        eat();   // 'use'
        if (after_decls)
            error_at(toks[pos - 1], "'use' declarations must come before all other "
                     "declarations of a file");
        if (!expect(token_type::string, "a module path in quotes, e.g. use \"schema.ls\""))
            return;
        out.uses.push_back({l, toks[pos - 1].text});
    }

    // sequence_decl ::= 'sequence' IDENT '{' statement_list '}'   (section 6.10)
    void parse_sequence(ast_file& out) {
        sequence_decl sq;
        sq.loc = loc();
        eat();   // 'sequence'
        if (!expect(token_type::ident, "a sequence name")) return;
        sq.name_loc = prev_loc();
        sq.name = toks[pos - 1].text;
        if (at(token_type::lparen)) {
            error_at(peek(), "expected '{' after sequence name '" + sq.name +
                     "'; sequences take no attributes");
            recover_to(token_type::rparen);
        }
        if (!expect(token_type::lbrace, "'{'")) return;
        parse_statement_list(sq.stmts, sq.name);
        expect(token_type::rbrace, "'}'");
        out.sequences.push_back(std::move(sq));
    }

    // ── recovery ─────────────────────────────────────────────────────────────

    void eat_bad() { if (!at_end()) eat(); }
    // Skip the rest of source line `line`, stopping at a '}' that may close
    // the enclosing block.
    void skip_line(int line) {
        while (!at_end() && !at(token_type::rbrace) && peek().line == line) eat();
    }
    void recover_to(token_type t) {
        while (!at(t) && !at_end()) eat();
        accept(t);
    }

    // ── entry ────────────────────────────────────────────────────────────────

    bool at_declaration() const {
        switch (peek().type) {
        case token_type::kw_use: case token_type::kw_tag: case token_type::kw_layers:
        case token_type::kw_params: case token_type::kw_rule: case token_type::kw_sequence:
            return true;
        default:
            return false;
        }
    }

    void run(ast_file& out) {
        bool after_decls = false;
        while (!at_end()) {
            if (at(token_type::kw_use)) {
                parse_use(out, after_decls);
                continue;
            }
            after_decls = true;
            switch (peek().type) {
            case token_type::kw_tag:      parse_tag(out);      break;
            case token_type::kw_layers:   parse_layers(out);   break;
            case token_type::kw_params:   parse_params(out);   break;
            case token_type::kw_rule:     parse_rule(out);     break;
            case token_type::kw_sequence: parse_sequence(out); break;
            default:
                // one diagnostic, then resume at the next declaration
                error_at(peek(), "expected a declaration (use, tag, layers, params, rule, sequence)");
                eat_bad();
                while (!at_end() && !at_declaration()) eat();
                break;
            }
        }
    }
};

}  // namespace

std::optional<ast_file> parse(std::string_view source, std::string_view file,
                              diagnostics& diags, int mod) {
    auto toks = lex(source, file, diags);
    ast_file out;
    parser p{toks, file, diags};
    p.mod = mod;
    p.run(out);
    return out;
}

}  // namespace ls
