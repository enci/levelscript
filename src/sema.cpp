#include "sema.hpp"
#include <unordered_set>

namespace ls {

namespace {

// ── pattern transforms (spec §5.6, §10.3) ────────────────────────────────────
//
// Applied at compile time to expand symmetry/rotation variants. The both-axis
// flip IS the 180° rotation, so flip_both doubles as rot180; the per-sub-rule
// dedup below collapses the coincidence (v0.6.2 semantics).

enum class transform { identity, flip_h, flip_v, flip_both, rot90, rot270 };

compiled_pattern transform_pattern(transform k, compiled_pattern const& p) {
    compiled_pattern out = p;
    bool swaps = (k == transform::rot90 || k == transform::rot270);
    out.rows = swaps ? p.cols : p.rows;
    out.cols = swaps ? p.rows : p.cols;
    for (int r = 0; r < out.rows; ++r)
        for (int c = 0; c < out.cols; ++c) {
            int sr = r, sc = c;
            switch (k) {
            case transform::identity:                                     break;
            case transform::flip_h:    sc = p.cols - 1 - c;               break;
            case transform::flip_v:    sr = p.rows - 1 - r;               break;
            case transform::flip_both: sr = p.rows - 1 - r;
                                       sc = p.cols - 1 - c;               break;
            case transform::rot90:     sr = p.rows - 1 - c; sc = r;       break;   // cw
            case transform::rot270:    sr = c; sc = p.cols - 1 - r;       break;   // ccw
            }
            out.cells[r * out.cols + c] = p.cells[sr * p.cols + sc];
        }
    return out;
}

compiled_write_term transform_write_term(transform k, compiled_write_term const& t) {
    compiled_write_term out;
    out.what   = t.what;
    out.weight = t.weight;
    if (t.what == compiled_write_term::kind::leaf)
        out.pattern = transform_pattern(k, t.pattern);
    else
        for (auto const& it : t.items)
            out.items.push_back(transform_write_term(k, it));
    return out;
}

compiled_pair transform_pair(transform k, compiled_pair const& pair) {
    compiled_pair out;
    out.sub_rule_idx = pair.sub_rule_idx;
    for (auto const& pat : pair.lhs)
        out.lhs.push_back(transform_pattern(k, pat));
    out.rhs = transform_write_term(k, pair.rhs);
    return out;
}

bool patterns_equal(compiled_pattern const& a, compiled_pattern const& b) {
    if (a.grid_id != b.grid_id || a.rows != b.rows || a.cols != b.cols) return false;
    for (int i = 0; i < (int)a.cells.size(); ++i)
        if (a.cells[i].what != b.cells[i].what || a.cells[i].val != b.cells[i].val ||
            a.cells[i].expr != b.cells[i].expr)   // transforms share arena indices
            return false;
    return true;
}

// Variant dedup key: two variants are duplicates when every LHS pattern is
// cell-identical (they would produce the same candidates).
bool pair_lhs_equal(compiled_pair const& a, compiled_pair const& b) {
    if (a.lhs.size() != b.lhs.size()) return false;
    for (int i = 0; i < (int)a.lhs.size(); ++i)
        if (!patterns_equal(a.lhs[i], b.lhs[i])) return false;
    return true;
}

// Expression value kinds (§5.8).
enum class val_type { num, mask, boolean };

struct analyzer {
    ast_file const&  ast;
    compiled&        out;
    diagnostics&     diags;
    std::string_view file;

    // Identifier scope for the position being compiled. Cell/where exprs are
    // permissive; `when` guards and param exprs are restricted (§4.2/§6).
    struct scope {
        bool allow_grids{true};
        bool allow_pos{true};      // x / y / width / height
        int  param_limit{-1};      // -1 = any param; else only ids < limit
    };
    scope scope_{};

    void error(source_loc loc, std::string msg) {
        diags.error(file, loc.line, loc.col, std::move(msg));
    }

    // ── expressions (§5.8) ───────────────────────────────────────────────────

    int add_expr(compiled_expr e) {
        out.exprs.push_back(e);
        return (int)out.exprs.size() - 1;
    }
    int num_lit(long long v = 0) { return add_expr({ce_kind::int_lit, v, -1, -1, -1, -1}); }
    // The typed empty value: tag → mask 0x1, number → the sentinel.
    int typed_empty(val_type t) {
        return t == val_type::mask
             ? add_expr({ce_kind::mask_lit, tag_empty, -1, -1, -1, -1})
             : add_expr({ce_kind::int_lit, num_empty, -1, -1, -1, -1});
    }

    static bool is_reserved_ident(std::string const& n) {
        return n == "x" || n == "y" || n == "width" || n == "height";
    }

    struct builtin_spec { char const* name; ce_kind kind; int arity; };
    static builtin_spec const* find_builtin(std::string const& name) {
        static const builtin_spec table[] = {
            {"if",     ce_kind::if_,     3},
            {"min",    ce_kind::min_,    2},
            {"max",    ce_kind::max_,    2},
            {"abs",    ce_kind::abs_,    1},
            {"clamp",  ce_kind::clamp_,  3},
            {"random", ce_kind::random_, 2},
        };
        for (auto const& s : table)
            if (name == s.name) return &s;
        return nullptr;
    }
    static bool is_builtin_name(std::string const& n) { return find_builtin(n) != nullptr; }

