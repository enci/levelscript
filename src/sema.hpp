#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace ls {

// ── cell encoding ─────────────────────────────────────────────────────────────
//
// Tag grid cells store a 32-bit mask: bit 0 = empty, value i on bit i+1;
// matching is any-overlap `(stored & mask) != 0` (spec section 4.1). Number grid
// cells store the integer directly with a dedicated empty sentinel.
constexpr int64_t tag_empty = 0x1;
constexpr int64_t num_empty = INT64_MIN;

inline int64_t tag_bit(int value_id) { return int64_t{1} << (value_id + 1); }

// The where pseudo-layer has no real grid id (section 5.9).
constexpr int where_grid = -2;

// ── compiled expressions (section 5.8) ───────────────────────────────────────────────
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
    // built-ins (section 5.10): if is eager — both branches always evaluate
    if_, min_, max_, abs_, clamp_,
    random_,       // the one impure built-in: one PRNG draw per evaluation
    // emptiness tests (read the cell raw, bypassing number→0 coercion)
    is_empty, is_not_empty,   // ref = grid id; val = 1 when a number grid
};

struct compiled_expr {
    ce_kind   kind{ce_kind::int_lit};
    long long val{0};
    int       ref{-1};            // grid_read / param_read / is_empty
    int       a{-1}, b{-1}, c{-1};   // child indices into the arena
};

// ── compiled program ──────────────────────────────────────────────────────────
//
// `compiled` is self-contained: sema copies every name and table the runtime
// and the public API need, and the AST dies after analysis. It is immutable
// after analyze() and shared (const) by generators, runs, and levels.

struct compiled_cell {
    enum class kind { wildcard, value, expr } what{kind::value};
    int64_t val{0};    // kind::value — mask (tag grid) or number
    int     expr{-1};  // kind::expr — arena index (matches/writes computed)
};

struct compiled_pattern {
    int  grid_id{-1};        // layer index, or where_grid (section 5.9)
    bool is_number{false};
    bool is_where{false};
    int  rows{0}, cols{0};
    std::vector<compiled_cell> cells;   // flat, row-major

    compiled_cell const& at(int r, int c) const { return cells[r * cols + c]; }
};

// Compiled recursive write term (spec section 5.2). The runtime resolves a tree to
// its applied leaves per application ({ all } = every item, { any } = one
// weighted draw per node); footprints/conflicts use every leaf, conservative
// across { any } branches.
struct compiled_write_term {
    enum class kind { leaf, all, any } what{kind::leaf};
    int                              weight{1};
    compiled_pattern                 pattern;   // leaf
    std::vector<compiled_write_term> items;     // all / any
};

// Collect every leaf pattern anywhere in a write tree (the conservative write
// footprint used by the conflict mask).
inline void collect_write_leaves(compiled_write_term const& t,
                                 std::vector<compiled_pattern const*>& out) {
    if (t.what == compiled_write_term::kind::leaf) { out.push_back(&t.pattern); return; }
    for (auto const& it : t.items) collect_write_leaves(it, out);
}

// One match-write pair, post-expansion: LHS patterns conjoined at one anchor,
// a write tree, and the declaring sub-rule's index (the `ordered` priority
// key, step 3).
struct compiled_pair {
    std::vector<compiled_pattern> lhs;
    compiled_write_term           rhs;
    int                           sub_rule_idx{0};
    // Which transform of the declared sub-rule this variant is (section 5.6), as the
    // attributes produced it: rotation= then symmetry=. Identity is 0 / none.
    // Display and tooling only; the runtime never reads them.
    enum class mirror : uint8_t { none, h, v, both };
    int16_t                       rotation{0};   // 0, 90, 180, 270
    mirror                        flip{mirror::none};
};

struct compiled_rule {
    std::string                name;
    body_combinator            body{body_combinator::none};   // `ordered` matters at runtime
    std::vector<compiled_pair> pairs;   // all symmetry/rotation variants, deduped
};

// ── operations (spec section 6.0) ────────────────────────────────────────────────────

enum class op_kind { resize, upscale, trim, mirror, pad, path };

// A compiled per-cell predicate (section 6.0 `pred`): a bare tag reads its unique
// layer with mask-overlap semantics; an expression is a boolean per cell.
struct compiled_pred {
    bool    given{false};
    bool    is_expr{false};
    int     grid_id{-1};   // tag form
    int64_t mask{0};
    int     expr{-1};      // expr form (arena index)
};

