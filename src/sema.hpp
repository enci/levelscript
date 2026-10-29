#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace ls {

// ── name lookup ───────────────────────────────────────────────────────────────
//
// Every table the compiler resolves by name is a short sequence searched
// linearly: `index_of` finds a position (-1 = none), `lookup` a row keyed by
// its `name` member (null = none).

template <class Range, class Key, class Proj = std::identity>
int index_of(Range const& range, Key const& key, Proj proj = {}) {
    int i = 0;
    for (auto const& x : range) {
        if (std::invoke(proj, x) == key) return i;
        ++i;
    }
    return -1;
}

template <class Table, class Key>
auto lookup(Table const& table, Key const& key) -> decltype(&*std::begin(table)) {
    for (auto const& row : table)
        if (row.name == key) return &row;
    return nullptr;
}

// ── cell encoding ─────────────────────────────────────────────────────────────
//
// Tag grid cells store a 32-bit mask: bit 0 = empty, value i on bit i+1;
// matching is any-overlap `(stored & mask) != 0`. Number grid
// cells store the integer directly with a dedicated empty sentinel.
constexpr int64_t tag_empty = 0x1;
constexpr int64_t num_empty = INT64_MIN;

inline int64_t tag_bit(int value_id) { return int64_t{1} << (value_id + 1); }

// The where pseudo-layer has no real grid id.
constexpr int where_grid = -2;

// ── compiled expressions ─────────────────────────────────────────────────────
//
// Nodes live in one flat arena (`compiled::exprs`), referenced by index — no
// pointer ownership, trivially copyable, and variant transforms share nodes
// by index (same-position reads make expressions position-independent).

enum class ce_kind {
    int_lit,       // val = integer (also the number-empty sentinel)
    mask_lit,      // val = tag mask constant
    grid_read,     // ref = grid id, read at the current position
    param_read,    // ref = param id
    pos_x, pos_y, width, height,
    neg, not_,
    add, sub, mul, div_,
    lt, le, gt, ge, eq, ne,
    and_, or_, bit_or,
    // built-ins: if is eager — both branches always evaluate
    if_, min_, max_, abs_, clamp_, mod_,
    random_,       // the one impure built-in: one PRNG draw per evaluation
    // emptiness tests (read the cell raw, bypassing number→0 coercion)
    is_empty, is_not_empty,   // ref = grid id; val = 1 when a number grid
    has,           // ref = tag grid id, val = mask: the cell overlaps it (a bare-tag pred)
    var_read,      // ref = the pair's variable slot; a node variable reads its node id
    // graph functions: ref = graph id, a = the node; the targeted ones take
    // a selector (val = selector id) or a node (b = its expr; val = -1)
    degree_, size_,
    distance_,     // unreachable is graph_unreachable
    neighbors_,
    adjacent_,     // neighbors > 0
    reachable_,    // distance < graph_unreachable
    value_,        // the value every cell of the node's region holds in the source
    // quantifiers: val = selector id, tested against the node's own cells
    count_,        // cells of the region the selector matches
    some_,         // count > 0
    every_,        // count == size
};

// What `distance` gives when no target node is reachable: 2^30, larger than
// every real distance (section 5.12).
constexpr long long graph_unreachable = 1ll << 30;

struct compiled_expr {
    ce_kind   kind{ce_kind::int_lit};
    long long val{0};
    int       ref{-1};            // grid_read / param_read / is_empty / var_read
    int       a{-1}, b{-1}, c{-1};   // child indices into the arena
};

// ── compiled program ──────────────────────────────────────────────────────────
//
// `compiled` is self-contained: sema copies every name and table the runtime
// and the public API need, and the AST dies after analysis. It is immutable
// after analyze() and shared (const) by generators, runs, and levels.

struct compiled_cell {
    enum class kind { wildcard, value, expr, variable } what{kind::value};
    int64_t val{0};    // kind::value — mask (tag grid) or number
    int     expr{-1};  // kind::expr — arena index (matches/writes computed)
    int     var{-1};   // kind::variable — the pair's variable slot:
                       // binds on the match side, writes its value on the write side
    bool    ne{false}; // kind::value in a number grid: matches any value but `val`
                       // (`!.` = not the empty sentinel)
};

// One cell the matcher tests, at its offset from the anchor.
struct match_probe {
    int     r{0}, c{0};
    int64_t val{0};    // bare: the value to test against
    int     expr{-1};  // computed: arena index
};

struct compiled_pattern {
    int  grid_id{-1};        // layer index (a graph's source), or where_grid
    int  graph_id{-1};       // a graph pattern: the graph it tests
    bool is_number{false};
    bool is_where{false};
    int  rows{0}, cols{0};
    std::vector<compiled_cell> cells;   // flat, row-major
    // A node extent `g(?n)[c]` (graph pairs' writes only): the slot of the
    // node whose region its one cell covers; -1 = an ordinary pattern.
    int  extent{-1};

