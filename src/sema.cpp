#include "sema.hpp"
#include "modules.hpp"
#include <algorithm>
#include <unordered_set>

namespace ls {

namespace {

// ── pattern transforms (spec section 5.6, section 10.3) ────────────────────────────────────
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

// Split a match-side pattern's cells into the matcher's two phases (section
// 5.11), in the variant's row-major order.
void index_probes(compiled_pattern& p) {
    p.bare.clear();
    p.computed.clear();
    for (int r = 0; r < p.rows; ++r)
        for (int c = 0; c < p.cols; ++c) {
            auto const& cell = p.at(r, c);
            if (cell.what == compiled_cell::kind::value && !p.is_where)
                p.bare.push_back({r, c, cell.val, -1});
            else if (cell.expr >= 0)
                p.computed.push_back({r, c, 0, cell.expr});
        }
}

// Index a variant for the matcher: per-pattern phase lists, then the
// variables' sites and the equality checks of their other binding cells.
void index_pair(compiled_pair& pair) {
    pair.var_sites.assign(pair.var_names.size(), {});
    pair.var_checks.clear();
    for (auto& pat : pair.lhs) {
        index_probes(pat);
        if (pat.is_where || pat.grid_id < 0) continue;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                auto const& cell = pat.at(r, c);
                if (cell.what != compiled_cell::kind::variable) continue;
                auto& site = pair.var_sites[(size_t)cell.var];
                if (site.grid < 0) site = {pat.grid_id, r, c};
                else pair.var_checks.push_back({cell.var, {pat.grid_id, r, c}});
            }
    }
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
    out.var_names = pair.var_names;
    for (auto const& pat : pair.lhs)
        out.lhs.push_back(transform_pattern(k, pat));
    out.rhs = transform_write_term(k, pair.rhs);
    return out;
}

bool patterns_equal(compiled_pattern const& a, compiled_pattern const& b) {
    if (a.grid_id != b.grid_id || a.rows != b.rows || a.cols != b.cols) return false;
    for (int i = 0; i < (int)a.cells.size(); ++i)
        if (a.cells[i].what != b.cells[i].what || a.cells[i].val != b.cells[i].val ||
            a.cells[i].expr != b.cells[i].expr ||  // transforms share arena indices
            a.cells[i].var != b.cells[i].var)      // and variable slots, i.e. names
            return false;
    return true;
}

// Structural write-tree equality: same combinator nesting, same item order,
// same weights, cell-identical leaves.
bool write_terms_equal(compiled_write_term const& a, compiled_write_term const& b) {
    if (a.what != b.what || a.weight != b.weight) return false;
    if (a.what == compiled_write_term::kind::leaf)
        return patterns_equal(a.pattern, b.pattern);
    if (a.items.size() != b.items.size()) return false;
    for (int i = 0; i < (int)a.items.size(); ++i)
        if (!write_terms_equal(a.items[i], b.items[i])) return false;
    return true;
}

// Variant dedup key (spec section 5.6.2): two variants are duplicates iff both the
// match side and the write tree are structurally equal. A same-LHS variant
// with different writes survives as its own candidate.
bool pairs_equal(compiled_pair const& a, compiled_pair const& b) {
    if (a.lhs.size() != b.lhs.size()) return false;
    for (int i = 0; i < (int)a.lhs.size(); ++i)
        if (!patterns_equal(a.lhs[i], b.lhs[i])) return false;
    return write_terms_equal(a.rhs, b.rhs);
}

// Expression value kinds (section 5.8).
enum class val_type { num, mask, boolean };

struct analyzer {
    ast_file const&       ast;   // the closure's merged declarations
    compiled&             out;
    diagnostics&          diags;
    module_closure const& mods;

    // Declaring module of each table entry, aligned with out.tag_names,
    // out.layers and out.param_names (rules and sequences align with the AST).
    std::vector<int> tag_mod, layer_mod, param_mod;

    // Identifier scope for the position being compiled. Cell/where exprs are
    // permissive; `when` guards and param exprs are restricted (section 4.2/section 6).
    struct scope {
        bool allow_grids{true};
        bool allow_pos{true};      // x / y / width / height
        int  param_limit{-1};      // -1 = any param; else only ids < limit
    };
    scope scope_{};