    // Resolve a bare identifier: x/y/width/height → param → grid → tag value
    // or union (the enclosing cell's tagset first, else a unique value match).
    int compile_ident(expr const& e, int ctx_tag, val_type& t) {
        auto const& name = e.ident;
        if (is_reserved_ident(name)) {
            if (!scope_.allow_pos) {
                error(e.loc, "'" + name + "' cannot be read here "
                      "(position/dimension is not available at this scope)");
                t = val_type::num;
                return num_lit();
            }
            t = val_type::num;
            ce_kind k = name == "x" ? ce_kind::pos_x
                      : name == "y" ? ce_kind::pos_y
                      : name == "width" ? ce_kind::width : ce_kind::height;
            return add_expr({k, 0, -1, -1, -1, -1});
        }
        int pid = out.param_id(name);
        if (pid >= 0) {
            if (scope_.param_limit >= 0 && pid >= scope_.param_limit) {
                error(e.loc, "param '" + name + "' is referenced before it is declared");
                t = val_type::num;
                return num_lit();
            }
            t = val_type::num;
            return add_expr({ce_kind::param_read, 0, pid, -1, -1, -1});
        }
        int gid = out.layer_id(name);
        if (gid >= 0) {
            if (!scope_.allow_grids) {
                error(e.loc, "grid '" + name + "' cannot be read here "
                      "(only params are available at this scope)");
                t = val_type::num;
                return num_lit();
            }
            t = out.layers[gid].tag_id < 0 ? val_type::num : val_type::mask;
            return add_expr({ce_kind::grid_read, 0, gid, -1, -1, -1});
        }
        if (ctx_tag >= 0) {   // tag value or union of the enclosing cell's tagset
            int64_t m = out.mask_of(ctx_tag, name);
            if (m != 0) {
                t = val_type::mask;
                return add_expr({ce_kind::mask_lit, m, -1, -1, -1, -1});
            }
        }
        // unique tag value anywhere
        int found = 0; int64_t mask = 0;
        for (int ti = 0; ti < (int)out.tag_names.size(); ++ti) {
            int vid = out.value_id(ti, name);
            if (vid >= 0) { ++found; mask = tag_bit(vid); }
        }
        if (found == 1) {
            t = val_type::mask;
            return add_expr({ce_kind::mask_lit, mask, -1, -1, -1, -1});
        }
        error(e.loc, "unknown identifier '" + name + "' in expression");
        t = val_type::num;
        return num_lit();
    }

    int compile_call(expr const& e, int ctx_tag, val_type& t, int hint) {
        builtin_spec const* spec = find_builtin(e.ident);
        auto fail = [&](std::string msg) {
            error(e.loc, std::move(msg));
            t = val_type::num;
            return num_lit();
        };
        if (!spec) return fail("unknown function '" + e.ident + "'");
        if ((int)e.args.size() != spec->arity)
            return fail("built-in '" + e.ident + "' takes " +
                        std::to_string(spec->arity) + " argument(s), got " +
                        std::to_string(e.args.size()));

        if (spec->kind == ce_kind::if_) {
            // if(c, a, b) — eager; a bare '.' branch types from its sibling.
            val_type tc;
            int cc = compile_expr(*e.args[0], ctx_tag, tc);
            if (tc != val_type::boolean) return fail("'if' condition must be boolean");
            bool e1 = e.args[1]->kind == expr_kind::empty_lit;
            bool e2 = e.args[2]->kind == expr_kind::empty_lit;
            val_type t1, t2;
            int c1, c2;
            if (e1 && !e2) {
                c2 = compile_expr(*e.args[2], ctx_tag, t2, hint);
                c1 = compile_expr(*e.args[1], ctx_tag, t1, (int)t2);
            } else {
                c1 = compile_expr(*e.args[1], ctx_tag, t1, hint);
                c2 = compile_expr(*e.args[2], ctx_tag, t2, e2 ? (int)t1 : hint);
            }
            if (t1 != t2) return fail("'if' branches must have the same type");
            t = t1;
            return add_expr({ce_kind::if_, 0, -1, cc, c1, c2});
        }

        // every other built-in is numeric
        int a[3] = {-1, -1, -1};
        for (int i = 0; i < spec->arity; ++i) {
            val_type ta;
            a[i] = compile_expr(*e.args[i], ctx_tag, ta, (int)val_type::num);
            if (ta != val_type::num)
                return fail("'" + e.ident + "' requires number arguments");
        }
        t = val_type::num;
        return add_expr({spec->kind, 0, -1, a[0], a[1], a[2]});
    }