    // Match side only, filled per variant: the cells of each matching phase
    // in row-major order, wildcards dropped.
    std::vector<match_probe> bare;       // value cells
    std::vector<match_probe> bare_ne;    // number grids: cells that must differ from val (`!.`)
    std::vector<match_probe> computed;   // expression and `where` cells

    compiled_cell const& at(int r, int c) const { return cells[r * cols + c]; }
};

// Compiled recursive write term. The runtime resolves a tree to
// its applied leaves per application ({ all } = every item, { any } = one
// weighted draw per node); footprints/conflicts use every leaf, conservative
// across { any } branches.
struct compiled_write_term {
    enum class kind { leaf, all, any } what{kind::leaf};
    int                              weight{1};
    compiled_pattern                 pattern;   // leaf
    std::vector<compiled_write_term> items;     // all / any
};

// Collect the leaf patterns of a write tree: every leaf (the conservative
// write footprint used by the conflict mask), or with `certain` only the
// leaves every resolution writes ({ any } branches skipped).
inline void collect_write_leaves(compiled_write_term const& t,
                                 std::vector<compiled_pattern const*>& out,
                                 bool certain = false) {
    if (t.what == compiled_write_term::kind::leaf) { out.push_back(&t.pattern); return; }
    if (certain && t.what == compiled_write_term::kind::any) return;
    for (auto const& it : t.items) collect_write_leaves(it, out, certain);
}

// One match-write pair, post-expansion: LHS patterns conjoined at one anchor,
// a write tree, and the declaring sub-rule's index (the `ordered` priority
// key, step 3).
struct compiled_pair {
    std::vector<compiled_pattern> lhs;
    // Rule guards: boolean expressions evaluated once per candidate, in phase 2
    // after the expression cells of the first `after` patterns. Variant
    // transforms leave them unchanged and share their arena indices.
    struct guard { int expr{-1}; int after{1}; };
    std::vector<guard>            guards;
    compiled_write_term           rhs;
    int                           sub_rule_idx{0};
    std::vector<std::string>      var_names;   // by slot, '?' included
    // Filled per variant: each variable's first binding cell (its site, which
    // also supplies its value) and every other binding cell, which must
    // equal the site. Offsets are from the anchor.
    // A site in a graph pattern (graph >= 0) binds the node covering it;
    // `grid` is then the graph's source.
    struct var_site  { int grid{-1}; int r{0}, c{0}; int graph{-1}; };
    struct var_check { int slot{0}; var_site at; };
    std::vector<var_site>         var_sites;    // by slot
    std::vector<var_check>        var_checks;
    // Which transform of the declared sub-rule this variant is, as the
    // attributes produced it: rotation= then symmetry=. Identity is 0 / none.
    // Display and tooling only; the runtime never reads them.
    enum class mirror : uint8_t { none, h, v, both };
    int16_t                       rotation{0};   // 0, 90, 180, 270
    mirror                        flip{mirror::none};

    // A graph pair (graph >= 0) has no patterns and no anchor: a candidate
    // assigns one distinct node of `graph` to each variable slot, in
    // lexicographic node-id order over the slots (declaration order), such
    // that every test and edge line holds. Its write leaves are node extents.
    int                           graph{-1};
    // `?n[m]`: the node's value overlaps `val` (tag source) or equals it (number).
    struct node_test { int slot{0}; int64_t val{0}; };
    struct node_edge { int a{0}, b{0}; };   // `?a -- ?b`: slots of adjacent nodes
    std::vector<node_test>        node_tests;
    std::vector<node_edge>        node_edges;
};

struct compiled_rule {
    std::string                name;      // an inline rule's is its source position, "inline@L:C"
    bool                       is_inline{false};   // written in a statement
    body_combinator            body{body_combinator::none};   // `ordered` matters at runtime
    std::vector<compiled_pair> pairs;   // all symmetry/rotation variants, deduped
};

// ── operations ───────────────────────────────────────────────────────────────

enum class op_kind { resize, upscale, trim, mirror, pad, path, bsp, voronoi, noise };

struct compiled_op {
    op_kind kind{op_kind::trim};
    // The `int` arguments (resize w/h, upscale n/m, pad n): statement-scope
    // expressions, in written order - the order they evaluate
    // in when the statement is reached. `param` is the parameter's index.
    struct int_arg { int param{0}; int expr{-1}; };
    // `param`s (table positions) the executors read
    static constexpr int path_connectivity = 6;
    static constexpr int bsp_min = 1, bsp_max = 2, bsp_split = 3;
    static constexpr int noise_lo = 1, noise_hi = 2, noise_scale = 3,
                         noise_octaves = 4, noise_persistence = 5;
    std::vector<int_arg> int_args;
    int w{0};   // mirror axis: 1 = horizontal; noise normalize: 1 = rank, 0 = range;
                // voronoi metric: 0 = euclidean, else the step adjacency (4 or 8)
    bool has_max{false};   // bsp: `max` supplied
    // path, voronoi
    // `pred` arguments: boolean expressions per cell (a bare tag compiles to `has`)
    int            from{-1}, to{-1};
    int            passable{-1};    // -1 → default: non-empty in any layer
    int            sites{-1};       // voronoi
    int            within{-1};      // voronoi; -1 → every cell
    int            into_grid{-1};
    int            write{-1};       // expr arena index, typed by into_grid
    int            over_grid{-1};   // the graph seam: validated, no effect yet
    int            cost{-1};        // expr arena index; -1 → constant 1 (BFS)
};