    // Pattern variables of the pair being compiled (section 5.11); null outside
    // a pattern pair, where any variable is check 46. A slot's type is the
    // binding cells' grid type: a tag id, or -1 for number.
    struct pair_vars {
        struct var {
            std::string name;          // '?name'
            int         type{-1};
            bool        bound{false};  // has a binding cell (check 44)
            bool        reported{false};
        };
        std::vector<var> vars;
        int slot(std::string const& name) {
            for (int i = 0; i < (int)vars.size(); ++i)
                if (vars[i].name == name) return i;
            vars.push_back({name});
            return (int)vars.size() - 1;
        }
    };
    pair_vars* vars_{nullptr};

    std::string type_name(int type) const {
        return type < 0 ? "number" : "tagset '" + out.tag_names[(size_t)type] + "'";
    }

    // A use of `name` in an expression or a write: its slot, or -1 after
    // reporting it (check 44, check 46).
    int use_variable(std::string const& name, source_loc loc) {
        if (!vars_) {
            error(loc, "pattern variable '" + name + "' outside a pattern; variables "
                  "exist only in pattern and 'where' cells");
            return -1;
        }
        int s = vars_->slot(name);
        auto& v = vars_->vars[(size_t)s];
        if (v.bound) return s;
        if (!v.reported)
            error(loc, "pattern variable '" + name + "' is never bound; it needs a "
                  "bare '" + name + "' cell on the match side");
        v.reported = true;
        return -1;
    }

    std::string const& label(source_loc loc) const { return mods.names[(size_t)loc.mod]; }
    void error(source_loc loc, std::string msg) {
        diags.error(label(loc), loc.line, loc.col, std::move(msg));
    }
    void warning(source_loc loc, std::string msg) {
        diags.warning(label(loc), loc.line, loc.col, std::move(msg));
    }

    // section 2.6 visibility: a module sees itself and the modules it uses directly.
    // A name declared in the closure but not seen is check 8, reported with
    // its declaring module so the fix is in the message.
    bool sees(int from, int decl) const { return mods.sees[(size_t)from][(size_t)decl] != 0; }
    void require_visible(source_loc at, char const* kind, std::string const& name, int decl) {
        if (sees(at.mod, decl)) return;
        error(at, std::string(kind) + " '" + name + "' is not visible here: it is declared in "
              "module '" + mods.names[(size_t)decl] + "', which this module does not use");
    }

    // ── expressions (section 5.8) ───────────────────────────────────────────────────

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
            require_visible(e.loc, "param", name, param_mod[(size_t)pid]);
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
            require_visible(e.loc, "grid", name, layer_mod[(size_t)gid]);
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
        case expr_kind::variable: {   // section 5.11
            int s = use_variable(e.ident, e.loc);
            if (s < 0) {   // reported; type it as wanted so nothing cascades
                t = hint == (int)val_type::mask ? val_type::mask : val_type::num;
                return typed_empty(t);
            }
            t = vars_->vars[(size_t)s].type < 0 ? val_type::num : val_type::mask;
            return add_expr({ce_kind::var_read, 0, s, -1, -1, -1});
        }
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
    // check_name): reserved position idents are caught here with one message
    // shape. Built-in names are not reserved (section 5.10): a call is `IDENT '('`,
    // and tag values, grids, and params are never called.
    void check_name(source_loc loc, std::string const& kind, std::string const& name) {
        if (is_reserved_ident(name))
            error(loc, kind + " '" + name + "' collides with a reserved "
                  "expression identifier (x, y, width, height)");
    }

    // ── symbol tables ────────────────────────────────────────────────────────