    // Compile + type-check one expression into the arena. `ctx_tag` is the
    // enclosing tag-grid cell's tagset (-1 when none); `hint` types a bare
    // '.' empty literal (-1 = none, else (int)val_type).
    int compile_expr(expr const& e, int ctx_tag, val_type& t, int hint = -1) {
        switch (e.kind) {
        case expr_kind::int_lit:
            t = val_type::num;
            return num_lit(e.int_val);
        case expr_kind::empty_lit:
            if (hint == (int)val_type::mask) { t = val_type::mask; return typed_empty(t); }
            if (hint == (int)val_type::num)  { t = val_type::num;  return typed_empty(t); }
            error(e.loc, "'.' has no inferable type here (use it against a grid, "
                  "an if-branch, or a target cell)");
            t = val_type::num;
            return typed_empty(val_type::num);
        case expr_kind::ident:
            return compile_ident(e, ctx_tag, t);
        case expr_kind::call:
            return compile_call(e, ctx_tag, t, hint);
        case expr_kind::neg: {
            val_type ta;
            int a = compile_expr(*e.args[0], ctx_tag, ta);
            if (ta != val_type::num) error(e.loc, "unary '-' requires a number");
            t = val_type::num;
            return add_expr({ce_kind::neg, 0, -1, a, -1, -1});
        }
        case expr_kind::not_: {
            val_type ta;
            int a = compile_expr(*e.args[0], ctx_tag, ta);
            if (ta != val_type::boolean) error(e.loc, "'!' requires a boolean");
            t = val_type::boolean;
            return add_expr({ce_kind::not_, 0, -1, a, -1, -1});
        }
        default:
            break;
        }

        // `g == .` / `g != .` — the emptiness tests, before generic binary
        // compilation so the empty literal needs no independent type.
        if (e.kind == expr_kind::eq || e.kind == expr_kind::ne) {
            bool a_empty = e.args[0]->kind == expr_kind::empty_lit;
            bool b_empty = e.args[1]->kind == expr_kind::empty_lit;
            if (a_empty && b_empty) {
                error(e.loc, "'. == .' has no inferable type");
                t = val_type::boolean;
                return num_lit();
            }
            if (a_empty || b_empty) {
                val_type to;
                int oc = compile_expr(a_empty ? *e.args[1] : *e.args[0], ctx_tag, to);
                t = val_type::boolean;
                if (out.exprs[oc].kind == ce_kind::grid_read) {
                    // read the cell RAW and test emptiness (no number→0 coercion)
                    ce_kind k = e.kind == expr_kind::eq ? ce_kind::is_empty
                                                        : ce_kind::is_not_empty;
                    return add_expr({k, to == val_type::num ? 1 : 0,
                                     out.exprs[oc].ref, -1, -1, -1});
                }
                int ec = typed_empty(to);
                return add_expr({e.kind == expr_kind::eq ? ce_kind::eq : ce_kind::ne,
                                 0, -1, oc, ec, -1});
            }
        }

        val_type lt_, rt_;
        int a = compile_expr(*e.args[0], ctx_tag, lt_);
        int b = compile_expr(*e.args[1], ctx_tag, rt_);
        auto bin = [&](ce_kind k, val_type rt) {
            t = rt;
            return add_expr({k, 0, -1, a, b, -1});
        };
        switch (e.kind) {
        case expr_kind::add: case expr_kind::sub:
        case expr_kind::mul: case expr_kind::div_:
            if (lt_ != val_type::num || rt_ != val_type::num)
                error(e.loc, "arithmetic operator requires numeric operands");
            return bin(e.kind == expr_kind::add ? ce_kind::add
                     : e.kind == expr_kind::sub ? ce_kind::sub
                     : e.kind == expr_kind::mul ? ce_kind::mul : ce_kind::div_,
                       val_type::num);
        case expr_kind::lt: case expr_kind::le:
        case expr_kind::gt: case expr_kind::ge:
            if (lt_ != val_type::num || rt_ != val_type::num)
                error(e.loc, "relational operator requires numeric operands");
            return bin(e.kind == expr_kind::lt ? ce_kind::lt
                     : e.kind == expr_kind::le ? ce_kind::le
                     : e.kind == expr_kind::gt ? ce_kind::gt : ce_kind::ge,
                       val_type::boolean);
        case expr_kind::eq: case expr_kind::ne:
            if (!(lt_ == val_type::num && rt_ == val_type::num) &&
                !(lt_ == val_type::mask && rt_ == val_type::mask))
                error(e.loc, "'==' / '!=' require two numbers or two tag values");
            return bin(e.kind == expr_kind::eq ? ce_kind::eq : ce_kind::ne,
                       val_type::boolean);
        case expr_kind::and_: case expr_kind::or_:
            if (lt_ != val_type::boolean || rt_ != val_type::boolean)
                error(e.loc, "'&&' / '||' require boolean operands");
            return bin(e.kind == expr_kind::and_ ? ce_kind::and_ : ce_kind::or_,
                       val_type::boolean);
        case expr_kind::bit_or:
            if (lt_ != val_type::mask || rt_ != val_type::mask)
                error(e.loc, "'|' (tag union) requires tag-valued operands");
            return bin(ce_kind::bit_or, val_type::mask);
        default:
            error(e.loc, "unsupported expression");
            t = val_type::num;
            return num_lit();
        }
    }

    // One collision discipline for every named declaration (the MGSL retro's
    // check_name): reserved position idents, built-in names, grid/param cross
    // collisions are all caught here with one message shape.
    void check_name(source_loc loc, std::string const& kind, std::string const& name) {
        if (is_reserved_ident(name))
            error(loc, kind + " '" + name + "' collides with a reserved "
                  "expression identifier (x, y, width, height)");
        if (is_builtin_name(name))
            error(loc, kind + " '" + name + "' collides with a built-in "
                  "function name (if, min, max, abs, clamp, random)");
    }

    // ── symbol tables ────────────────────────────────────────────────────────