// ── program ───────────────────────────────────────────────────────────────────

struct compiled_stmt {
    enum class kind { op_call, apply } what{kind::apply};
    source_loc  loc;
    compiled_op op;                       // op_call
    apply_mode  mode{apply_mode::everywhere};   // how many applications, and what each sees
    int         count{-1};        // count expr arena index; -1 = none: once = 1,
                                  // everywhere = all, grow/settle = fixpoint
    bool        percent{false};   // scatter(P%): the count is P
    int         rule_id{-1};   // apply over a rule
    int         seq_id{-1};    // apply over a sequence: once or settle
    int         guard{-1};   // `when` expr arena index; -1 = unguarded
};

// A named statement list, applied by name like a rule; one run of
// the body is an iteration.
struct compiled_sequence {
    std::string                name;
    std::vector<compiled_stmt> stmts;
};

struct compiled_layer {
    std::string name;
    int         tag_id{-1};   // index into tag_names/tag_values; -1 = number grid
};

// A graph layer: a read-only view of its source grid, extracted on every
// read. Node cells are the source's non-empty cells overlapping `nodes`
// (every non-empty cell of a number grid); connector cells overlap `edges`.
struct compiled_graph {
    std::string name;
    int         source{-1};   // grid id
    int64_t     nodes{0};     // value bits; tag sources only
    int64_t     edges{0};     // value bits; 0 = no connectors
};

// The argument of `distance` and `neighbors`: a one-cell pattern on a grid.
// It selects a node when one of the node's cells matches.
struct compiled_selector {
    int     grid{-1};
    int64_t val{0};     // a mask (tag grid) or a number
    bool    ne{false};  // number grid: matches any value but `val` (`!.`)
};

struct compiled {
    std::vector<std::string>              tag_names;
    std::vector<std::vector<std::string>> tag_values;   // [tag_id][value_id]
    // named unions per tagset: (name, resolved mask)
    std::vector<std::vector<std::pair<std::string, int64_t>>> tag_unions;
    std::vector<compiled_layer>           layers;
    std::vector<compiled_graph>           graphs;   // not in the layer order
    std::vector<compiled_selector>        selectors;
    std::vector<compiled_rule>            rules;
    std::vector<compiled_sequence>        sequences;   // canonical order = API ids
    std::vector<std::string>              modules;     // canonical names, canonical order
    std::vector<std::string>              module_labels;   // by module id: names in diagnostics
    std::vector<compiled_expr>            exprs;   // the expression arena

    // params: startup_exprs run once, in declaration order, when the
    // inputs bind — a default only when its param was not supplied.
    std::vector<std::string> param_names;
    struct startup_expr { int param; int expr; bool is_default; };
    std::vector<startup_expr> startup_exprs;

    int param_id(std::string_view name) const { return index_of(param_names, name); }
    // A name's mask within a tagset: a value's bit or a union's mask; 0 = unknown.
    int64_t mask_of(int tag, std::string_view name) const {
        int vid = value_id(tag, name);
        if (vid >= 0) return tag_bit(vid);
        int u = union_id(tag, name);
        return u >= 0 ? tag_unions[(size_t)tag][(size_t)u].second : 0;
    }

    int layer_id(std::string_view name) const { return index_of(layers, name, &compiled_layer::name); }
    int graph_id(std::string_view name) const { return index_of(graphs, name, &compiled_graph::name); }
    int tag_id(std::string_view name) const   { return index_of(tag_names, name); }
    int value_id(int tag, std::string_view name) const {
        if (tag < 0 || tag >= (int)tag_values.size()) return -1;
        return index_of(tag_values[(size_t)tag], name);
    }
    int union_id(int tag, std::string_view name) const {
        if (tag < 0 || tag >= (int)tag_unions.size()) return -1;
        return index_of(tag_unions[(size_t)tag], name, &std::pair<std::string, int64_t>::first);
    }
};

struct module_closure;

// Analyze a loaded module closure into a self-contained compiled
// artifact. Returns false (with diagnostics) on any error. `best_effort`
// keeps going through later phases despite errors — editor tooling wants
// every table the closure still supports (plus the extra diagnostics), not a
// first-phase bail.
bool analyze(module_closure const& mods, compiled& out, diagnostics& diags,
             bool best_effort = false);

}  // namespace ls