    void build_tables() {
        for (auto const& t : ast.tags) {
            if (out.tag_id(t.name) >= 0)
                error(t.loc, "duplicate tag '" + t.name + "'");
            if (t.values.empty() && t.unions.empty())
                error(t.loc, "tagset '" + t.name + "' has no values");
            if ((int)t.values.size() > 30)   // section 3: bits 1..30; 0 = empty, 31 reserved
                error(t.loc, "tagset '" + t.name + "' has " +
                      std::to_string(t.values.size()) + " values; the maximum is 30");
            std::vector<std::string> vals;
            for (auto const& v : t.values) {
                check_name(v.loc, "tag value", v.name);
                for (auto const& seen : vals)
                    if (seen == v.name) {
                        error(v.loc, "duplicate tag value '" + v.name + "' in '" + t.name + "'");
                        break;
                    }
                vals.push_back(v.name);
            }
            out.tag_names.push_back(t.name);
            out.tag_values.push_back(std::move(vals));
            tag_mod.push_back(t.loc.mod);

            // Named unions (section 3): resolve in declaration order; a member is a
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
            check_name(l.loc, "grid", l.name);
            if (out.layer_id(l.name) >= 0) {   // section 7.3, check 42, closure-wide
                error(l.loc, "duplicate grid '" + l.name + "'");
                continue;
            }
            int tag = -1;
            if (l.type != "number") {
                tag = out.tag_id(l.type);
                if (tag < 0)
                    error(l.loc, "grid '" + l.name +
                          "' references undeclared tag '" + l.type + "'");
                else
                    require_visible(l.loc, "tagset", l.type, tag_mod[(size_t)tag]);
            }
            out.layers.push_back({l.name, tag});
            layer_mod.push_back(l.loc.mod);
        }

        for (auto const& p : ast.params) {
            check_name(p.loc, "param", p.name);
            if (out.layer_id(p.name) >= 0)
                error(p.loc, "param '" + p.name + "' collides with a grid of the same name");
            if (out.param_id(p.name) >= 0) {
                error(p.loc, "duplicate param '" + p.name + "'");
                continue;
            }
            // section 7.3, check 27: every input param must carry a default.
            if (!p.is_derived && !p.value)
                error(p.loc, "input param '" + p.name +
                      "' must have a default, e.g. '" + p.name + ": number = 0'");
            out.param_names.push_back(p.name);
            param_mod.push_back(p.loc.mod);
        }
    }

