#include "sema.hpp"
#include "modules.hpp"
#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace ls {

namespace {

// The largest side of a sized pattern `g(W, H)[c]`; a written-out pattern has
// no cap, but a size is one token and could otherwise ask for billions of cells.
constexpr int max_pattern_side = 256;

// ── pattern transforms ───────────────────────────────────────────────────────
//
// Applied at compile time to expand symmetry/rotation variants. The both-axis
// flip IS the 180° rotation, so flip_both doubles as rot180; the per-sub-rule
// dedup below collapses the coincidence.

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

// Split a match-side pattern's cells into the matcher's two phases, in the
// variant's row-major order.
void index_probes(compiled_pattern& p) {
    p.bare.clear();
    p.bare_ne.clear();
    p.computed.clear();
    for (int r = 0; r < p.rows; ++r)
        for (int c = 0; c < p.cols; ++c) {
            auto const& cell = p.at(r, c);
            if (cell.what == compiled_cell::kind::value && !p.is_where)
                (cell.ne ? p.bare_ne : p.bare).push_back({r, c, cell.val, -1});
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
    if (a.grid_id != b.grid_id || a.graph_id != b.graph_id || a.rows != b.rows ||
        a.cols != b.cols) return false;
    for (int i = 0; i < (int)a.cells.size(); ++i)
        if (a.cells[i].what != b.cells[i].what || a.cells[i].val != b.cells[i].val ||
            a.cells[i].expr != b.cells[i].expr ||  // transforms share arena indices
            a.cells[i].var != b.cells[i].var ||    // and variable slots, i.e. names
            a.cells[i].ne != b.cells[i].ne)
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

// Variant dedup key: two variants are duplicates iff both the
// match side and the write tree are structurally equal. A same-LHS variant
// with different writes survives as its own candidate.
bool pairs_equal(compiled_pair const& a, compiled_pair const& b) {
    if (a.lhs.size() != b.lhs.size()) return false;
    for (int i = 0; i < (int)a.lhs.size(); ++i)
        if (!patterns_equal(a.lhs[i], b.lhs[i])) return false;
    return write_terms_equal(a.rhs, b.rhs);
}

// The kinds of value an expression can have.
enum class val_type { num, mask, boolean };

// ── name tables ───────────────────────────────────────────────────────────────
//
// Every closed set the language resolves by name (built-ins, operations,
// attribute values, enum words) is a table of rows keyed by `name`; one
// lookup serves them all, and diagnostics list the allowed keys from the
// same rows.


inline std::string key_text(char const* k) { return k; }
inline std::string key_text(long long k)   { return std::to_string(k); }

template <class Table>
std::string allowed_keys(Table const& table) {
    std::string out;
    for (auto const& row : table) out += (out.empty() ? "" : ", ") + key_text(row.name);
    return out;
}

// Rule attributes: the variants each `symmetry` value adds, and each
// `rotation` angle's transform. symmetry=all is four variants: identity + H +
// V + both-axis; the both-axis flip IS the 180° rotation, and the
// per-sub-rule dedup collapses the coincidence.
using mirror = compiled_pair::mirror;
struct symmetry_row {
    char const* name;
    std::vector<std::pair<transform, mirror>> variants;
};
static const symmetry_row symmetries[] = {
    {"none",       {{transform::identity, mirror::none}}},
    {"horizontal", {{transform::identity, mirror::none}, {transform::flip_h, mirror::h}}},
    {"vertical",   {{transform::identity, mirror::none}, {transform::flip_v, mirror::v}}},
    {"all",        {{transform::identity, mirror::none}, {transform::flip_h, mirror::h},
                    {transform::flip_v, mirror::v}, {transform::flip_both, mirror::both}}},
};
struct rotation_row { long long name; transform k; };
static const rotation_row rotations[] = {
    {90, transform::rot90}, {180, transform::flip_both}, {270, transform::rot270},
};

// The unary and binary operators, with their operand and result types. A
// `same_kind` operator takes two numbers or two tag values.
struct operator_row {
    expr_kind   name;
    ce_kind     code;
    val_type    operand;
    bool        same_kind;
    val_type    result;
    char const* message;
};
static constexpr char const* arith = "arithmetic operator requires numeric operands";
static constexpr char const* rel   = "relational operator requires numeric operands";
static constexpr char const* logic = "'&&' / '||' require boolean operands";
static constexpr char const* equal = "'==' / '!=' require two numbers or two tag values";
static const operator_row operators[] = {
    {expr_kind::neg,    ce_kind::neg,    val_type::num,     false, val_type::num,     "unary '-' requires a number"},
    {expr_kind::not_,   ce_kind::not_,   val_type::boolean, false, val_type::boolean, "'!' requires a boolean"},
    {expr_kind::add,    ce_kind::add,    val_type::num,     false, val_type::num,     arith},
    {expr_kind::sub,    ce_kind::sub,    val_type::num,     false, val_type::num,     arith},
    {expr_kind::mul,    ce_kind::mul,    val_type::num,     false, val_type::num,     arith},
    {expr_kind::div_,   ce_kind::div_,   val_type::num,     false, val_type::num,     arith},
    {expr_kind::lt,     ce_kind::lt,     val_type::num,     false, val_type::boolean, rel},
    {expr_kind::le,     ce_kind::le,     val_type::num,     false, val_type::boolean, rel},
    {expr_kind::gt,     ce_kind::gt,     val_type::num,     false, val_type::boolean, rel},
    {expr_kind::ge,     ce_kind::ge,     val_type::num,     false, val_type::boolean, rel},
    {expr_kind::eq,     ce_kind::eq,     val_type::num,     true,  val_type::boolean, equal},
    {expr_kind::ne,     ce_kind::ne,     val_type::num,     true,  val_type::boolean, equal},
    {expr_kind::and_,   ce_kind::and_,   val_type::boolean, false, val_type::boolean, logic},
    {expr_kind::or_,    ce_kind::or_,    val_type::boolean, false, val_type::boolean, logic},
    {expr_kind::bit_or, ce_kind::bit_or, val_type::mask,    false, val_type::mask,
     "'|' (tag union) requires tag-valued operands"},
};

// The `mirror` operation's axis: 1 = horizontal.
struct axis_row { char const* name; int w; };
static const axis_row mirror_axes[] = {{"horizontal", 1}, {"vertical", 0}};
// The `voronoi` operation's metric: 0 = euclidean, else the step adjacency.
static const axis_row voronoi_metrics[] = {{"euclidean", 0}, {"manhattan", 4}, {"chebyshev", 8}};
// The `noise` operation's normalization: 1 = rank.
static const axis_row noise_normalizations[] = {{"rank", 1}, {"range", 0}};

// Identifier scope for the position being compiled. Cell/where exprs are
// permissive; statement exprs and param exprs are restricted.
struct scope {
    bool allow_grids{true};
    bool allow_pos{true};      // x / y / width / height
    int  param_limit{-1};      // -1 = any param; else only ids < limit
};
// Statement scope (guards, counts, `int` arguments): params only - a statement
// has no cell to read grids or positions from.
constexpr scope stmt_scope{false, false, -1};

struct analyzer {
    ast_file const&       ast;   // the closure's merged declarations
    compiled&             out;
    diagnostics&          diags;
    module_closure const& mods;

    // Declaring module of each table entry, aligned with out.tag_names,
    // out.layers and out.param_names (rules and sequences align with the AST).
    std::vector<int> tag_mod, layer_mod, graph_mod, param_mod;

    scope scope_{};

    // Pattern variables (`?name`) of the pair being compiled; null outside
    // a pattern pair, where using one is an error. A slot's type is the
    // binding cells' grid type: a tag id, or -1 for number.
    struct pair_vars {
        struct var {
            std::string name;          // '?name'
            int         type{-1};
            bool        bound{false};  // has a binding cell (a bare match-side cell)
            bool        reported{false};
        };
        std::vector<var> vars;
        int slot(std::string const& name) {
            int i = index_of(vars, name, &var::name);
            if (i >= 0) return i;
            vars.push_back({name});
            return (int)vars.size() - 1;
        }
    };
    pair_vars* vars_{nullptr};

    // The graph whose pattern cell is being compiled; -1 elsewhere, where
    // calling a graph function is an error.
    int graph_{-1};

    std::string type_name(int type) const {
        return type < 0 ? "number" : "tagset '" + out.tag_names[(size_t)type] + "'";
    }

    // The tagsets this module sees that declare `name` as a value or union.
    std::vector<int> tagsets_declaring(std::string const& name, int mod) const {
        std::vector<int> found;
        for (int ti = 0; ti < (int)out.tag_names.size(); ++ti)
            if (sees(mod, tag_mod[(size_t)ti]) && out.mask_of(ti, name) != 0)
                found.push_back(ti);
        return found;
    }

    // A use of `name` in an expression or a write: its slot, or -1 after
    // reporting it (outside a pattern pair, or never bound).
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
        diags.error(label(loc), loc.line, loc.col, loc.end_line, loc.end_col, std::move(msg));
    }
    // A word or number outside its table: "invalid <what> '<got>'; allowed: ...".
    template <class Table>
    void invalid(source_loc loc, char const* what, std::string const& got, Table const& table) {
        error(loc, std::string("invalid ") + what + " '" + got + "'; allowed: " +
              allowed_keys(table));
    }
    void warning(source_loc loc, std::string msg) {
        diags.warning(label(loc), loc.line, loc.col, loc.end_line, loc.end_col, std::move(msg));
    }

    // Visibility: a module sees itself and the modules it uses directly.
    // A name declared in the closure but not seen is an error, reported with
    // its declaring module so the fix is in the message.
    bool sees(int from, int decl) const { return mods.sees[(size_t)from][(size_t)decl] != 0; }
    void require_visible(source_loc at, char const* kind, std::string const& name, int decl) {
        if (sees(at.mod, decl)) return;
        error(at, std::string(kind) + " '" + name + "' is not visible here: it is declared in "
              "module '" + mods.names[(size_t)decl] + "', which this module does not use");
    }

    // ── expressions ──────────────────────────────────────────────────────────

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

    // Report an expression error and stand in a number so nothing cascades.
    int poison(source_loc loc, std::string msg, val_type& t) {
        error(loc, std::move(msg));
        t = val_type::num;
        return num_lit();
    }

    // Compile `e` under scope `sc`, reporting `msg` at `at` unless it has
    // type `want`. A bare '.' takes `want` as its type when `type_empty`.
    int compile_as(expr const& e, val_type want, source_loc at, std::string const& msg,
                   scope sc = {}, int ctx_tag = -1, bool type_empty = true) {
        scope saved = scope_;
        scope_ = sc;
        val_type t;
        int c = compile_expr(e, ctx_tag, t, type_empty ? (int)want : -1);
        scope_ = saved;
        if (t != want) error(at, msg);
        return c;
    }

    static bool is_reserved_ident(std::string const& n) {
        return n == "x" || n == "y" || n == "width" || n == "height";
    }

    struct builtin_spec { char const* name; ce_kind kind; int arity; bool graph; };
    static constexpr builtin_spec builtins[] = {
        {"if",        ce_kind::if_,        3, false},
        {"min",       ce_kind::min_,       2, false},
        {"max",       ce_kind::max_,       2, false},
        {"abs",       ce_kind::abs_,       1, false},
        {"clamp",     ce_kind::clamp_,     3, false},
        {"random",    ce_kind::random_,    2, false},
        // graph functions: a selector argument, if any
        {"degree",    ce_kind::degree_,    0, true},
        {"size",      ce_kind::size_,      0, true},
        {"distance",  ce_kind::distance_,  1, true},
        {"neighbors", ce_kind::neighbors_, 1, true},
    };

    // Resolve a bare identifier, first match wins: x/y/width/height → param → tag
    // value or union of the enclosing cell's tagset → grid → a tag value or
    // union of the one tagset that declares it.
    int compile_ident(expr const& e, int ctx_tag, val_type& t) {
        auto const& name = e.ident;
        if (is_reserved_ident(name)) {
            if (!scope_.allow_pos)
                return poison(e.loc, "'" + name + "' cannot be read here "
                      "(position/dimension is not available at this scope)", t);
            t = val_type::num;
            ce_kind k = name == "x" ? ce_kind::pos_x
                      : name == "y" ? ce_kind::pos_y
                      : name == "width" ? ce_kind::width : ce_kind::height;
            return add_expr({k, 0, -1, -1, -1, -1});
        }
        int pid = out.param_id(name);
        if (pid >= 0) {
            require_visible(e.loc, "param", name, param_mod[(size_t)pid]);
            if (scope_.param_limit >= 0 && pid >= scope_.param_limit)
                return poison(e.loc, "param '" + name + "' is referenced before it is declared", t);
            t = val_type::num;
            return add_expr({ce_kind::param_read, 0, pid, -1, -1, -1});
        }
        if (ctx_tag >= 0) {   // tag value or union of the enclosing cell's tagset
            int64_t m = out.mask_of(ctx_tag, name);
            if (m != 0) {
                t = val_type::mask;
                return add_expr({ce_kind::mask_lit, m, -1, -1, -1, -1});
            }
        }
        int gid = out.layer_id(name);
        if (gid >= 0) {
            require_visible(e.loc, "grid", name, layer_mod[(size_t)gid]);
            if (graph_ >= 0)
                return poison(e.loc, "a graph-pattern cell reads no grid: to test '" + name +
                              "' too, conjoin a '" + name + "[...]' pattern on the match side", t);
            if (!scope_.allow_grids)
                return poison(e.loc, "grid '" + name + "' cannot be read here "
                              "(only params are available at this scope)", t);
            t = out.layers[gid].tag_id < 0 ? val_type::num : val_type::mask;
            return add_expr({ce_kind::grid_read, 0, gid, -1, -1, -1});
        }
        if (int grid = out.graph_id(name); grid >= 0) {
            require_visible(e.loc, "graph", name, graph_mod[(size_t)grid]);
            return poison(e.loc, "graph '" + name + "' cannot be read in an expression; "
                          "a graph heads a match-side pattern, e.g. " + name + "[ (degree() > 1) ]", t);
        }
        // a tag value or union of the one tagset this module sees that declares it
        auto tagsets = tagsets_declaring(name, e.loc.mod);
        if (tagsets.size() == 1) {
            t = val_type::mask;
            return add_expr({ce_kind::mask_lit, out.mask_of(tagsets[0], name), -1, -1, -1, -1});
        }
        if (tagsets.size() > 1) {
            std::string list;
            for (int ti : tagsets) list += (list.empty() ? "'" : ", '") + out.tag_names[(size_t)ti] + "'";
            return poison(e.loc, "tag value '" + name + "' is ambiguous: it is declared by " +
                          "tagsets " + list + "; compare it in a cell of the grid it belongs to", t);
        }
        return poison(e.loc, "unknown identifier '" + name + "' in expression", t);
    }

    int compile_call(expr const& e, int ctx_tag, val_type& t, int hint) {
        auto spec = lookup(builtins, e.ident);
        auto fail = [&](std::string msg) { return poison(e.loc, std::move(msg), t); };
        if (!spec) return fail("unknown function '" + e.ident + "'");
        if ((int)e.args.size() != spec->arity)
            return fail("built-in '" + e.ident + "' takes " +
                        std::to_string(spec->arity) + " argument(s), got " +
                        std::to_string(e.args.size()));

        if (spec->graph) {
            if (graph_ < 0)
                return fail("'" + e.ident + "' is a graph function; it is valid only in a "
                            "graph-pattern cell, e.g. map[ (" + e.ident + "(" +
                            (spec->arity ? "rooms[Start]" : "") + ") > 0) ]");
            int sel = -1;
            if (spec->arity == 1) {
                if (e.args[0]->kind != expr_kind::selector)
                    return fail("'" + e.ident + "' takes a selector, a one-cell pattern "
                                "such as " + e.ident + "(rooms[Start])");
                sel = compile_selector(*e.args[0]);
            }
            t = val_type::num;
            return add_expr({spec->kind, sel, graph_, -1, -1, -1});
        }

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

    // A selector `g[m]`: its grid, and the one cell, tested like a pattern
    // cell of that grid. -1 after reporting an error.
    int compile_selector(expr const& e) {
        auto const& name = e.ident;
        compiled_selector s;
        s.grid = out.layer_id(name);
        if (s.grid < 0) {
            if (out.graph_id(name) >= 0)
                error(e.loc, "graph '" + name + "' cannot be a selector's grid; a selector "
                      "reads a grid, such as the graph's source");
            else
                error(e.loc, "undeclared grid '" + name + "' in a selector");
            return -1;
        }
        require_visible(e.loc, "grid", name, layer_mod[(size_t)s.grid]);
        if (e.args.empty()) return -1;   // a parse error, reported
        expr const& cell = *e.args[0];
        int tag = out.layers[(size_t)s.grid].tag_id;
        bool is_int = cell.kind == expr_kind::int_lit;
        bool empty  = cell.kind == expr_kind::empty_lit;
        bool not_empty = cell.kind == expr_kind::not_ && cell.args[0]->kind == expr_kind::empty_lit;
        if (tag >= 0 && is_int) {
            error(cell.loc, "an integer selector cell needs a number grid; '" + name +
                  "' is a tag grid");
            return -1;
        }
        if (tag >= 0) {
            s.val = tag_mask(cell, tag, "for grid '" + name + "'");
        } else if (is_int || empty || not_empty) {
            s.val = is_int ? cell.int_val : num_empty;
            s.ne  = not_empty;
        } else {
            error(cell.loc, "a selector cell on number grid '" + name + "' is an integer, "
                  "'.', or '!.'");
            return -1;
        }
        out.selectors.push_back(s);
        return (int)out.selectors.size() - 1;
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
        case expr_kind::variable: {   // `?name`: what its binding cell matched
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
        case expr_kind::selector:
            return poison(e.loc, "a selector is valid only as the argument of 'distance' "
                          "or 'neighbors'", t);
        default:
            break;
        }

        // `g == .` / `g != .` — the emptiness tests, before generic binary
        // compilation so the empty literal needs no independent type.
        if (e.kind == expr_kind::eq || e.kind == expr_kind::ne) {
            bool a_empty = e.args[0]->kind == expr_kind::empty_lit;
            bool b_empty = e.args[1]->kind == expr_kind::empty_lit;
            if (a_empty && b_empty) {
                int c = poison(e.loc, "'. == .' has no inferable type", t);
                t = val_type::boolean;
                return c;
            }
            if (a_empty || b_empty) {
                val_type to;
                int oc = compile_expr(a_empty ? *e.args[1] : *e.args[0], ctx_tag, to);
                t = val_type::boolean;
                if (out.exprs[oc].kind == ce_kind::distance_)   // unreachable stays empty
                    out.exprs[oc].kind = ce_kind::distance_raw;
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

        auto op = lookup(operators, e.kind);
        if (!op) return poison(e.loc, "unsupported expression", t);
        int c[2] = {-1, -1};
        val_type ts[2];
        for (size_t i = 0; i < e.args.size(); ++i)
            c[i] = compile_expr(*e.args[i], ctx_tag, ts[i]);
        bool ok = true;
        for (size_t i = 0; i < e.args.size(); ++i)
            ok &= op->same_kind ? ts[i] == ts[0] && ts[0] != val_type::boolean
                                : ts[i] == op->operand;
        if (!ok) error(e.loc, op->message);
        t = op->result;
        return add_expr({op->code, 0, -1, c[0], c[1], -1});
    }

    // One collision discipline for every named declaration: reserved position
    // idents are caught here with one message shape. Built-in names are not
    // reserved: a call is `IDENT '('`, and tag values, grids, and params are
    // never called.
    void check_name(source_loc loc, std::string const& kind, std::string const& name) {
        if (is_reserved_ident(name))
            error(loc, kind + " '" + name + "' collides with a reserved "
                  "expression identifier (x, y, width, height)");
    }

    // ── symbol tables ────────────────────────────────────────────────────────

    void build_tables() {
        for (auto const& t : ast.tags) {
            std::string const& tname = t.name.text;
            if (out.tag_id(tname) >= 0)
                error(t.name.loc, "duplicate tag '" + tname + "'");
            if (t.values.empty() && t.unions.empty())
                error(t.name.loc, "tagset '" + tname + "' has no values");
            if ((int)t.values.size() > 30)   // bits 1..30; 0 = empty, 31 reserved
                error(t.name.loc, "tagset '" + tname + "' has " +
                      std::to_string(t.values.size()) + " values; the maximum is 30");
            std::vector<std::string> vals;
            for (auto const& [name, loc] : t.values) {
                check_name(loc, "tag value", name);
                if (index_of(vals, name) >= 0)
                    error(loc, "duplicate tag value '" + name + "' in '" + tname + "'");
                vals.push_back(name);
            }
            out.tag_names.push_back(tname);
            out.tag_values.push_back(std::move(vals));
            tag_mod.push_back(t.loc.mod);

            // Named unions: resolve in declaration order; a member is a
            // value or an earlier union of this tagset; a union shares the
            // value namespace and consumes no bit.
            int tid = (int)out.tag_names.size() - 1;
            out.tag_unions.push_back({});
            for (auto const& u : t.unions) {
                auto const& [name, loc] = u.name;
                check_name(loc, "union", name);
                if (out.value_id(tid, name) >= 0 || out.mask_of(tid, name) != 0)
                    error(loc, "union '" + name +
                          "' redeclares a tag value or union in '" + tname + "'");
                int64_t mask = 0;
                for (auto const& m : u.members) {
                    int64_t mm = out.mask_of(tid, m);
                    if (mm == 0)
                        error(loc, "union '" + name + "' references '" + m +
                              "', which is not a value or earlier union of '" +
                              tname + "'");
                    mask |= mm;
                }
                out.tag_unions[tid].push_back({name, mask});
            }
        }

        for (auto const& l : ast.layers.layers) {
            if (l.is_graph) continue;   // after every grid: a source may come later
            auto const& [name, loc] = l.name;
            check_name(loc, "grid", name);
            if (out.layer_id(name) >= 0) {   // across every module in the closure
                error(loc, "duplicate grid '" + name + "'");
                continue;
            }
            int tag = -1;
            if (l.type != "number") {
                tag = out.tag_id(l.type);
                if (tag < 0)
                    error(loc, "grid '" + name +
                          "' references undeclared tag '" + l.type + "'");
                else
                    require_visible(loc, "tagset", l.type, tag_mod[(size_t)tag]);
            }
            out.layers.push_back({name, tag});
            layer_mod.push_back(loc.mod);
        }

        for (auto const& l : ast.layers.layers)
            if (l.is_graph) add_graph(l);

        for (auto const& p : ast.params) {
            auto const& [name, loc] = p.name;
            check_name(loc, "param", name);
            if (out.layer_id(name) >= 0)
                error(loc, "param '" + name + "' collides with a grid of the same name");
            if (out.graph_id(name) >= 0)
                error(loc, "param '" + name + "' collides with a graph of the same name");
            if (out.param_id(name) >= 0) {
                error(loc, "duplicate param '" + name + "'");
                continue;
            }
            // Every input param must carry a default.
            if (!p.is_derived && !p.value)
                error(loc, "input param '" + name +
                      "' must have a default, e.g. '" + name + ": number = 0'");
            out.param_names.push_back(name);
            param_mod.push_back(loc.mod);
        }
    }

    // The value bits of tagset `tag`: every value, no empty bit.
    int64_t value_bits(int tag) const {
        int64_t full = 0;
        for (int i = 0; i < (int)out.tag_values[(size_t)tag].size(); ++i) full |= tag_bit(i);
        return full;
    }

    // A whitespace-free mask over tagset `tag`: its atoms' union, each `!a`
    // the complement of `a` within the tagset. Unknown atoms are reported
    // with `whose` ("for this grid") and add nothing.
    int64_t tag_mask(expr const& e, int tag, std::string const& whose) {
        std::vector<mask_atom> atoms;
        mask_atoms(e, atoms);
        int64_t mask = 0;
        for (auto const& a : atoms) {
            int64_t m = a.name == "." ? tag_empty : out.mask_of(tag, a.name);
            if (m == 0) {
                error(a.loc, "unknown tag value '" + a.name + "' " + whose);
                continue;
            }
            mask |= a.negate ? value_bits(tag) & ~m : m;
        }
        return mask;
    }

    // A graph entry: its source grid, form, and masks.
    void add_graph(layer_decl const& l) {
        auto const& [name, loc] = l.name;
        check_name(loc, "graph", name);
        if (out.layer_id(name) >= 0 || out.graph_id(name) >= 0) {
            error(loc, "duplicate layer '" + name + "': a grid or graph of that name is "
                  "already declared");
            return;
        }
        compiled_graph g;
        g.name = name;
        g.source = out.layer_id(l.type);
        auto is_graph = [&](layer_decl const& o) { return o.is_graph && o.name.text == l.type; };
        if (g.source < 0) {
            if (std::any_of(ast.layers.layers.begin(), ast.layers.layers.end(), is_graph))
                error(loc, "graph '" + name + "' is built from graph '" + l.type +
                      "'; a graph's source must be a grid");
            else if (out.tag_id(l.type) >= 0)
                error(loc, "graph '" + name + "' is built from tagset '" + l.type +
                      "'; a graph's source must be a grid");
            else
                error(loc, "graph '" + name + "' references undeclared grid '" + l.type + "'");
        } else {
            require_visible(loc, "grid", l.type, layer_mod[(size_t)g.source]);
        }
        out.graphs.push_back(g);
        graph_mod.push_back(loc.mod);
        if (g.source < 0) return;

        int tag = out.layers[(size_t)g.source].tag_id;
        graph_arg const* nodes = nullptr;
        graph_arg const* edges = nullptr;
        for (auto const& a : l.args) {
            bool is_nodes = a.name == "nodes";
            if (!is_nodes && !(a.name == "edges")) {
                error(a.name.loc, "unknown graph argument '" + a.name.text +
                      "'; allowed: nodes, edges");
                continue;
            }
            graph_arg const*& slot = is_nodes ? nodes : edges;
            if (slot) error(a.name.loc, "graph argument '" + a.name.text + "' given twice");
            else slot = &a;
        }
        if (tag < 0) {
            if (!l.args.empty())
                error(l.args[0].name.loc, "graph '" + name + "' is built from number grid '" +
                      l.type + "', which takes no 'nodes' or 'edges'; write 'graph of " +
                      l.type + "'");
            return;
        }
        if (edges && !nodes)
            error(edges->name.loc, "graph '" + name + "' has 'edges' without 'nodes'");

        int64_t full = value_bits(tag);
        std::string whose = "in a mask of graph '" + name + "': its source '" + l.type +
                            "' holds tagset '" + out.tag_names[(size_t)tag] + "'";
        auto mask = [&](graph_arg const* a) -> int64_t {
            int64_t m = tag_mask(*a->mask, tag, whose) & full;
            if (m == 0)
                error(a->mask->loc, "the '" + a->name.text + "' mask of graph '" + name +
                      "' selects no value");
            return m;
        };
        g.nodes = nodes ? mask(nodes) : full;
        g.edges = edges ? mask(edges) : 0;
        if (int64_t shared = g.nodes & g.edges) {
            std::string list;
            for (int i = 0; i < (int)out.tag_values[(size_t)tag].size(); ++i)
                if (shared & tag_bit(i))
                    list += (list.empty() ? "'" : ", '") + out.tag_values[(size_t)tag][(size_t)i] + "'";
            error(edges->mask->loc, "the 'nodes' and 'edges' masks of graph '" + name +
                  "' share " + list + "; a cell is a node or a connector, not both");
        }
        out.graphs.back() = g;
    }

    // Param startup expressions: derived params and input defaults share
    // one discipline — earlier params only, no grids, no position/dimensions.
    void compile_params() {
        for (int i = 0; i < (int)ast.params.size(); ++i) {
            auto const& p = ast.params[i];
            if (!p.value) continue;   // missing default already reported
            int e = compile_as(*p.value, val_type::num, p.name.loc,
                               std::string(p.is_derived ? "derived param '" : "default for param '")
                               + p.name.text + "' must evaluate to a number", scope{false, false, i});
            out.startup_exprs.push_back({i, e, !p.is_derived});
        }
    }

    // ── rules ────────────────────────────────────────────────────────────────

    // Compile one pattern; `exp_rows/exp_cols` enforce the shape constraint
    // (every pattern of a pair has one shape, recursively over the write tree) —
    // 0 means "sets the reference".
    compiled_pattern compile_pattern(pattern const& p, int exp_rows, int exp_cols,
                                     bool is_rhs) {
        compiled_pattern cp;
        int tag = -1;
        if (p.is_where) {
            cp.grid_id  = where_grid;
            cp.is_where = true;
            if (is_rhs)
                error(p.loc, "'where' is a match-side pseudo-layer; it cannot "
                      "appear on the write side");
        } else if (int gid = out.graph_id(p.grid.text); gid >= 0) {
            require_visible(p.grid.loc, "graph", p.grid.text, graph_mod[(size_t)gid]);
            cp.graph_id = gid;
            cp.grid_id  = out.graphs[(size_t)gid].source;
            if (is_rhs) {
                error(p.grid.loc, "graph '" + p.grid.text + "' cannot be written; a graph "
                      "changes only through writes to its source grid");
                cp.grid_id = -1;
            }
        } else {
            cp.grid_id = out.layer_id(p.grid.text);
            if (cp.grid_id < 0) {
                error(p.grid.loc, "undeclared grid '" + p.grid.text + "'");
                return cp;
            }
            require_visible(p.grid.loc, "grid", p.grid.text, layer_mod[(size_t)cp.grid_id]);
            tag = out.layers[cp.grid_id].tag_id;
            cp.is_number = tag < 0;
        }
        // A sized pattern's shape is its declared size, not its body's.
        bool size_ok = !p.sized ||
                       (p.size_cols >= 1 && p.size_cols <= max_pattern_side &&
                        p.size_rows >= 1 && p.size_rows <= max_pattern_side);
        if (!size_ok)
            error(p.size_loc, "a pattern size must be between 1 and " +
                  std::to_string(max_pattern_side) + " on each side");
        int rows = p.sized ? (int)p.size_rows : p.rows;
        int cols = p.sized ? (int)p.size_cols : p.cols;

        if (size_ok && exp_rows > 0 && (rows != exp_rows || cols != exp_cols))
            error(p.loc, "pattern dimension mismatch: expected " +
                  std::to_string(exp_rows) + "x" + std::to_string(exp_cols) +
                  ", got " + std::to_string(rows) + "x" + std::to_string(cols));

        if (p.rows == 0 || p.cols == 0) {
            error(p.loc, "empty pattern body");
            return cp;
        }
        if (!size_ok) return cp;
        if (p.sized) {   // one cell, compiled once and repeated over the shape
            if (p.rows != 1 || p.cols != 1 || p.cells[0].size() != 1) {
                error(p.loc, "a sized pattern repeats a single cell, but this body has " +
                      std::to_string(p.rows) + " row(s) of " + std::to_string(p.cols) +
                      " cell(s)");
                return cp;
            }
            expr const& in = *p.cells[0][0];
            compiled_cell cell = cp.graph_id >= 0
                ? compile_graph_cell(in, cp.graph_id)
                : compile_cell(in, tag, cp.is_number, p.is_where, is_rhs);
            cp.rows = rows;
            cp.cols = cols;
            cp.cells.assign((size_t)rows * cols, cell);
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
                    cp.graph_id >= 0 ? compile_graph_cell(*p.cells[r][c], cp.graph_id)
                                     : compile_cell(*p.cells[r][c], tag, cp.is_number,
                                                    p.is_where, is_rhs);
        }
        return cp;
    }

    // The atoms of a bare mask cell `a|!b|.`: each a name ("." for empty)
    // and whether it is complemented.
    struct mask_atom { source_loc loc; std::string name; bool negate; };
    static void mask_atoms(expr const& e, std::vector<mask_atom>& out) {
        if (e.kind == expr_kind::bit_or) {
            mask_atoms(*e.args[0], out);
            mask_atoms(*e.args[1], out);
            return;
        }
        bool negate = e.kind == expr_kind::not_;
        expr const& a = negate ? *e.args[0] : e;
        out.push_back({e.loc, a.kind == expr_kind::empty_lit ? "." : a.ident, negate});
    }

    // A cell's compiled form. Every constant cell - a bare one, or a
    // parenthesized literal like `(wall)` - becomes a value the matcher tests
    // with one mask or number comparison; only a computed cell is evaluated.
    compiled_cell compile_cell(expr const& in, int tag, bool is_number,
                               bool is_where, bool is_rhs) {
        compiled_cell cc;

        if (is_where) {   // where cells are parenthesized booleans or '*'
            if (in.kind == expr_kind::wildcard) {   // no condition
                cc.what = compiled_cell::kind::wildcard;
                return cc;
            }
            cc.what = compiled_cell::kind::expr;
            if (in.kind == expr_kind::variable) {
                error(in.loc, "a 'where' cell cannot be a bare pattern variable; "
                      "'where' cells are boolean expressions, e.g. (" + in.ident + " > 0)");
                cc.expr = num_lit(1);
            } else if (in.kind != expr_kind::paren) {
                error(in.loc, "'where' cells must be '*' or a parenthesized boolean expression");
                cc.expr = num_lit();
            } else {
                cc.expr = compile_as(*in.args[0], val_type::boolean, in.loc,
                                     "'where' cell must evaluate to a boolean", scope_);
            }
            return cc;
        }

        switch (in.kind) {
        case expr_kind::wildcard:
            cc.what = compiled_cell::kind::wildcard;
            return cc;
        case expr_kind::empty_lit:
            cc.val = is_number ? num_empty : tag_empty;
            return cc;
        case expr_kind::int_lit:
            if (!is_number)
                error(in.loc, "integer cell in a tag grid");
            cc.val = in.int_val;
            return cc;
        case expr_kind::variable: {   // bound and typed by bind_variables
            int s = is_rhs ? use_variable(in.ident, in.loc) : vars_->slot(in.ident);
            if (s < 0) { cc.what = compiled_cell::kind::wildcard; return cc; }
            int type = vars_->vars[(size_t)s].type;
            if (is_rhs && type != tag)
                error(in.loc, "pattern variable '" + in.ident + "' holds a " +
                      type_name(type) + " value but is written into a " +
                      type_name(tag) + " grid");
            cc.what = compiled_cell::kind::variable;
            cc.var = s;
            return cc;
        }
        case expr_kind::paren: {
            int e = compile_as(*in.args[0], is_number ? val_type::num : val_type::mask, in.loc,
                               is_number ? "expression in a 'number' grid cell must evaluate to a number"
                                         : "expression in a tag grid cell must evaluate to a tag value",
                               scope_, tag);
            auto const& ce = out.exprs[(size_t)e];
            if (ce.kind == ce_kind::int_lit || ce.kind == ce_kind::mask_lit)
                cc.val = ce.val;   // a literal: tested like a bare cell
            else {
                cc.what = compiled_cell::kind::expr;
                cc.expr = e;
            }
            return cc;
        }
        default:
            break;
        }

        // a bare mask: ident, '.', '!', and '|' (the parser builds nothing else)
        std::vector<mask_atom> atoms;
        mask_atoms(in, atoms);
        bool has_empty = false;
        for (auto const& a : atoms) has_empty |= a.name == ".";
        if (is_number) {   // a number cell pattern is only '.' or '!.'
            if (atoms.size() == 1 && atoms[0].name == "." && atoms[0].negate) {
                if (is_rhs)
                    error(in.loc, "'!.' is not allowed on the write side");
                cc.val = num_empty;
                cc.ne = true;
            } else if (has_empty) {
                error(in.loc, "a 'number' grid has no masks: '.' combines with "
                      "nothing here (write '.' or '!.')");
            } else {
                error(in.loc, "expected an integer or wildcard in a 'number' grid cell");
            }
            return cc;
        }
        if (is_rhs && has_empty && atoms.size() > 1)
            error(in.loc, "'.' cannot be written in a union: a cell holds empty or "
                  "values, never both");
        if (is_rhs)   // complement is match-side only
            for (auto const& a : atoms)
                if (a.negate)
                    error(a.loc, "tag complement '!" + a.name +
                          "' is not allowed on the write side");
        cc.val = tag_mask(in, tag, "for this grid");
        return cc;
    }

    // A graph-pattern cell: '*', or a boolean expression about the node
    // covering the cell, which reads no grid.
    compiled_cell compile_graph_cell(expr const& in, int graph) {
        compiled_cell cc;
        if (in.kind == expr_kind::wildcard) {
            cc.what = compiled_cell::kind::wildcard;
            return cc;
        }
        cc.what = compiled_cell::kind::expr;
        if (in.kind == expr_kind::variable) {
            error(in.loc, "a graph-pattern cell cannot be a bare pattern variable; its cells "
                  "are boolean expressions, e.g. (size() == " + in.ident + ")");
            cc.expr = num_lit(1);
        } else if (in.kind != expr_kind::paren) {
            error(in.loc, "graph-pattern cells must be '*' or a parenthesized boolean "
                  "expression about the node, e.g. (degree() == 1)");
            cc.expr = num_lit();
        } else {
            int saved = graph_;
            graph_ = graph;
            cc.expr = compile_as(*in.args[0], val_type::boolean, in.loc,
                                 "a graph-pattern cell must evaluate to a boolean",
                                 scope{false, true, -1});
            graph_ = saved;
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
        if (ct.what == compiled_write_term::kind::any && !ct.items.empty() &&
            std::none_of(ct.items.begin(), ct.items.end(),
                         [](compiled_write_term const& it) { return it.weight > 0; }))
            error(t.loc, "every item of this '{ any }' has weight 0; at least one needs a "
                  "positive weight to be picked");
        return ct;
    }

    // Within one { all } write block, two items may not write the same grid at
    // the same cell — a write-write ambiguity.
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
    // one variable's binding cells must share a grid type. Runs before any
    // cell compiles, so a variable may be used before its binding cell.
    void bind_variables(rule_pair const& pr, pair_vars& pv) {
        for (auto const& p : pr.lhs) {
            if (p.is_where) continue;   // a bare variable there is an error, reported by its cell
            int gid = out.layer_id(p.grid.text);
            if (gid < 0) continue;      // reported by compile_pattern
            int type = out.layers[gid].tag_id;
            for (auto const& row : p.cells)
                for (auto const& c : row) {
                    if (c->kind != expr_kind::variable) continue;
                    auto& v = pv.vars[(size_t)pv.slot(c->ident)];
                    if (!v.bound) { v.bound = true; v.type = type; continue; }
                    if (v.type != type && !v.reported) {
                        error(c->loc, "pattern variable '" + c->ident + "' binds a " +
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
            if (index_of(out.rules, r.name, &compiled_rule::name) >= 0)
                error(r.name.loc, "duplicate rule '" + r.name.text + "'");
            out.rules.push_back(compile_rule(r));
            out.rules.back().name = r.name.text;
        }
    }

    // One rule's attributes and body, expanded into its variants.
    compiled_rule compile_rule(rule_decl const& r) {
        // attributes; an invalid one is reported and adds no variants
        auto sym = lookup(symmetries, r.symmetry);
        if (!sym) invalid(r.name.loc, "symmetry", r.symmetry, symmetries);
        std::vector<std::pair<transform, int16_t>> rots = {{transform::identity, 0}};
        for (long long a : r.rotation_angles) {
            auto rot = lookup(rotations, a);
            if (rot) rots.push_back({rot->k, (int16_t)a});
            else     invalid(r.name.loc, "rotation angle", std::to_string(a), rotations);
        }
        auto const& syms = sym ? sym->variants : symmetries[0].variants;

        compiled_rule cr;
        cr.body = r.body;
        // Expand each sub-rule into its variants; dedup by (match, write)
        // equality, scoped per sub-rule so same-LHS sub-rules both survive.
        for (int bi = 0; bi < (int)r.pairs.size(); ++bi) {
            compiled_pair base = compile_base_pair(r.pairs[bi]);
            base.sub_rule_idx = bi;
            std::vector<compiled_pair> variants;
            for (auto [rk, deg] : rots)
                for (auto [sk, flip] : syms) {
                    compiled_pair cand = transform_pair(sk, transform_pair(rk, base));
                    cand.sub_rule_idx = bi;
                    cand.rotation = deg;
                    cand.flip = flip;
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

    // ── reductivity check. A sub-rule *definitely* sustains the fixpoint when
    //    no write that happens on every resolution ({ any } branches don't count)
    //    overwrites one of its own LHS-constrained cells with a value that no
    //    longer matches — the applied anchor then re-matches forever, and
    //    `grow` without a count never terminates. Conservative: warns only
    //    on the guaranteed case. ─────────────────────────────────────────────

    void check_reductive(compiled_rule const& rule, source_loc loc) {
        for (auto const& pair : rule.pairs) {
            std::vector<compiled_pattern const*> writes;
            collect_write_leaves(pair.rhs, writes, /*certain=*/true);

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
                        // its equality with the others.
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
                            bool still = lp.is_number ? ((w.val == req.val) != req.ne)
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

    // ── program operations ───────────────────────────────────────────────────

    // ── the operation table: a closed set resolved by name, exactly
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
        static const std::vector<op_spec> table = {   // keyed by name
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
                                           {"connectivity", arg_kind::int_,  false, false},
                                           {"cost",         arg_kind::expr,  false, false}}},
            {"bsp",     op_kind::bsp,     {{"into",         arg_kind::grid,  false, true},
                                           {"min",          arg_kind::int_,  false, true},
                                           {"max",          arg_kind::int_,  false, false},
                                           {"split",        arg_kind::int_,  false, false}}},
            {"voronoi", op_kind::voronoi, {{"sites",        arg_kind::pred,  false, true},
                                           {"into",         arg_kind::grid,  false, true},
                                           {"within",       arg_kind::pred,  false, false},
                                           {"metric",       arg_kind::enum_, false, false},
                                           {"cost",         arg_kind::expr,  false, false}}},
            {"noise",   op_kind::noise,   {{"into",         arg_kind::grid,  false, true},
                                           {"lo",           arg_kind::int_,  false, true},
                                           {"hi",           arg_kind::int_,  false, true},
                                           {"scale",        arg_kind::int_,  false, true},
                                           {"octaves",      arg_kind::int_,  false, false},
                                           {"persistence",  arg_kind::int_,  false, false},
                                           {"normalize",    arg_kind::enum_, false, false}}},
        };
        return table;
    }

    // "'into=' of 'path'" - how argument diagnostics name an argument.
    static std::string arg_ctx(char const* op, char const* pn) {
        return "'" + std::string(pn) + "=' of '" + op + "'";
    }

    // A bare-identifier argument's name (a `grid` or `enum` value), else null.
    static std::string const* bare_name(op_arg const& a) {
        return a.value->kind == expr_kind::ident ? &a.value->ident : nullptr;
    }
    static std::string arg_render(op_arg const& a) {
        if (a.value->kind == expr_kind::int_lit) return std::to_string(a.value->int_val);
        if (auto n = bare_name(a)) return *n;
        return "(expr)";
    }

    // ── per-kind argument compilers ──────────────────────────────────────────

    // A number argument. `int` arguments (resize w/h, upscale n/m, pad n,
    // path connectivity) are statement-scope expressions, like a count;
    // out-of-range values are handled at run time. `cost` is per cell.
    int op_num(op_arg const& a, char const* op, char const* pn, bool statement) {
        return compile_as(*a.value, val_type::num, a.loc,
                          "'" + std::string(pn) + "' of '" + op + "' must be a number",
                          statement ? stmt_scope : scope{});
    }

    int op_grid(op_arg const& a, char const* op, char const* pn) {
        auto name = bare_name(a);
        if (!name) {
            error(a.loc, arg_ctx(op, pn) + " expects a grid name, got '" + arg_render(a) + "'");
            return -1;
        }
        int id = out.layer_id(*name);
        if (id < 0 && out.graph_id(*name) >= 0)
            error(a.loc, arg_ctx(op, pn) + ": '" + *name + "' is a graph layer; it takes a grid");
        else if (id < 0)
            error(a.loc, arg_ctx(op, pn) + ": '" + *name + "' is not a declared grid");
        else
            require_visible(a.loc, "grid", *name, layer_mod[(size_t)id]);
        return id;
    }

    // A grid that receives ids or values only a number grid can hold.
    int op_number_grid(op_arg const& a, char const* op, char const* pn) {
        int id = op_grid(a, op, pn);
        if (id >= 0 && out.layers[(size_t)id].tag_id >= 0) {
            error(a.loc, arg_ctx(op, pn) + ": '" + *bare_name(a) +
                  "' is a tag grid; it takes a number grid");
            return -1;
        }
        return id;
    }

    // A bare tag must belong to exactly ONE tag layer's tagset; ambiguity
    // needs the expression form naming the grid. Any other argument is a
    // per-cell boolean expression.
    int op_pred(op_arg const& a, char const* op, char const* pn) {
        auto name = bare_name(a);
        if (!name)
            return compile_as(*a.value, val_type::boolean, a.loc,
                              arg_ctx(op, pn) + " must be a tag value or a boolean expression");
        auto tagsets = tagsets_declaring(*name, a.loc.mod);
        std::vector<int> layers;
        for (int li = 0; li < (int)out.layers.size(); ++li)
            if (sees(a.loc.mod, layer_mod[(size_t)li]) &&
                std::count(tagsets.begin(), tagsets.end(), out.layers[li].tag_id))
                layers.push_back(li);
        if (layers.size() == 1) {
            int64_t m = out.mask_of(out.layers[(size_t)layers[0]].tag_id, *name);
            return add_expr({ce_kind::has, m, layers[0], -1, -1, -1});
        }
        if (layers.empty())
            error(a.loc, "unknown tag value '" + *name + "' in " + arg_ctx(op, pn) +
                  " (no layer this module sees has a tagset declaring it)");
        else
            error(a.loc, "predicate '" + *name + "' in " + arg_ctx(op, pn) +
                  " is ambiguous (several layers could hold it); use an expression "
                  "naming the grid, e.g. " + pn + "=(grid == " + *name + ")");
        return -1;
    }

    // A `value` argument: an expression typed by the target grid, so a bare
    // name resolves against its tagset first.
    int op_value(op_arg const& a, char const* op, char const* pn, int into_grid) {
        int tid = out.layers[into_grid].tag_id;
        return compile_as(*a.value, tid < 0 ? val_type::num : val_type::mask, a.loc,
                          arg_ctx(op, pn) + " must be " +
                          (tid < 0 ? "a number (the target grid is a number grid)"
                                   : "a tag value (the target grid is a tag grid)"),
                          scope{}, tid);
    }

    // ── argument binding + per-op compilation ────────────────────────────────

    bool compile_op_call(program_stmt const& s, compiled_op& op) {
        op_spec const* spec = lookup(op_table(), s.op_name);
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
                auto p = lookup(spec->params, a.name);
                int idx = p ? (int)(p - spec->params.data()) : -1;
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
        // `int` arguments, positional or named, in written order: they evaluate
        // (and draw any `random`) in that order
        for (auto const& a : s.op_args)
            for (int i = 0; i < (int)spec->params.size(); ++i)
                if (bound[i] == &a && spec->params[i].kind == arg_kind::int_)
                    op.int_args.push_back({i, op_num(a, spec->name, spec->params[i].name, true)});
        switch (spec->kind) {
        case op_kind::resize:
        case op_kind::upscale:
        case op_kind::pad:
        case op_kind::trim:
            break;
        case op_kind::mirror: {
            auto axis = lookup(mirror_axes, arg_render(*bound[0]));   // its one parameter
            if (!axis) {
                invalid(bound[0]->loc, "mirror axis", arg_render(*bound[0]), mirror_axes);
                return false;
            }
            op.w = axis->w;
            break;
        }
        case op_kind::path: {
            auto index = [&](std::string_view n) {
                return (int)(lookup(spec->params, n) - spec->params.data());
            };
            auto arg = [&](char const* n) { return bound[(size_t)index(n)]; };
            op.into_grid = op_grid(*arg("into"), spec->name, "into");
            op.from      = op_pred(*arg("from"), spec->name, "from");
            op.to        = op_pred(*arg("to"), spec->name, "to");
            if (op.into_grid >= 0)
                op.write = op_value(*arg("write"), spec->name, "write", op.into_grid);
            if (arg("over"))     op.over_grid = op_grid(*arg("over"), spec->name, "over");
            if (arg("passable")) op.passable  = op_pred(*arg("passable"), spec->name, "passable");
            if (arg("cost"))     op.cost      = op_num(*arg("cost"), spec->name, "cost", false);
            assert(index("connectivity") == compiled_op::path_connectivity);
            if (!arg("connectivity"))   // the default, as an `int` argument
                op.int_args.push_back({compiled_op::path_connectivity, num_lit(4)});
            break;
        }
        case op_kind::bsp: {
            auto index = [&](std::string_view n) {
                return (int)(lookup(spec->params, n) - spec->params.data());
            };
            auto arg = [&](char const* n) { return bound[(size_t)index(n)]; };
            op.into_grid = op_number_grid(*arg("into"), spec->name, "into");
            assert(index("min") == compiled_op::bsp_min && index("max") == compiled_op::bsp_max &&
                   index("split") == compiled_op::bsp_split);
            op.has_max = arg("max") != nullptr;
            if (!arg("split"))
                op.int_args.push_back({compiled_op::bsp_split, num_lit(50)});
            break;
        }
        case op_kind::voronoi: {
            auto index = [&](std::string_view n) {
                return (int)(lookup(spec->params, n) - spec->params.data());
            };
            auto arg = [&](char const* n) { return bound[(size_t)index(n)]; };
            op.into_grid = op_number_grid(*arg("into"), spec->name, "into");
            op.sites     = op_pred(*arg("sites"), spec->name, "sites");
            if (arg("within")) op.within = op_pred(*arg("within"), spec->name, "within");
            op.w = 0;   // euclidean
            if (auto a = arg("metric")) {
                auto m = lookup(voronoi_metrics, arg_render(*a));
                if (!m) {
                    invalid(a->loc, "voronoi metric", arg_render(*a), voronoi_metrics);
                    return false;
                }
                op.w = m->w;
            }
            if (auto a = arg("cost")) {
                if (op.w == 0) {
                    error(a->loc, arg_ctx(spec->name, "cost") + " measures route length; it "
                          "takes metric=manhattan or metric=chebyshev, not euclidean");
                    return false;
                }
                op.cost = op_num(*a, spec->name, "cost", false);
            }
            break;
        }
        case op_kind::noise: {
            auto index = [&](std::string_view n) {
                return (int)(lookup(spec->params, n) - spec->params.data());
            };
            auto arg = [&](char const* n) { return bound[(size_t)index(n)]; };
            op.into_grid = op_number_grid(*arg("into"), spec->name, "into");
            assert(index("lo") == compiled_op::noise_lo && index("hi") == compiled_op::noise_hi &&
                   index("scale") == compiled_op::noise_scale &&
                   index("octaves") == compiled_op::noise_octaves &&
                   index("persistence") == compiled_op::noise_persistence);
            if (!arg("octaves"))
                op.int_args.push_back({compiled_op::noise_octaves, num_lit(1)});
            if (!arg("persistence"))
                op.int_args.push_back({compiled_op::noise_persistence, num_lit(50)});
            op.w = 1;   // rank
            if (auto a = arg("normalize")) {
                auto mode = lookup(noise_normalizations, arg_render(*a));
                if (!mode) {
                    invalid(a->loc, "noise normalize", arg_render(*a), noise_normalizations);
                    return false;
                }
                op.w = mode->w;
            }
            break;
        }
        }
        return true;
    }

    // ── statements: the program body and every sequence body ─────────────────

    // A count: a statement-scope expression like a guard - params only, and a
    // number. Out-of-range values are clamped, with a warning, at run time.
    void compile_count(program_stmt const& s, compiled_stmt& cs, std::string const& ctx) {
        cs.count = compile_as(*s.count, val_type::num, s.count->loc,
                              "a count must be a number" + ctx, stmt_scope, -1,
                              /*type_empty=*/false);
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

    // Rules and sequences share one namespace. Every sequence gets
    // a slot, duplicates included, so out.sequences[i] is ast.sequences[i];
    // name lookups find the first declaration.
    void register_sequences() {
        for (auto const& sq : ast.sequences) {
            int r = index_of(ast.rules, sq.name.text, &rule_decl::name);
            int k = index_of(out.sequences, sq.name.text, &compiled_sequence::name);
            if (r >= 0)
                error(sq.name.loc, "duplicate name '" + sq.name.text + "': already declared as a "
                      "rule on line " + std::to_string(ast.rules[(size_t)r].loc.line) +
                      " - rules and sequences share one namespace");
            else if (k >= 0)
                error(sq.name.loc, "duplicate sequence '" + sq.name.text + "' (first declared on line " +
                      std::to_string(ast.sequences[(size_t)k].loc.line) + ")");
            out.sequences.push_back({sq.name.text, {}});
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
                    // an anonymous rule declared in the sequence's module
                    auto const& r = *s.inline_rule;
                    compiled_rule cr = compile_rule(r);
                    cr.name = "inline@" + std::to_string(r.loc.line) + ":" +
                              std::to_string(r.loc.col);
                    cr.is_inline = true;
                    out.rules.push_back(std::move(cr));
                    cs.rule_id = (int)out.rules.size() - 1;
                }
                if (cs.rule_id < 0)
                    cs.rule_id = index_of(out.rules, s.rule_name.text, &compiled_rule::name);
                if (cs.rule_id < 0)
                    cs.seq_id = index_of(out.sequences, s.rule_name.text, &compiled_sequence::name);
                if (cs.rule_id < 0 && cs.seq_id < 0) {
                    error(s.rule_name.loc, "undeclared rule or sequence '" + s.rule_name.text + "'" +
                          did_you_mean(s.rule_name.text));
                    continue;
                }
                if (s.inline_rule)
                    ;   // declared right here
                else if (cs.rule_id >= 0)
                    require_visible(s.rule_name.loc, "rule", s.rule_name.text,
                                    ast.rules[(size_t)cs.rule_id].loc.mod);
                else
                    require_visible(s.rule_name.loc, "sequence", s.rule_name.text,
                                    ast.sequences[(size_t)cs.seq_id].loc.mod);
                if (s.count) compile_count(s, cs, ctx);
                if (cs.seq_id >= 0) {
                    // Rule-only modes on a sequence.
                    if (s.mode != apply_mode::once && s.mode != apply_mode::settle)
                        error(s.loc, std::string("'") + mode_name(s.mode) + "' is not valid on sequence '" +
                              s.rule_name.text + "'; a sequence takes 'once' or 'settle'" +
                              (s.mode == apply_mode::scatter
                                   ? ", and 'scatter' belongs on the statements inside it" : ""));
                } else if (s.mode == apply_mode::grow && !s.count) {
                    // Reductivity warning: a `grow` fixpoint over a rule whose
                    // write never invalidates its own match cannot terminate.
                    check_reductive(out.rules[cs.rule_id], s.loc);
                }
            }
            // `when (expr)` guard: boolean, params only — no grids, no
            // position/dimensions (there is no candidate position or committed
            // size at statement scope).
            if (s.guard)
                cs.guard = compile_as(*s.guard, val_type::boolean, s.loc,
                                      "'when' guard must be a boolean expression" + ctx,
                                      stmt_scope);
            outv.push_back(cs);
        }
    }

    // A sequence may not apply itself, directly or through others.
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

    void run(bool best_effort) {
        for (int id : mods.order) out.modules.push_back(mods.names[(size_t)id]);
        out.module_labels = mods.names;
        build_tables();
        if (!best_effort && diags.has_errors()) return;
        compile_params();
        if (!best_effort && diags.has_errors()) return;
        compile_rules();
        if (!best_effort && diags.has_errors()) return;
        register_sequences();
        for (size_t i = 0; i < ast.sequences.size(); ++i)
            compile_stmts(ast.sequences[i].stmts, out.sequences[i].stmts, ast.sequences[i].name.text);
        check_sequence_cycles();
    }
};

}  // namespace

bool analyze(module_closure const& mods, compiled& out, diagnostics& diags,
             bool best_effort) {
    analyzer a{mods.merged, out, diags, mods, {}, {}, {}, {}};
    a.run(best_effort);
    return !diags.has_errors();
}

}  // namespace ls
