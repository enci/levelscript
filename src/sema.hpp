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
// matching is any-overlap `(stored & mask) != 0` (spec §4.1). Number grid
// cells store the integer directly with a dedicated empty sentinel.
constexpr int64_t tag_empty = 0x1;
constexpr int64_t num_empty = INT64_MIN;

inline int64_t tag_bit(int value_id) { return int64_t{1} << (value_id + 1); }

// ── compiled program ──────────────────────────────────────────────────────────
//
// `compiled` is self-contained: sema copies every name and table the runtime
// and the public API need, and the AST dies after analysis. It is immutable
// after analyze() and shared (const) by generators, runs, and levels.

struct compiled_cell {
    enum class kind { wildcard, value } what{kind::value};
    int64_t val{0};   // mask (tag grid) or number; empty sentinel for '.'
};

struct compiled_pattern {
    int  grid_id{-1};
    bool is_number{false};
    int  rows{0}, cols{0};
    std::vector<compiled_cell> cells;   // flat, row-major

    compiled_cell const& at(int r, int c) const { return cells[r * cols + c]; }
};

// Compiled recursive write term (spec §5.2). The runtime resolves a tree to
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
};

struct compiled_rule {
    std::string                name;
    body_combinator            body{body_combinator::none};   // `ordered` matters at runtime
    std::vector<compiled_pair> pairs;   // all symmetry/rotation variants, deduped
};

// ── operations (spec §6.0) ────────────────────────────────────────────────────

enum class op_kind { resize };

struct compiled_op {
    op_kind kind{op_kind::resize};
    int w{0}, h{0};
};

// ── program ───────────────────────────────────────────────────────────────────

struct compiled_stmt {
    enum class kind { op_call, apply } what{kind::apply};
    compiled_op op;                       // op_call
    strategy    strat{strategy::all};     // apply — count × policy (§6.7)
    exec_policy pol{exec_policy::snapshot};
    bool        is_percent{false};
    int         max_count{0};
    int         percent{0};
    int         rule_id{-1};
};

struct compiled_layer {
    std::string name;
    int         tag_id{-1};   // index into tag_names/tag_values; -1 = number grid
};

struct compiled {
    std::vector<std::string>              tag_names;
    std::vector<std::vector<std::string>> tag_values;   // [tag_id][value_id]
    std::vector<compiled_layer>           layers;
    std::vector<compiled_rule>            rules;
    std::vector<compiled_stmt>            stmts;

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

// Analyze a parsed file into a self-contained compiled program.
// Returns false (with diagnostics) on any error.
bool analyze(ast_file const& ast, compiled& out, diagnostics& diags,
             std::string_view file);

}  // namespace ls