    // Param startup expressions (section 4.2): derived params and input defaults share
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
    // (spec section 5.4, recursive over the write tree) — 0 means "sets the reference".
    compiled_pattern compile_pattern(pattern const& p, int exp_rows, int exp_cols,
                                     bool is_rhs) {
        compiled_pattern cp;
        int tag = -1;
        if (p.is_where) {
            cp.grid_id  = where_grid;
            cp.is_where = true;
            if (is_rhs)   // section 7.3, check 11
                error(p.loc, "'where' is a match-side pseudo-layer; it cannot "
                      "appear on the write side");
        } else {
            cp.grid_id = out.layer_id(p.grid);
            if (cp.grid_id < 0) {
                error(p.loc, "undeclared grid '" + p.grid + "'");
                return cp;
            }
            require_visible(p.loc, "grid", p.grid, layer_mod[(size_t)cp.grid_id]);
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

        if (is_where) {   // where cells are parenthesized booleans or '*' (section 5.9)
            if (in.kind == cell_kind::any) {   // no condition
                cc.what = compiled_cell::kind::wildcard;
                return cc;
            }
            if (in.kind == cell_kind::variable) {   // check 46
                error(in.loc, "a 'where' cell cannot be a bare pattern variable; "
                      "'where' cells are boolean expressions, e.g. (" + in.variable + " > 0)");
                cc.what = compiled_cell::kind::expr;
                cc.expr = num_lit(1);
                return cc;
            }
            if (in.kind != cell_kind::expr_cell) {
                error(in.loc, "'where' cells must be '*' or a parenthesized boolean expression");
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
                    if (is_rhs)   // section 7.3, check 15: complement is match-side only
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
        case cell_kind::variable: {   // section 5.11; bound and typed by bind_variables
            int s = is_rhs ? use_variable(in.variable, in.loc) : vars_->slot(in.variable);
            if (s < 0) { cc.what = compiled_cell::kind::wildcard; break; }
            int type = vars_->vars[(size_t)s].type;
            if (is_rhs && type != tag)   // check 45
                error(in.loc, "pattern variable '" + in.variable + "' holds a " +
                      type_name(type) + " value but is written into a " +
                      type_name(tag) + " grid");
            cc.what = compiled_cell::kind::variable;
            cc.var = s;
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
    // the same cell — a write-write ambiguity (spec section 7.3, check 31).
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

    // Every bare variable cell of a real-grid match-side pattern binds; all of
    // one variable's binding cells must share a grid type (check 45). Runs
    // before any cell compiles, so uses are order-free (section 5.11).
    void bind_variables(rule_pair const& pr, pair_vars& pv) {
        for (auto const& p : pr.lhs) {
            if (p.is_where) continue;   // a bare variable there is check 46
            int gid = out.layer_id(p.grid);
            if (gid < 0) continue;      // reported by compile_pattern
            int type = out.layers[gid].tag_id;
            for (auto const& row : p.cells)
                for (auto const& c : row) {
                    if (c.kind != cell_kind::variable) continue;
                    auto& v = pv.vars[(size_t)pv.slot(c.variable)];
                    if (!v.bound) { v.bound = true; v.type = type; continue; }
                    if (v.type != type && !v.reported) {
                        error(c.loc, "pattern variable '" + c.variable + "' binds a " +
                              type_name(type) + " cell here but a " +
                              type_name(v.type) + " cell elsewhere");
                        v.reported = true;
                    }
                }
        }
    }

    compiled_pair compile_base_pair(rule_pair const& pr) {
        compiled_pair cp;
        pair_vars pv;
        pair_vars* outer = vars_;
        vars_ = &pv;
        bind_variables(pr, pv);
        int rows = 0, cols = 0;
        for (auto const& p : pr.lhs) {
            cp.lhs.push_back(compile_pattern(p, rows, cols, /*is_rhs=*/false));
            if (rows == 0) { rows = cp.lhs.back().rows; cols = cp.lhs.back().cols; }
        }
        cp.rhs = compile_write_term(pr.rhs, rows, cols);
        vars_ = outer;
        for (auto const& v : pv.vars) cp.var_names.push_back(v.name);
        return cp;
    }

    void compile_rules() {
        for (auto const& r : ast.rules) {
            for (auto const& seen : out.rules)
                if (seen.name == r.name)
                    error(r.loc, "duplicate rule '" + r.name + "'");
            out.rules.push_back(compile_rule(r));
            out.rules.back().name = r.name;
        }
    }

    // One rule's attributes and body, expanded into its variants.
    compiled_rule compile_rule(rule_decl const& r) {
        // attribute validation (spec section 7.3, checks 7/18)
        if (r.symmetry != "none" && r.symmetry != "horizontal" &&
            r.symmetry != "vertical" && r.symmetry != "all")
            error(r.loc, "invalid value '" + r.symmetry +
                  "' for attribute 'symmetry'; allowed: none, horizontal, vertical, all");
        for (long long a : r.rotation_angles)
            if (a != 90 && a != 180 && a != 270)
                error(r.loc, "invalid rotation angle '" + std::to_string(a) +
                      "'; allowed angles are 90, 180, 270");

        // symmetry=all is four variants: identity + H + V + both-axis; the
        // both-axis/180° coincidence dedups below (spec section 5.6.1, v0.6.2).
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
        cr.body = r.body;
        // Expand each sub-rule into its variants; dedup by (match, write)
        // equality, scoped per sub-rule so same-LHS sub-rules both survive
        // (section 5.6.2, section 10.2).
        for (int bi = 0; bi < (int)r.pairs.size(); ++bi) {
            compiled_pair base = compile_base_pair(r.pairs[bi]);
            base.sub_rule_idx = bi;
            std::vector<compiled_pair> variants;
            using mirror = compiled_pair::mirror;
            auto rot_deg = [](transform k) -> int16_t {   // flip_both doubles as rot180
                return k == transform::rot90     ? 90
                     : k == transform::flip_both ? 180
                     : k == transform::rot270    ? 270 : 0;
            };
            auto sym_of = [](transform k) {
                return k == transform::flip_h    ? mirror::h
                     : k == transform::flip_v    ? mirror::v
                     : k == transform::flip_both ? mirror::both : mirror::none;
            };
            for (auto rk : rots)
                for (auto sk : syms) {
                    compiled_pair cand = transform_pair(sk, transform_pair(rk, base));
                    cand.sub_rule_idx = bi;
                    cand.rotation = rot_deg(rk);
                    cand.flip = sym_of(sk);
                    bool dup = false;
                    for (auto const& v : variants)
                        if (pairs_equal(v, cand)) { dup = true; break; }
                    if (!dup) variants.push_back(std::move(cand));
                }
            for (auto& v : variants) index_pair(v);
            for (auto& v : variants) cr.pairs.push_back(std::move(v));
        }
        return cr;
    }

    // ── reductivity check (LevelScript addition; MGSL section 6.9 left this to the
    //    author). A sub-rule *definitely* sustains the fixpoint when no write
    //    that happens on every resolution ({ any } branches don't count)
    //    overwrites one of its own LHS-constrained cells with a value that no
    //    longer matches — the applied anchor then re-matches forever, and
    //    `grow` without a count never terminates. Conservative: warns only
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
                        // A binding cell no write touches keeps its value, and so
                        // its equality with the others (section 6.9).
                        for (auto const* wp : writes) {
                            if (wp->grid_id != lp.grid_id) continue;
                            auto const& w = wp->at(r, c);
                            if (w.what == compiled_cell::kind::wildcard) continue;
                            // computed writes are uncertain too. Any write over a
                            // binding cell, or of a variable, invalidates: for
                            // certain with a '.' over a binding cell or a variable
                            // over a '.' cell (both non-empty), else uncertainly.
                            if (req.what == compiled_cell::kind::variable ||
                                w.what == compiled_cell::kind::expr ||
                                w.what == compiled_cell::kind::variable) { invalidates = true; break; }
                            bool still = lp.is_number ? (w.val == req.val)
                                                      : ((w.val & req.val) != 0);
                            if (!still) { invalidates = true; break; }
                        }
                    }
            }
            if (!invalidates) {
                warning(loc,
                    "'grow' over rule '" + rule.name +
                    "' may never terminate: a sub-rule's write leaves its own "
                    "match intact, so the fixpoint is unreachable");
                return;
            }
        }
    }

    // ── program (operation table, section 6.0) ──────────────────────────────────────

    // ── the operation table (section 6.0): a closed set resolved by name, exactly
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
        else
            require_visible(a.loc, "grid", a.ident, layer_mod[(size_t)id]);
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
                if (tid < 0 || !sees(a.loc.mod, layer_mod[(size_t)li])) continue;
                int64_t m = out.mask_of(tid, a.ident);
                if (m != 0) { ++matches; found_layer = li; mask = m; }
            }
            if (matches == 0)
                error(a.loc, "unknown tag value '" + a.ident + "' in '" +
                      std::string(pn) + "=' of '" + op +
                      "' (no layer this module sees has a tagset declaring it)");
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