    void build_tables() {
        for (auto const& t : ast.tags) {
            if (out.tag_id(t.name) >= 0)
                error(t.loc, "duplicate tag '" + t.name + "'");
            if (t.values.empty() && t.unions.empty())
                error(t.loc, "tagset '" + t.name + "' has no values");
            if ((int)t.values.size() > 30)   // §3: bits 1..30; 0 = empty, 31 reserved
                error(t.loc, "tagset '" + t.name + "' has " +
                      std::to_string(t.values.size()) + " values; the maximum is 30");
            std::vector<std::string> vals;
            for (auto const& v : t.values) {
                check_name(t.loc, "tag value", v);
                for (auto const& seen : vals)
                    if (seen == v) {
                        error(t.loc, "duplicate tag value '" + v + "' in '" + t.name + "'");
                        break;
                    }
                vals.push_back(v);
            }
            out.tag_names.push_back(t.name);
            out.tag_values.push_back(std::move(vals));

            // Named unions (§3): resolve in declaration order; a member is a
            // value or an earlier union of this tagset; a union shares the
            // value namespace and consumes no bit.
            int tid = (int)out.tag_names.size() - 1;
            out.tag_unions.push_back({});
            for (auto const& u : t.unions) {
                check_name(u.loc, "union", u.name);
                if (out.value_id(tid, u.name) >= 0 || out.mask_of(tid, u.name) != 0)
                    error(u.loc, "union '" + u.name +
                          "' redeclares a tag value or union in '" + t.name + "'");
                int64_t mask = 0;
                for (auto const& m : u.members) {
                    int64_t mm = out.mask_of(tid, m);
                    if (mm == 0)
                        error(u.loc, "union '" + u.name + "' references '" + m +
                              "', which is not a value or earlier union of '" +
                              t.name + "'");
                    mask |= mm;
                }
                out.tag_unions[tid].push_back({u.name, mask});
            }
        }

        for (auto const& l : ast.layers.layers) {
            check_name(ast.layers.loc, "grid", l.name);
            if (out.layer_id(l.name) >= 0) {
                error(ast.layers.loc, "duplicate grid '" + l.name + "'");
                continue;
            }
            int tag = -1;
            if (l.type != "number") {
                tag = out.tag_id(l.type);
                if (tag < 0)
                    error(ast.layers.loc, "grid '" + l.name +
                          "' references undeclared tag '" + l.type + "'");
            }
            out.layers.push_back({l.name, tag});
        }

        for (auto const& p : ast.params) {
            check_name(p.loc, "param", p.name);
            if (out.layer_id(p.name) >= 0)
                error(p.loc, "param '" + p.name + "' collides with a grid of the same name");
            if (out.param_id(p.name) >= 0) {
                error(p.loc, "duplicate param '" + p.name + "'");
                continue;
            }
            // §7.3 #27: every input param must carry a default.
            if (!p.is_derived && !p.value)
                error(p.loc, "input param '" + p.name +
                      "' must have a default, e.g. '" + p.name + ": number = 0'");
            out.param_names.push_back(p.name);
        }
    }

    // Param startup expressions (§4.2): derived params and input defaults share
    // one discipline — earlier params only, no grids, no position/dimensions.
    void compile_params() {
        for (int i = 0; i < (int)ast.params.size(); ++i) {
            auto const& p = ast.params[i];
            if (!p.value) continue;   // missing default already reported
            scope_ = scope{false, false, i};
            val_type t;
            int e = compile_expr(*p.value, -1, t, (int)val_type::num);
            if (t != val_type::num)
                error(p.loc, std::string(p.is_derived ? "derived param '" : "default for param '")
                      + p.name + "' must evaluate to a number");
            out.startup_exprs.push_back({i, e, !p.is_derived});
        }
        scope_ = scope{};
    }

    // ── rules ────────────────────────────────────────────────────────────────

    // Compile one pattern; `exp_rows/exp_cols` enforce the shape constraint
    // (spec §5.4, recursive over the write tree) — 0 means "sets the reference".
    compiled_pattern compile_pattern(pattern const& p, int exp_rows, int exp_cols,
                                     bool is_rhs) {
        compiled_pattern cp;
        int tag = -1;
        if (p.is_where) {
            cp.grid_id  = where_grid;
            cp.is_where = true;
            if (is_rhs)   // §7.3 #11
                error(p.loc, "'where' is a match-side pseudo-layer; it cannot "
                      "appear on the write side");
        } else {
            cp.grid_id = out.layer_id(p.grid);
            if (cp.grid_id < 0) {
                error(p.loc, "undeclared grid '" + p.grid + "'");
                return cp;
            }
            tag = out.layers[cp.grid_id].tag_id;
            cp.is_number = tag < 0;
        }
        if (exp_rows > 0 && (p.rows != exp_rows || p.cols != exp_cols))
            error(p.loc, "pattern dimension mismatch: expected " +
                  std::to_string(exp_rows) + "x" + std::to_string(exp_cols) +
                  ", got " + std::to_string(p.rows) + "x" + std::to_string(p.cols));

        if (p.rows == 0 || p.cols == 0) {
            error(p.loc, "empty pattern body");
            return cp;
        }
        for (int r = 0; r < p.rows; ++r)
            if ((int)p.cells[r].size() != p.cols)
                error(p.loc, "inconsistent row widths in pattern: row " +
                      std::to_string(r + 1) + " has " +
                      std::to_string(p.cells[r].size()) + " cells, expected " +
                      std::to_string(p.cols));

        cp.rows = p.rows;
        cp.cols = p.cols;
        cp.cells.assign((size_t)p.rows * p.cols, {});
        for (int r = 0; r < p.rows; ++r) {
            int w = (int)p.cells[r].size() < p.cols ? (int)p.cells[r].size() : p.cols;
            for (int c = 0; c < w; ++c)
                cp.cells[r * cp.cols + c] =
                    compile_cell(p.cells[r][c], tag, cp.is_number, p.is_where, is_rhs);
        }
        return cp;
    }

