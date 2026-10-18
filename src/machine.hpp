#pragma once
#include "graph.hpp"
#include "sema.hpp"
#include "sequence.hpp"
#include <functional>
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

namespace ls::internal {

// uniform(n): an unbiased integer in [0, n), n >= 1, from a stream of raw
// 64-bit draws `next`. Rejection sampling: discard raw outputs below
// 2^64 mod n, return x mod n. Plain 64-bit arithmetic, so a seed gives the
// same draws on every platform and compiler.
template <class Next> uint64_t uniform_draw(Next&& next, uint64_t n) {
    uint64_t threshold = (0 - n) % n;   // 2^64 mod n, in 64-bit arithmetic
    for (;;) {
        uint64_t x = next();
        if (x >= threshold) return x % n;
    }
}

// hash(d, i): a 64-bit key from a raw draw `d` and an index `i`, consuming no
// draw - splitmix64's finalizer over d ^ i (section 10.6). A bijection in i
// for a fixed d, so distinct indices never share a key.
inline uint64_t hash(uint64_t d, uint64_t i) {
    uint64_t z = (d ^ i) + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// floor(a * b / c) with the product computed exactly in 128 bits, for a
// quotient that fits in 64. Portable (MSVC has no __int128).
inline uint64_t mul_div(uint64_t a, uint64_t b, uint64_t c) {
    // 128-bit product as hi:lo from 32-bit halves
    uint64_t al = a & 0xffffffffu, ah = a >> 32, bl = b & 0xffffffffu, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    uint64_t lo = (mid << 32) | (ll & 0xffffffffu);
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    if (hi == 0) return lo / c;
    // restoring long division of hi:lo by c, one bit at a time
    uint64_t q = 0, r = 0;
    for (int bit = 127; bit >= 0; --bit) {
        bool top = (r >> 63) != 0;
        r = (r << 1) | ((bit >= 64 ? hi >> (bit - 64) : lo >> bit) & 1);
        q <<= 1;
        if (top || r >= c) { r -= c; q |= 1; }
    }
    return q;
}

// ── generated output (owned values; what level/grid hand to the game) ────────

struct level_data {
    std::shared_ptr<compiled const> info;   // for name lookups
    int width{0}, height{0};
    struct layer {
        int  tag_id{-1};             // -1 = number grid
        std::vector<int64_t> cells;  // raw, flat row-major
    };
    std::vector<layer> layers;       // declaration order
};

// ── the execution core ────────────────────────────────────────────────────────

// One yield per rule application, one per completed statement. Granularity is
// a filter on the puller, never a second code path.
struct step_event {
    // begin: a statement (leaf or sequence application) is about to run -
    // yielded before it, so a debugger can stop *before* a statement.
    // application: one rule application done. statement: a leaf finished.
    enum class kind { begin, application, statement } what{kind::statement};
    int stmt{-1};   // statement of the entry's body (a nested sequence's applying one)
};

// One level of the statement stack:
// position in the enclosing list, and the enclosing sequence's iteration.
struct frame {
    int index{0};
    int iteration{0};
    int iterations{1};   // the enclosing application's evaluated count; -1 = until stable
};

struct grid_state {
    int rows{0}, cols{0};
    bool is_number{false};
    std::vector<int64_t> front, back;   // double-buffered
    // Bumped by every write, so caches derived from the front buffer
    // (graph extractions) know when they are stale.
    uint64_t version{0};

    int64_t empty_raw() const { return is_number ? num_empty : tag_empty; }
    int64_t get(int r, int c) const { return front[r * cols + c]; }
};

// One highlighted cell, for the observe channel (debug overlays, editors).
struct highlight {
    int  grid, row, col;
    bool is_write;
};

// The interpreter for one run: owns the grids and the PRNG, executes the
// program as a coroutine. There is exactly one of these per generate()/begin()
// and exactly one implementation of the execution model.
class machine {
public:
    // `entry`: the sequence a run applies; an invalid id runs nothing.
    // `warn` receives each runtime warning, formatted; it may be empty.
    machine(std::shared_ptr<compiled const> prog, uint64_t seed, int entry,
            std::function<void(std::string const&)> warn = {});

    // Supply an input param before run(); unknown names are ignored.
    void set_param(std::string const& name, long long value);

    sequence<step_event> run();

    // Copy the current visible state out as a self-contained level (mid-batch,
    // the accumulating back buffer — committed statements plus this batch's
    // applications so far).
    std::shared_ptr<level_data const> snapshot() const;

    // Observe channel: when enabled, each application records its matched and
    // written cells. Off by default — the release path skips the recording.
    void set_observe(bool on) { observe_ = on; }
    std::vector<highlight> const& highlights() const { return highlights_; }
    // Where the last event happened: outermost first; empty before the first.
    std::vector<frame> const& frames() const { return frames_; }

private:
    struct match { int pair; int row, col; };

    // One rule application or operation - a leaf statement. Yields its
    // applications; the caller yields the statement boundary.
    // A statement's count, evaluated on each visit after its guard:
    // -1 = none; below 0 acts as 0, a percentage above 100 as 100.
    int eval_count(compiled_stmt const& st);
    sequence<step_event> run_leaf(compiled_stmt const& st, int top, int count);
    // A sequence application: iterations of the body until the count
    // runs out or an iteration is stable.
    sequence<step_event> run_sequence(compiled_stmt const& st, int top, int count);

    // The whole grid stack, for sequence stability.
    struct stack_state {
        int rows{0}, cols{0};
        std::vector<std::vector<int64_t>> cells;
    };
    stack_state capture() const;
    // The begin event of the statement the frames now point at.
    step_event begin_event(int top) {
        if (observe_) highlights_.clear();   // nothing matched yet
        return step_event{step_event::kind::begin, top};
    }
    bool unchanged_since(stack_state const& s) const;

    // Report a runtime warning at the statement being run.
    void warn(std::string msg) const;

    void exec_op(compiled_op const& op);
    void run_path(compiled_op const& op, int ndirs);   // ndirs = 4 or 8
    void run_bsp(compiled_op const& op, long long min, long long max, long long split);
    void run_voronoi(compiled_op const& op, int ndirs);
    void run_noise(compiled_op const& op, long long lo, long long hi, long long scale,
                   long long octaves, long long persistence);
    // Write a full W x H field into number grid `id`.
    void write_field(int id, std::vector<int64_t> const& cells);
    // Evaluate an arena expression at position (x, y). Total: /0 = 0,
    // wrap at 32 bits, empty number cells read as 0. `random` draws — which is
    // why the whole matching path is non-const.
    long long eval(int expr_idx, int x, int y);
    // Startup params: defaults for unsupplied inputs, then derived,
    // in declaration order — the first PRNG draws of a run.
    void bind_params();
    // ── pinned draw primitives: plain 64-bit integer arithmetic over
    // the mt19937_64 stream, so a seed gives the same draws on every
    // platform and compiler - unlike std::shuffle and
    // std::uniform_int_distribution, whose algorithms the standard leaves
    // to each library.
    uint64_t uniform(uint64_t n) { return uniform_draw(rng_, n); }
    // Fisher-Yates: for i = size-1 down to 1, swap item i with item uniform(i+1).
    template <class T> void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[(size_t)uniform(i)]);
    }