    // ── argument binding + per-op compilation (section 7.3, check 32–36) ─────────────────

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

    // ── statements (section 6): the program body and every sequence body ──────────

    static std::string mode_name(apply_mode m) {
        switch (m) {
        case apply_mode::once:       return "once";
        case apply_mode::scatter:    return "scatter";
        case apply_mode::everywhere: return "everywhere";
        case apply_mode::grow:       return "grow";
        case apply_mode::settle:     return "settle";
        }
        return "";
    }

    // A count (section 6): a statement-scope expression like a guard - params
    // only, a number (check 21). A literal 0 applies nothing (check 28); a
    // literal percentage may not exceed 100 (check 30). Computed values are
    // clamped at run time instead.
    void compile_count(program_stmt const& s, compiled_stmt& cs, std::string const& ctx) {
        scope_ = scope{false, false, -1};
        val_type t;
        cs.count = compile_expr(*s.count, -1, t);
        scope_ = scope{};
        if (t != val_type::num)
            error(s.count_loc, "a count must be a number" + ctx);
        // literal-ness from the source: a count that failed to compile also
        // compiles to a literal
        auto const& e = *s.count;
        if (e.kind != expr_kind::int_lit) return;
        cs.count_lit = (int)e.int_val;
        if (e.int_val == 0)
            error(s.count_loc, std::string("a count of 0") +
                  (s.count_percent ? "%" : "") + " applies nothing" + ctx);
        if (s.count_percent && e.int_val > 100)
            error(s.count_loc, "percentage " + std::to_string(e.int_val) +
                  "% is above 100" + ctx);
    }

    // Levenshtein distance - did-you-mean for unknown rule/sequence names.
    static int edit_distance(std::string const& a, std::string const& b) {
        std::vector<int> row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) row[j] = (int)j;
        for (size_t i = 1; i <= a.size(); ++i) {
            int diag = row[0];
            row[0] = (int)i;
            for (size_t j = 1; j <= b.size(); ++j) {
                int up = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1,
                                   diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
                diag = up;
            }
        }
        return row[b.size()];
    }

    std::string did_you_mean(std::string const& name) const {
        std::string best, kind;
        int best_d = 3;   // suggest within 2 edits only
        for (auto const& r : out.rules) {
            if (r.is_inline) continue;
            int d = edit_distance(name, r.name);
            if (d < best_d) { best_d = d; best = r.name; kind = "rule"; }
        }
        for (auto const& sq : out.sequences) {
            int d = edit_distance(name, sq.name);
            if (d < best_d) { best_d = d; best = sq.name; kind = "sequence"; }
        }
        return best.empty() ? "" : "; did you mean " + kind + " '" + best + "'?";
    }