    compiled_cell compile_cell(cell const& in, int tag, bool is_number,
                               bool is_where, bool is_rhs) {
        compiled_cell cc;

        if (is_where) {   // where cells are always parenthesized booleans (§5.9)
            if (in.kind != cell_kind::expr_cell) {
                error(in.loc, "'where' cells must be a parenthesized boolean expression");
                cc.what = compiled_cell::kind::expr;
                cc.expr = num_lit();
                return cc;
            }
            val_type t;
            cc.what = compiled_cell::kind::expr;
            cc.expr = compile_expr(*in.value, -1, t);
            if (t != val_type::boolean)
                error(in.loc, "'where' cell must evaluate to a boolean");
            return cc;
        }

        switch (in.kind) {
        case cell_kind::any:
            cc.what = compiled_cell::kind::wildcard;
            break;
        case cell_kind::empty:
            cc.val = is_number ? num_empty : tag_empty;
            break;
        case cell_kind::number:
            if (!is_number)
                error(in.loc, "integer cell in a tag grid");
            cc.val = in.number;
            break;
        case cell_kind::tag_mask: {
            if (is_number) {
                error(in.loc, "expected an integer or wildcard in a 'number' grid cell");
                break;
            }
            int64_t full = 0;
            for (int i = 0; i < (int)out.tag_values[tag].size(); ++i)
                full |= tag_bit(i);
            int64_t mask = 0;
            for (auto const& a : in.atoms) {
                int64_t m = out.mask_of(tag, a.name);
                if (m == 0) {
                    error(a.loc, "unknown tag value '" + a.name + "' for this grid");
                    continue;
                }
                if (a.negate) {
                    if (is_rhs)   // §7.3 #15: complement is match-side only
                        error(a.loc, "tag complement '!" + a.name +
                              "' is not allowed on the write side");
                    mask |= full & ~m;
                } else {
                    mask |= m;
                }
            }
            cc.val = mask;
            break;
        }
        case cell_kind::expr_cell: {
            cc.what = compiled_cell::kind::expr;
            val_type t;
            int hint = (int)(is_number ? val_type::num : val_type::mask);
            cc.expr = compile_expr(*in.value, tag, t, hint);
            if (is_number && t != val_type::num)
                error(in.loc, "expression in a 'number' grid cell must evaluate to a number");
            if (!is_number && t != val_type::mask)
                error(in.loc, "expression in a tag grid cell must evaluate to a tag value");
            break;
        }
        }
        return cc;
    }

    compiled_write_term compile_write_term(write_term const& t,
                                           int exp_rows, int exp_cols) {
        compiled_write_term ct;
        ct.weight = t.weight;
        if (t.what == write_term::kind::leaf) {
            ct.what    = compiled_write_term::kind::leaf;
            ct.pattern = compile_pattern(t.pat, exp_rows, exp_cols, /*is_rhs=*/true);
            return ct;
        }
        ct.what = t.what == write_term::kind::all ? compiled_write_term::kind::all
                                                  : compiled_write_term::kind::any;
        for (auto const& it : t.items)
            ct.items.push_back(compile_write_term(it, exp_rows, exp_cols));
        if (ct.what == compiled_write_term::kind::all)
            check_all_no_overlap(ct, t.loc);
        return ct;
    }

    // Within one { all } write block, two items may not write the same grid at
    // the same cell — a write-write ambiguity (spec §7.3 #31).
    void check_all_no_overlap(compiled_write_term const& all_node, source_loc loc) {
        auto key = [](int g, int r, int c) {
            return ((int64_t)g << 40) | ((int64_t)r << 20) | (int64_t)c;
        };
        std::unordered_set<int64_t> seen;
        for (auto const& item : all_node.items) {
            std::vector<compiled_pattern const*> leaves;
            collect_write_leaves(item, leaves);
            std::vector<int64_t> mine;
            for (auto const* p : leaves) {
                if (p->grid_id < 0) continue;
                for (int r = 0; r < p->rows; ++r)
                    for (int c = 0; c < p->cols; ++c) {
                        if (p->at(r, c).what == compiled_cell::kind::wildcard) continue;
                        int64_t k = key(p->grid_id, r, c);
                        if (seen.count(k)) {
                            error(loc, "same-grid simultaneous write in '{ all }': grid '" +
                                  out.layers[p->grid_id].name +
                                  "' is written twice at the same cell");
                            return;
                        }
                        mine.push_back(k);
                    }
            }
            for (int64_t k : mine) seen.insert(k);
        }
    }

    compiled_pair compile_base_pair(rule_pair const& pr) {
        compiled_pair cp;
        int rows = 0, cols = 0;
        for (auto const& p : pr.lhs) {
            cp.lhs.push_back(compile_pattern(p, rows, cols, /*is_rhs=*/false));
            if (rows == 0) { rows = cp.lhs.back().rows; cols = cp.lhs.back().cols; }
        }
        cp.rhs = compile_write_term(pr.rhs, rows, cols);
        return cp;
    }

    void compile_rules() {
        for (auto const& r : ast.rules) {
            for (auto const& seen : out.rules)
                if (seen.name == r.name)
                    error(r.loc, "duplicate rule '" + r.name + "'");

            // attribute validation (spec §7.3 #7/#18)
            if (r.symmetry != "none" && r.symmetry != "horizontal" &&
                r.symmetry != "vertical" && r.symmetry != "all")
                error(r.loc, "invalid value '" + r.symmetry +
                      "' for attribute 'symmetry'; allowed: none, horizontal, vertical, all");
            for (long long a : r.rotation_angles)
                if (a != 90 && a != 180 && a != 270)
                    error(r.loc, "invalid rotation angle '" + std::to_string(a) +
                          "'; allowed angles are 90, 180, 270");

            // symmetry=all is four variants: identity + H + V + both-axis; the
            // both-axis/180° coincidence dedups below (spec §5.6.1, v0.6.2).
            std::vector<transform> syms = {transform::identity};
            if (r.symmetry == "horizontal") syms.push_back(transform::flip_h);
            else if (r.symmetry == "vertical") syms.push_back(transform::flip_v);
            else if (r.symmetry == "all")
                syms = {transform::identity, transform::flip_h,
                        transform::flip_v, transform::flip_both};

            std::vector<transform> rots = {transform::identity};
            for (long long a : r.rotation_angles) {
                if (a == 90)       rots.push_back(transform::rot90);
                else if (a == 180) rots.push_back(transform::flip_both);   // rot180
                else if (a == 270) rots.push_back(transform::rot270);
            }

            compiled_rule cr;
            cr.name = r.name;
            cr.body = r.body;
            // Expand each sub-rule into its variants; dedup by LHS equality,
            // scoped per sub-rule so same-LHS sub-rules both survive (§10.3).
            for (int bi = 0; bi < (int)r.pairs.size(); ++bi) {
                compiled_pair base = compile_base_pair(r.pairs[bi]);
                base.sub_rule_idx = bi;
                std::vector<compiled_pair> variants;
                for (auto rk : rots)
                    for (auto sk : syms) {
                        compiled_pair cand = transform_pair(sk, transform_pair(rk, base));
                        cand.sub_rule_idx = bi;
                        bool dup = false;
                        for (auto const& v : variants)
                            if (pair_lhs_equal(v, cand)) { dup = true; break; }
                        if (!dup) variants.push_back(std::move(cand));
                    }
                for (auto& v : variants) cr.pairs.push_back(std::move(v));
            }
            out.rules.push_back(std::move(cr));
        }
    }