// A `value` argument, typed by the target grid.
struct compiled_value {
    bool    is_expr{false};
    int64_t const_val{0};
    int     expr{-1};
};

struct compiled_op {
    op_kind kind{op_kind::trim};
    // resize/upscale dims; pad margin (w); mirror axis (w: 1 = horizontal)
    int w{0}, h{0};
    // path (section 6.6)
    compiled_pred  from, to;
    compiled_pred  passable;        // !given → default: non-empty in any layer
    int            into_grid{-1};
    compiled_value write;
    int            over_grid{-1};   // the graph seam: validated, no effect yet
    int            connectivity{4};
    int            cost{-1};        // expr arena index; -1 → constant 1 (BFS)
};

// ── program ───────────────────────────────────────────────────────────────────

struct compiled_stmt {
    enum class kind { op_call, apply } what{kind::apply};
    source_loc  loc;
    compiled_op op;                       // op_call
    apply_mode  mode{apply_mode::everywhere};   // apply (sections 6, 6.7)
    int         count{-1};        // -1 = none: once = 1, everywhere = all, grow/settle = fixpoint
    bool        percent{false};   // scatter(P%): count is P
    int         rule_id{-1};   // apply over a rule
    int         seq_id{-1};    // apply over a sequence (section 6.10): once or settle
    int         guard{-1};   // `when` expr arena index; -1 = unguarded
};

// A named statement list (section 6.10), applied by name like a rule; one run of
// the body is an iteration.
struct compiled_sequence {
    std::string                name;
    std::vector<compiled_stmt> stmts;
};

struct compiled_layer {
    std::string name;
    int         tag_id{-1};   // index into tag_names/tag_values; -1 = number grid
};

struct compiled {
    std::vector<std::string>              tag_names;
    std::vector<std::vector<std::string>> tag_values;   // [tag_id][value_id]
    // named unions per tagset: (name, resolved mask) — section 3
    std::vector<std::vector<std::pair<std::string, int64_t>>> tag_unions;
    std::vector<compiled_layer>           layers;
    std::vector<compiled_rule>            rules;
    std::vector<compiled_sequence>        sequences;   // canonical order = API ids
    std::vector<std::string>              modules;     // canonical names, canonical order (section 2.6)
    std::vector<compiled_expr>            exprs;   // the expression arena

    // params (section 4.2): startup_exprs run once, in declaration order, when the
    // inputs bind — a default only when its param was not supplied.
    std::vector<std::string> param_names;
    struct startup_expr { int param; int expr; bool is_default; };
    std::vector<startup_expr> startup_exprs;

    int param_id(std::string_view name) const {
        for (int i = 0; i < (int)param_names.size(); ++i)
            if (param_names[i] == name) return i;
        return -1;
    }
    // A name's mask within a tagset: a value's bit or a union's mask; 0 = unknown.
    int64_t mask_of(int tag, std::string_view name) const {
        int vid = value_id(tag, name);
        if (vid >= 0) return tag_bit(vid);
        if (tag >= 0 && tag < (int)tag_unions.size())
            for (auto const& [uname, mask] : tag_unions[tag])
                if (uname == name) return mask;
        return 0;
    }

    int layer_id(std::string_view name) const {
        for (int i = 0; i < (int)layers.size(); ++i)
            if (layers[i].name == name) return i;
        return -1;
    }
    int tag_id(std::string_view name) const {
        for (int i = 0; i < (int)tag_names.size(); ++i)
            if (tag_names[i] == name) return i;
        return -1;
    }
    int value_id(int tag, std::string_view name) const {
        if (tag < 0 || tag >= (int)tag_values.size()) return -1;
        auto const& vals = tag_values[tag];
        for (int i = 0; i < (int)vals.size(); ++i)
            if (vals[i] == name) return i;
        return -1;
    }
};

struct module_closure;

// Analyze a loaded module closure (section 2.6) into a self-contained compiled
// artifact. Returns false (with diagnostics) on any error. `best_effort`
// keeps going through later phases despite errors — editor tooling wants
// every table the closure still supports (plus the extra diagnostics), not a
// first-phase bail.
bool analyze(module_closure const& mods, compiled& out, diagnostics& diags,
             bool best_effort = false);

}  // namespace ls