    // Rules and sequences share one namespace (section 7.3, check 39). Every sequence gets
    // a slot, duplicates included, so out.sequences[i] is ast.sequences[i];
    // name lookups find the first declaration.
    void register_sequences() {
        for (auto const& sq : ast.sequences) {
            bool dup = false;
            for (auto const& r : ast.rules)
                if (r.name == sq.name) {
                    error(sq.loc, "duplicate name '" + sq.name + "': already declared as a "
                          "rule on line " + std::to_string(r.loc.line) +
                          " - rules and sequences share one namespace");
                    dup = true;
                    break;
                }
            for (size_t k = 0; !dup && k < out.sequences.size(); ++k)
                if (out.sequences[k].name == sq.name) {
                    error(sq.loc, "duplicate sequence '" + sq.name + "' (first declared on line " +
                          std::to_string(ast.sequences[k].loc.line) + ")");
                    dup = true;
                }
            out.sequences.push_back({sq.name, {}});
        }
    }

    // Compile one statement list. `where` names the enclosing sequence for
    // diagnostics ("" = the program).
    void compile_stmts(std::vector<program_stmt> const& in,
                       std::vector<compiled_stmt>& outv, std::string const& where) {
        std::string ctx = where.empty() ? "" : " (sequence '" + where + "')";
        for (auto const& s : in) {
            compiled_stmt cs;
            cs.loc = s.loc;
            if (s.what == program_stmt::kind::op_call) {
                cs.what = compiled_stmt::kind::op_call;
                if (!compile_op_call(s, cs.op)) continue;
            } else {
                cs.what = compiled_stmt::kind::apply;
                cs.mode = s.mode;
                cs.percent = s.count_percent;
                if (s.inline_rule) {
                    // an anonymous rule declared in the sequence's module (section 6)
                    auto const& r = *s.inline_rule;
                    compiled_rule cr = compile_rule(r);
                    cr.name = "inline@" + std::to_string(r.loc.line) + ":" +
                              std::to_string(r.loc.col);
                    cr.is_inline = true;
                    out.rules.push_back(std::move(cr));
                    cs.rule_id = (int)out.rules.size() - 1;
                }
                for (int i = 0; cs.rule_id < 0 && i < (int)out.rules.size(); ++i)
                    if (out.rules[i].name == s.rule_name) { cs.rule_id = i; break; }
                if (cs.rule_id < 0)
                    for (int i = 0; i < (int)out.sequences.size(); ++i)
                        if (out.sequences[i].name == s.rule_name) { cs.seq_id = i; break; }
                if (cs.rule_id < 0 && cs.seq_id < 0) {
                    error(s.loc, "undeclared rule or sequence '" + s.rule_name + "'" +
                          did_you_mean(s.rule_name));
                    continue;
                }
                if (s.inline_rule)
                    ;   // declared right here
                else if (cs.rule_id >= 0)
                    require_visible(s.rule_name_loc, "rule", s.rule_name,
                                    ast.rules[(size_t)cs.rule_id].loc.mod);
                else
                    require_visible(s.rule_name_loc, "sequence", s.rule_name,
                                    ast.sequences[(size_t)cs.seq_id].loc.mod);
                if (s.count) compile_count(s, cs, ctx);
                if (cs.seq_id >= 0) {
                    // section 7.3, check 37: rule-only modes on a sequence.
                    if (s.mode != apply_mode::once && s.mode != apply_mode::settle)
                        error(s.loc, "'" + mode_name(s.mode) + "' is not valid on sequence '" +
                              s.rule_name + "'; a sequence takes 'once' or 'settle'" +
                              (s.mode == apply_mode::scatter
                                   ? ", and 'scatter' belongs on the statements inside it" : ""));
                } else if (s.mode == apply_mode::grow && !s.count) {
                    // Reductivity warning (section 6.9 — LevelScript addition): a
                    // `grow` fixpoint over a rule whose write never invalidates
                    // its own match cannot terminate.
                    check_reductive(out.rules[cs.rule_id], s.loc);
                }
            }
            // `when (expr)` guard (section 6): boolean, params only — no grids, no
            // position/dimensions (there is no candidate position or committed
            // size at statement scope).
            if (s.guard) {
                scope_ = scope{false, false, -1};
                val_type t;
                cs.guard = compile_expr(*s.guard, -1, t);
                if (t != val_type::boolean)
                    error(s.loc, "'when' guard must be a boolean expression" + ctx);
                scope_ = scope{};
            }
            outv.push_back(cs);
        }
    }