    // ── reductivity check (LevelScript addition; MGSL §6.9 left this to the
    //    author). A sub-rule *definitely* sustains the fixpoint when no write
    //    that happens on every resolution ({ any } branches don't count)
    //    overwrites one of its own LHS-constrained cells with a value that no
    //    longer matches — the applied anchor then re-matches forever, and
    //    `all(policy=incremental)` never terminates. Conservative: warns only
    //    on the guaranteed case. ─────────────────────────────────────────────

    static void collect_unconditional_leaves(compiled_write_term const& t,
                                             std::vector<compiled_pattern const*>& out) {
        if (t.what == compiled_write_term::kind::leaf) { out.push_back(&t.pattern); return; }
        if (t.what == compiled_write_term::kind::any) return;   // may not happen
        for (auto const& it : t.items) collect_unconditional_leaves(it, out);
    }

    void check_reductive(compiled_rule const& rule, source_loc loc) {
        for (auto const& pair : rule.pairs) {
            std::vector<compiled_pattern const*> writes;
            collect_unconditional_leaves(pair.rhs, writes);

            bool invalidates = false;
            for (auto const& lp : pair.lhs) {
                if (invalidates) break;
                if (lp.is_where) { invalidates = true; break; }   // uncertain → no warning
                if (lp.grid_id < 0) continue;
                for (int r = 0; r < lp.rows && !invalidates; ++r)
                    for (int c = 0; c < lp.cols && !invalidates; ++c) {
                        auto const& req = lp.at(r, c);
                        if (req.what == compiled_cell::kind::wildcard) continue;
                        // computed matches are uncertain — never a guaranteed loop
                        if (req.what == compiled_cell::kind::expr) { invalidates = true; break; }
                        for (auto const* wp : writes) {
                            if (wp->grid_id != lp.grid_id) continue;
                            auto const& w = wp->at(r, c);
                            if (w.what == compiled_cell::kind::wildcard) continue;
                            // computed writes are uncertain too
                            if (w.what == compiled_cell::kind::expr) { invalidates = true; break; }
                            bool still = lp.is_number ? (w.val == req.val)
                                                      : ((w.val & req.val) != 0);
                            if (!still) { invalidates = true; break; }
                        }
                    }
            }
            if (!invalidates) {
                diags.warning(file, loc.line, loc.col,
                    "'all(policy=incremental)' over rule '" + rule.name +
                    "' may never terminate: a sub-rule's write leaves its own "
                    "match intact, so the fixpoint is unreachable");
                return;
            }
        }
    }

    // ── program (operation table, §6.0) ──────────────────────────────────────

    // ── the operation table (§6.0): a closed set resolved by name, exactly
    //    like expression built-ins. Adding an operation = a row here + an
    //    executor in the machine; no grammar or keyword change. ──────────────

    enum class arg_kind { int_, grid, pred, value, expr, enum_ };

    struct op_param {
        char const* name;
        arg_kind    kind;
        bool        positional;   // false = named-only
        bool        required;
    };
    struct op_spec {
        char const*            name;
        op_kind                 kind;
        std::vector<op_param>   params;
    };

    static std::vector<op_spec> const& op_table() {
        static const std::vector<op_spec> table = {
            {"resize",  op_kind::resize,  {{"w", arg_kind::int_, true, true},
                                           {"h", arg_kind::int_, true, true}}},
            {"upscale", op_kind::upscale, {{"n", arg_kind::int_, true, true},
                                           {"m", arg_kind::int_, true, true}}},
            {"trim",    op_kind::trim,    {}},
            {"mirror",  op_kind::mirror,  {{"axis", arg_kind::enum_, true, true}}},
            {"pad",     op_kind::pad,     {{"n", arg_kind::int_, true, true}}},
            {"path",    op_kind::path,    {{"from",         arg_kind::pred,  false, true},
                                           {"to",           arg_kind::pred,  false, true},
                                           {"into",         arg_kind::grid,  false, true},
                                           {"write",        arg_kind::value, false, true},
                                           {"over",         arg_kind::grid,  false, false},
                                           {"passable",     arg_kind::pred,  false, false},
                                           {"connectivity", arg_kind::enum_, false, false},
                                           {"cost",         arg_kind::expr,  false, false}}},
        };
        return table;
    }

    static std::string arg_render(op_arg const& a) {
        switch (a.what) {
        case op_arg::kind::int_:  return std::to_string(a.int_val);
        case op_arg::kind::ident: return a.ident;
        case op_arg::kind::expr:  return "(expr)";
        }
        return "?";
    }

    // ── per-kind argument compilers ──────────────────────────────────────────

    bool op_int(op_arg const& a, char const* op, char const* pn, int& v) {
        if (a.what != op_arg::kind::int_) {
            error(a.loc, "argument '" + std::string(pn) + "' of '" + op +
                  "' must be an integer, got '" + arg_render(a) + "'");
            return false;
        }
        v = (int)a.int_val;
        return true;
    }

