#pragma once
#include "sema.hpp"
#include "sequence.hpp"
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

namespace ls::internal {

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

// One level of the statement stack (spec section 6.10, Appendix A stmt_stack):
// position in the enclosing list, and the enclosing sequence's iteration.
struct frame {
    int index{0};
    int iteration{0};
};

struct grid_state {
    int rows{0}, cols{0};
    bool is_number{false};
    std::vector<int64_t> front, back;   // double-buffered (spec section 7.1)

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
    // `entry`: the sequence a run applies (section 6); an invalid id runs nothing.
    machine(std::shared_ptr<compiled const> prog, uint64_t seed, int entry);

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
    // A statement's count, evaluated on each visit after its guard (section 6):
    // -1 = none; below 0 acts as 0, a percentage above 100 as 100.
    int eval_count(compiled_stmt const& st);
    sequence<step_event> run_leaf(compiled_stmt const& st, int top, int count);
    // A sequence application (section 6.10): iterations of the body until the count
    // runs out or an iteration is stable.
    sequence<step_event> run_sequence(compiled_stmt const& st, int top, int count);

    // The whole grid stack, for sequence stability (section 6.10, section 10.7).
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

    void exec_op(compiled_op const& op);
    void run_path(compiled_op const& op);   // section 6.6
    // Evaluate an arena expression at position (x, y). Total (section 5.8): /0 = 0,
    // wrap at 32 bits, empty number cells read as 0. `random` draws — which is
    // why the whole matching path is non-const.
    long long eval(int expr_idx, int x, int y);
    // Startup params (section 4.2): defaults for unsupplied inputs, then derived,
    // in declaration order — the first PRNG draws of a run.
    void bind_params();
    // ── pinned draw primitives (section 10.6): plain 64-bit integer arithmetic over
    // the mt19937_64 stream, so a seed gives the same draws on every
    // platform and compiler - unlike std::shuffle and
    // std::uniform_int_distribution, whose algorithms the standard leaves
    // to each library.
    //
    // uniform(n): unbiased integer in [0, n), n >= 1. Rejection sampling:
    // discard raw outputs below 2^64 mod n, return x mod n.
    uint64_t uniform(uint64_t n);
    // Fisher-Yates: for i = size-1 down to 1, swap item i with item uniform(i+1).
    template <class T> void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[(size_t)uniform(i)]);
    }

    std::vector<match> collect(compiled_rule const& rule);
    bool match_at(compiled_pair const& pair, int row, int col);
    bool conflicts(compiled_pair const& pair, match const& m,
                   std::unordered_set<uint64_t> const& written) const;
    void apply(compiled_pair const& pair, match const& m,
               std::unordered_set<uint64_t>& written);
    // Resolve a write tree to its applied leaves: { all } every item, { any }
    // one weighted draw per node, pre-order (outer before inner) so the PRNG
    // sequence is pinned per seed (spec section 10.7).
    void resolve_write(compiled_write_term const& t,
                       std::vector<compiled_pattern const*>& out);

    // Seeded shuffle; under `ordered`, a stable sort by sub-rule priority on
    // top (groups in declaration order, shuffled within each — spec section 5.2).
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

    static uint64_t mask_key(int grid_id, int flat) {
        return ((uint64_t)grid_id << 40) | (uint64_t)flat;
    }

    std::shared_ptr<compiled const> prog_;
    int                             entry_{-1};
    std::mt19937_64                 rng_;
    std::vector<grid_state>         grids_;    // indexed by grid id
    // The stack's dimensions - kept apart from the layers so a layer-less
    // program still has a size (width/height, stability, the level).
    int                             rows_{0}, cols_{0};
    std::vector<frame>              frames_;
    std::vector<long long>          params_;   // indexed by param id
    std::vector<char>               param_supplied_;
    bool                            in_batch_{false};
    bool                            observe_{false};
    std::vector<highlight>          highlights_;
};

}  // namespace ls::internal