    std::vector<match> collect(compiled_rule const& rule);
    bool match_at(compiled_pair const& pair, int row, int col);
    // A pattern variable's value: its site in the snapshot, from the anchor.
    // The front buffer is frozen between collection and application, so it
    // is the value the candidate bound.
    int64_t var_value(compiled_pair const& pair, int slot, int row, int col) const {
        auto const& s = pair.var_sites[(size_t)slot];
        return grids_[(size_t)s.grid].get(row + s.r, col + s.c);
    }
    bool conflicts(compiled_pair const& pair, match const& m,
                   std::unordered_set<uint64_t> const& written) const;
    void apply(compiled_pair const& pair, match const& m,
               std::unordered_set<uint64_t>& written);
    // Resolve a write tree to its applied leaves: { all } every item, { any }
    // one weighted draw per node, pre-order (outer before inner) so the PRNG
    // sequence is pinned per seed.
    void resolve_write(compiled_write_term const& t,
                       std::vector<compiled_pattern const*>& out);

    // Seeded shuffle; under `ordered`, a stable sort by sub-rule priority on
    // top (groups in declaration order, shuffled within each).
    void order_candidates(compiled_rule const& rule, std::vector<match>& ms);
    // One uniform pick; under `ordered`, uniform within the highest-priority
    // group present (preemptive priority under re-collection).
    match pick_candidate(compiled_rule const& rule, std::vector<match> const& ms);
    // The non-conflicting prefix a full snapshot pass would apply, cut to P%
    // — the `scatter(P%)` denominator is the applied set, not raw candidates.
    std::vector<match> applicable_prefix(compiled_rule const& rule,
                                         std::vector<match> const& ordered_ms,
                                         int pct) const;
    void add_write_footprint(compiled_pair const& pair, match const& m,
                             std::unordered_set<uint64_t>& out) const;
    bool grids_differ() const;   // back vs front (settle change detection)
    void record_highlights(compiled_pair const& pair, match const& m);

    // Graph `id` as extracted from its source's front buffer, re-extracted
    // only after the source has been written.
    graph_data const& graph(int id);
    // What selector `sel` of graph `id` gives each node: the distance to the
    // nearest selected node (-1 = unreachable), or the number of selected
    // neighbours.
    std::vector<int> const& selection(int id, int sel, bool distance);
    long long eval_graph(compiled_expr const& e, int x, int y);

    static uint64_t mask_key(int grid_id, int flat) {
        return ((uint64_t)grid_id << 40) | (uint64_t)flat;
    }

    std::shared_ptr<compiled const> prog_;
    int                             entry_{-1};
    std::function<void(std::string const&)> warn_;
    source_loc                      stmt_loc_;   // the statement being run, for warnings
    std::mt19937_64                 rng_;
    std::vector<grid_state>         grids_;    // indexed by grid id
    struct graph_cache {
        uint64_t   version{~0ull};   // the source's, when extracted
        graph_data data;
    };
    std::vector<graph_cache>        graphs_;   // indexed by graph id
    struct selection_cache {
        uint64_t         source_version{~0ull}, grid_version{~0ull};
        std::vector<int> per_node;
    };
    std::vector<selection_cache>    selections_;   // indexed by selector id
    // The stack's dimensions - kept apart from the layers so a layer-less
    // program still has a size (width/height, stability, the level).
    int                             rows_{0}, cols_{0};
    std::vector<frame>              frames_;
    std::vector<long long>          params_;   // indexed by param id
    std::vector<char>               param_supplied_;
    bool                            in_batch_{false};
    // The pair and anchor whose cells are being evaluated - what `var_read`
    // resolves against.
    compiled_pair const*            cur_pair_{nullptr};
    int                             anchor_row_{0}, anchor_col_{0};
    bool                            observe_{false};
    std::vector<highlight>          highlights_;
};

}  // namespace ls::internal