    int op_grid(op_arg const& a, char const* op, char const* pn) {
        if (a.what != op_arg::kind::ident) {
            error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                  "' expects a grid name, got '" + arg_render(a) + "'");
            return -1;
        }
        int id = out.layer_id(a.ident);
        if (id < 0)
            error(a.loc, "'" + std::string(pn) + "=' of '" + op + "': '" +
                  a.ident + "' is not a declared grid");
        return id;
    }

    // A bare tag must belong to exactly ONE tag layer's tagset; ambiguity
    // needs the expression form naming the grid.
    compiled_pred op_pred(op_arg const& a, char const* op, char const* pn) {
        compiled_pred p;
        p.given = true;
        if (a.what == op_arg::kind::ident) {
            int found_layer = -1, matches = 0;
            int64_t mask = 0;
            for (int li = 0; li < (int)out.layers.size(); ++li) {
                int tid = out.layers[li].tag_id;
                if (tid < 0) continue;
                int64_t m = out.mask_of(tid, a.ident);
                if (m != 0) { ++matches; found_layer = li; mask = m; }
            }
            if (matches == 0)
                error(a.loc, "unknown tag value '" + a.ident + "' in '" +
                      std::string(pn) + "=' of '" + op +
                      "' (no layer's tagset declares it)");
            else if (matches > 1)
                error(a.loc, "predicate '" + a.ident + "' in '" + std::string(pn) +
                      "=' of '" + op + "' is ambiguous (several layers could hold "
                      "it); use an expression naming the grid, e.g. " + pn +
                      "=((grid == " + a.ident + "))");
            else {
                p.grid_id = found_layer;
                p.mask = mask;
            }
            return p;
        }
        if (a.what == op_arg::kind::expr) {
            p.is_expr = true;
            val_type t;
            p.expr = compile_expr(*a.value, -1, t);
            if (t != val_type::boolean)
                error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                      "' must be a boolean expression");
            return p;
        }
        error(a.loc, "'" + std::string(pn) + "=' of '" + op +
              "' expects a tag value or a (boolean expression)");
        return p;
    }

