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
    enum class kind { application, statement } what{kind::statement};
    int stmt{-1};
};

struct grid_state {
    int rows{0}, cols{0};
    bool is_number{false};
    std::vector<int64_t> front, back;   // double-buffered (spec §7.1)

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
    machine(std::shared_ptr<compiled const> prog, uint64_t seed);

    sequence<step_event> run();

    // Copy the current visible state out as a self-contained level (mid-batch,
    // the accumulating back buffer — committed statements plus this batch's
    // applications so far).
    std::shared_ptr<level_data const> snapshot() const;

    // Observe channel: when enabled, each application records its matched and
    // written cells. Off by default — the release path skips the recording.
    void set_observe(bool on) { observe_ = on; }
    std::vector<highlight> const& highlights() const { return highlights_; }

private:
    struct match { int pair; int row, col; };

    void exec_op(compiled_op const& op);
    std::vector<match> collect(compiled_rule const& rule) const;
    bool match_at(compiled_pair const& pair, int row, int col) const;
    bool conflicts(compiled_pair const& pair, match const& m,
                   std::unordered_set<uint64_t> const& written) const;
    void apply(compiled_pair const& pair, match const& m,
               std::unordered_set<uint64_t>& written);
    // Resolve a write tree to its applied leaves: { all } every item, { any }
    // one weighted draw per node, pre-order (outer before inner) so the PRNG
    // sequence is pinned per seed (spec §10.7).
    void resolve_write(compiled_write_term const& t,
                       std::vector<compiled_pattern const*>& out);

    // Seeded shuffle; under `ordered`, a stable sort by sub-rule priority on
    // top (groups in declaration order, shuffled within each — spec §5.2).
    void order_candidates(compiled_rule const& rule, std::vector<match>& ms);
    // One uniform pick; under `ordered`, uniform within the highest-priority
    // group present (preemptive priority under re-collection).
    match pick_candidate(compiled_rule const& rule, std::vector<match> const& ms);
    // The non-conflicting prefix a full snapshot pass would apply, cut to P%
    // — the `percent` denominator is the applied set, not raw candidates.
    std::vector<match> applicable_prefix(compiled_rule const& rule,
                                         std::vector<match> const& ordered_ms,
                                         int pct) const;
    void add_write_footprint(compiled_pair const& pair, match const& m,
                             std::unordered_set<uint64_t>& out) const;
    bool grids_differ() const;   // back vs front (stabilize change detection)
    void record_highlights(compiled_pair const& pair, match const& m);

    static uint64_t mask_key(int grid_id, int flat) {
        return ((uint64_t)grid_id << 40) | (uint64_t)flat;
    }

    std::shared_ptr<compiled const> prog_;
    std::mt19937_64                 rng_;
    std::vector<grid_state>         grids_;    // indexed by grid id
    bool                            in_batch_{false};
    bool                            observe_{false};
    std::vector<highlight>          highlights_;
};

}  // namespace ls::internal