    // section 7.3, check 38: a sequence that applies itself, directly or through others.
    void check_sequence_cycles() {
        int n = (int)out.sequences.size();
        std::vector<int> state(n, 0);   // 0 new, 1 on the DFS path, 2 done
        std::vector<int> path;
        auto dfs = [&](auto&& self, int i) -> void {
            state[i] = 1;
            path.push_back(i);
            for (auto const& st : out.sequences[i].stmts) {
                int j = st.seq_id;
                if (st.what != compiled_stmt::kind::apply || j < 0) continue;
                if (state[j] == 1) {
                    if (j == i) {
                        error(ast.sequences[i].loc, "sequence '" + out.sequences[i].name +
                              "' applies itself");
                    } else {
                        std::string chain;
                        auto from = std::find(path.begin(), path.end(), j);
                        for (auto it = from; it != path.end(); ++it)
                            chain += "'" + out.sequences[*it].name + "' -> ";
                        error(ast.sequences[j].loc, "sequence cycle: " + chain + "'" +
                              out.sequences[j].name + "'");
                    }
                } else if (state[j] == 0) {
                    self(self, j);
                }
            }
            path.pop_back();
            state[i] = 2;
        };
        for (int i = 0; i < n; ++i)
            if (state[i] == 0) dfs(dfs, i);
    }

    // The operation that changes the grid dimensions on every iteration of
    // sequence `sid` ("" if none is certain): an unguarded non-identity
    // upscale/pad, directly or through unguarded nested applications that are
    // certain to run an iteration (no count, or a literal one). section 6.10.
    std::string dims_changer(int sid, std::vector<char>& seen) const {
        if (seen[sid]) return "";
        seen[sid] = 1;
        for (auto const& st : out.sequences[sid].stmts) {
            if (st.guard >= 0) continue;
            if (st.what == compiled_stmt::kind::op_call) {
                if (st.op.kind == op_kind::upscale && (st.op.w != 1 || st.op.h != 1)) return "upscale";
                if (st.op.kind == op_kind::pad && st.op.w > 0) return "pad";
            } else if (st.seq_id >= 0 && (st.count < 0 || st.count_lit > 0)) {
                // a computed count may be zero, so it is not certain to run
                std::string inner = dims_changer(st.seq_id, seen);
                if (!inner.empty()) return inner;
            }
        }
        return "";
    }

    // section 7.4 warning 2: `settle S` (no count) where no iteration can be stable.
    void warn_unstable_fixpoints(std::vector<compiled_stmt> const& stmts) {
        for (auto const& st : stmts) {
            if (st.what != compiled_stmt::kind::apply || st.seq_id < 0 ||
                st.mode != apply_mode::settle || st.count >= 0)
                continue;
            std::vector<char> seen(out.sequences.size(), 0);
            std::string op = dims_changer(st.seq_id, seen);
            if (!op.empty())
                warning(st.loc,
                    "'settle' over sequence '" + out.sequences[st.seq_id].name +
                    "' may never terminate: '" + op + "' changes the grid dimensions "
                    "on every iteration, so no iteration can be stable");
        }
    }

    void run(bool best_effort) {
        for (int id : mods.order) out.modules.push_back(mods.names[(size_t)id]);
        build_tables();
        if (!best_effort && diags.has_errors()) return;
        compile_params();
        if (!best_effort && diags.has_errors()) return;
        compile_rules();
        if (!best_effort && diags.has_errors()) return;
        register_sequences();
        for (size_t i = 0; i < ast.sequences.size(); ++i)
            compile_stmts(ast.sequences[i].stmts, out.sequences[i].stmts, ast.sequences[i].name);
        check_sequence_cycles();
        if (diags.has_errors()) return;   // the warning walks sequences: cycle-free only
        for (auto const& sq : out.sequences) warn_unstable_fixpoints(sq.stmts);
    }
};

}  // namespace

bool analyze(module_closure const& mods, compiled& out, diagnostics& diags,
             bool best_effort) {
    analyzer a{mods.merged, out, diags, mods, {}, {}, {}};
    a.run(best_effort);
    return !diags.has_errors();
}

}  // namespace ls