    compiled_value op_value(op_arg const& a, char const* op, char const* pn,
                            int into_grid) {
        compiled_value v;
        int tid = into_grid >= 0 ? out.layers[into_grid].tag_id : -1;
        bool is_number = tid < 0;
        if (a.what == op_arg::kind::int_) {
            if (!is_number)
                error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                      "' writes into a tag grid; expected a tag value");
            v.const_val = a.int_val;
            return v;
        }
        if (a.what == op_arg::kind::ident) {
            if (is_number) {
                error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                      "' writes into a number grid; expected an integer");
                return v;
            }
            int64_t m = out.mask_of(tid, a.ident);
            if (m == 0)
                error(a.loc, "unknown tag value '" + a.ident +
                      "' for the target grid in '" + std::string(pn) + "=' of '" +
                      op + "'");
            v.const_val = m;
            return v;
        }
        v.is_expr = true;
        val_type t;
        v.expr = compile_expr(*a.value, tid, t,
                              (int)(is_number ? val_type::num : val_type::mask));
        if (is_number && t != val_type::num)
            error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                  "' must be a number (the target grid is a number grid)");
        if (!is_number && t != val_type::mask)
            error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                  "' must be a tag value (the target grid is a tag grid)");
        return v;
    }

    int op_expr(op_arg const& a, char const* op, char const* pn) {
        if (a.what == op_arg::kind::int_)
            return num_lit(a.int_val);
        if (a.what != op_arg::kind::expr) {
            error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                  "' expects an integer or a (numeric expression)");
            return -1;
        }
        val_type t;
        int e = compile_expr(*a.value, -1, t, (int)val_type::num);
        if (t != val_type::num)
            error(a.loc, "'" + std::string(pn) + "=' of '" + op +
                  "' must be a numeric expression");
        return e;
    }

    // ── argument binding + per-op compilation (§7.3 #32–36) ─────────────────

    bool compile_op_call(program_stmt const& s, compiled_op& op) {
        op_spec const* spec = nullptr;
        for (auto const& t : op_table())
            if (s.op_name == t.name) { spec = &t; break; }
        if (!spec) {
            error(s.loc, "unknown operation '" + s.op_name + "'");
            return false;
        }

        int n_positional = 0;
        for (auto const& p : spec->params)
            if (p.positional) ++n_positional;

        std::vector<op_arg const*> bound(spec->params.size(), nullptr);
        int  next_pos = 0;
        bool seen_named = false, ok = true;

        for (auto const& a : s.op_args) {
            if (a.name.empty()) {
                if (seen_named) {
                    error(a.loc, "positional argument after a named argument in '" +
                          s.op_name + "(...)'");
                    ok = false;
                    continue;
                }
                if (next_pos >= n_positional) {
                    if (spec->params.empty())
                        error(a.loc, "operation '" + s.op_name + "' takes no arguments");
                    else if (n_positional == 0) {
                        std::string names;
                        for (auto const& p : spec->params)
                            names += (names.empty() ? "" : ", ") + std::string(p.name) + "=";
                        error(a.loc, "the parameters of '" + s.op_name +
                              "' are named-only (" + names + ")");
                    } else {
                        error(a.loc, "too many positional arguments for '" + s.op_name +
                              "' (takes " + std::to_string(n_positional) + ")");
                    }
                    ok = false;
                    continue;
                }
                bound[next_pos++] = &a;
            } else {
                seen_named = true;
                int idx = -1;
                for (int i = 0; i < (int)spec->params.size(); ++i)
                    if (a.name == spec->params[i].name) { idx = i; break; }
                if (idx < 0) {
                    error(a.loc, "unknown parameter '" + a.name + "' of operation '" +
                          s.op_name + "'");
                    ok = false;
                    continue;
                }
                if (bound[idx]) {
                    error(a.loc, "parameter '" + a.name + "' of '" + s.op_name +
                          "' supplied twice");
                    ok = false;
                    continue;
                }
                bound[idx] = &a;
            }
        }
        for (int i = 0; i < (int)spec->params.size(); ++i)
            if (spec->params[i].required && !bound[i]) {
                error(s.loc, "operation '" + s.op_name + "' requires argument '" +
                      std::string(spec->params[i].name) +
                      (spec->params[i].positional ? "'" : "='"));
                ok = false;
            }
        if (!ok) return false;

        op.kind = spec->kind;
        switch (spec->kind) {
        case op_kind::resize:
        case op_kind::upscale: {
            bool k = op_int(*bound[0], spec->name, spec->params[0].name, op.w)
                   & op_int(*bound[1], spec->name, spec->params[1].name, op.h);
            if (!k) return false;
            if (op.w <= 0 || op.h <= 0) {
                error(s.loc, std::string("'") + spec->name +
                      "' dimensions must be positive; got " +
                      std::to_string(op.w) + "x" + std::to_string(op.h));
                return false;
            }
            break;
        }
        case op_kind::trim:
            break;
        case op_kind::mirror: {
            std::string axis = arg_render(*bound[0]);
            if (axis != "horizontal" && axis != "vertical") {
                error(bound[0]->loc, "invalid mirror axis '" + axis +
                      "'; expected horizontal or vertical");
                return false;
            }
            op.w = axis == "horizontal" ? 1 : 0;
            break;
        }
        case op_kind::pad:
            if (!op_int(*bound[0], spec->name, spec->params[0].name, op.w))
                return false;
            if (op.w < 0) {
                error(s.loc, "invalid pad argument; margin must be non-negative, got " +
                      std::to_string(op.w));
                return false;
            }
            break;
        case op_kind::path: {
            op.into_grid = op_grid(*bound[2], spec->name, "into");
            op.from      = op_pred(*bound[0], spec->name, "from");
            op.to        = op_pred(*bound[1], spec->name, "to");
            if (op.into_grid >= 0)
                op.write = op_value(*bound[3], spec->name, "write", op.into_grid);
            if (bound[4]) op.over_grid = op_grid(*bound[4], spec->name, "over");
            if (bound[5]) op.passable  = op_pred(*bound[5], spec->name, "passable");
            if (bound[6]) {
                auto const& a = *bound[6];
                long long cv = a.what == op_arg::kind::int_ ? a.int_val : -1;
                if (cv != 4 && cv != 8)
                    error(a.loc, "invalid connectivity '" + arg_render(a) +
                          "'; expected 4 or 8");
                else
                    op.connectivity = (int)cv;
            }
            if (bound[7]) op.cost = op_expr(*bound[7], spec->name, "cost");
            break;
        }
        }
        return true;
    }

    void compile_program() {
        for (auto const& s : ast.program.stmts) {
            compiled_stmt cs;
            if (s.what == program_stmt::kind::op_call) {
                cs.what = compiled_stmt::kind::op_call;
                if (!compile_op_call(s, cs.op)) continue;
            } else {
                cs.what = compiled_stmt::kind::apply;
                cs.strat = s.strat;
                cs.pol = s.pol;
                cs.is_percent = s.is_percent;
                cs.max_count = s.max_count;
                cs.percent = s.percent;
                for (int i = 0; i < (int)out.rules.size(); ++i)
                    if (out.rules[i].name == s.rule_name) { cs.rule_id = i; break; }
                if (cs.rule_id < 0) {
                    error(s.loc, "undeclared rule '" + s.rule_name + "'");
                    continue;
                }
                if (s.strat == strategy::some && !s.is_percent && s.max_count == 0)
                    error(s.loc, "'some(max=0)' applies no matches; did you mean a different strategy?");
                // §7.3 #30: unknown policy value.
                if (s.bad_policy)
                    error(s.loc, "unknown policy '" + s.policy_raw +
                          "'; expected snapshot, incremental, or stabilize");
                // §7.3 #28: invalid count/policy combination.
                if (s.is_percent && s.pol != exec_policy::snapshot)
                    error(s.loc, "'percent' requires the default 'snapshot' policy");
                if (s.strat == strategy::one && s.pol == exec_policy::stabilize)
                    error(s.loc, "'one' with 'policy=stabilize' is contradictory "
                          "(a single application cannot reach a sweep fixpoint)");
                // Reductivity warning (§6.9 — LevelScript addition): an
                // `all(policy=incremental)` fixpoint over a rule whose write
                // never invalidates its own match cannot terminate.
                if (s.strat == strategy::all && s.pol == exec_policy::incremental &&
                    cs.rule_id >= 0)
                    check_reductive(out.rules[cs.rule_id], s.loc);
            }
            // `when (expr)` guard (§6): boolean, params only — no grids, no
            // position/dimensions (there is no candidate position or committed
            // size at statement scope).
            if (s.guard) {
                scope_ = scope{false, false, -1};
                val_type t;
                cs.guard = compile_expr(*s.guard, -1, t);
                if (t != val_type::boolean)
                    error(s.loc, "'when' guard must be a boolean expression");
                scope_ = scope{};
            }
            out.stmts.push_back(cs);
        }
    }

    void run(bool best_effort) {
        build_tables();
        if (!best_effort && diags.has_errors()) return;
        compile_params();
        if (!best_effort && diags.has_errors()) return;
        compile_rules();
        if (!best_effort && diags.has_errors()) return;
        if (ast.has_program) compile_program();
    }
};

}  // namespace

bool analyze(ast_file const& ast, compiled& out, diagnostics& diags,
             std::string_view file, bool best_effort) {
    analyzer a{ast, out, diags, file};
    a.run(best_effort);
    return !diags.has_errors();
}

}  // namespace ls
