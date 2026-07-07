#include "machine.hpp"
#include <algorithm>

namespace ls::internal {

machine::machine(std::shared_ptr<compiled const> prog, uint64_t seed)
    : prog_(std::move(prog)), rng_(seed) {
    grids_.resize(prog_->layers.size());
    for (int i = 0; i < (int)grids_.size(); ++i)
        grids_[i].is_number = prog_->layers[i].tag_id < 0;
}

// The one execution core. Statements run top to bottom; an apply statement is
// a snapshot batch (spec §6.7, step 1: snapshot policy only): collect against
// the front buffer, seeded-shuffle, apply non-conflicting matches to the back
// buffer under the write mask (§6.8), swap. Yields after every application
// and after every completed statement — pullers filter to their granularity.
sequence<step_event> machine::run() {
    for (int si = 0; si < (int)prog_->stmts.size(); ++si) {
        auto const& st = prog_->stmts[si];

        if (st.what == compiled_stmt::kind::op_call) {
            exec_op(st.op);
        } else {
            auto const& rule = prog_->rules[st.rule_id];
            int cap = st.strat == strategy::one  ? 1
                    : st.strat == strategy::some ? st.max_count
                    : -1;   // all

            auto matches = collect(rule);
            std::shuffle(matches.begin(), matches.end(), rng_);

            for (auto& g : grids_) g.back = g.front;
            std::unordered_set<uint64_t> written;
            int applied = 0;
            for (auto const& m : matches) {
                if (cap >= 0 && applied >= cap) break;
                auto const& pair = rule.pairs[m.pair];
                if (conflicts(pair, m, written)) continue;
                apply(pair, m, written);
                ++applied;
                co_yield step_event{step_event::kind::application, si};
            }
            for (auto& g : grids_) std::swap(g.front, g.back);
        }

        co_yield step_event{step_event::kind::statement, si};
    }
}

// resize(W, H): content-preserving, top-left anchored (spec §6.1), all layers.
void machine::exec_op(compiled_op const& op) {
    int nr = op.h, nc = op.w;
    for (auto& g : grids_) {
        std::vector<int64_t> nf((size_t)nr * nc, g.empty_raw());
        int cr = g.rows < nr ? g.rows : nr;
        int cc = g.cols < nc ? g.cols : nc;
        for (int r = 0; r < cr; ++r)
            for (int c = 0; c < cc; ++c)
                nf[r * nc + c] = g.get(r, c);
        g.rows = nr;
        g.cols = nc;
        g.front = std::move(nf);
        g.back.assign((size_t)nr * nc, g.empty_raw());
    }
}

std::vector<machine::match> machine::collect(compiled_rule const& rule) const {
    std::vector<match> out;
    if (grids_.empty()) return out;
    grid_state const& ref = grids_[0];   // all layers share one size

    for (int pi = 0; pi < (int)rule.pairs.size(); ++pi) {
        auto const& pair = rule.pairs[pi];
        if (pair.lhs.empty()) continue;
        int pr = pair.lhs[0].rows, pc = pair.lhs[0].cols;
        for (int r = 0; r + pr <= ref.rows; ++r)
            for (int c = 0; c + pc <= ref.cols; ++c)
                if (match_at(pair, r, c))
                    out.push_back({pi, r, c});
    }
    return out;
}

bool machine::match_at(compiled_pair const& pair, int row, int col) const {
    for (auto const& pat : pair.lhs) {
        if (pat.grid_id < 0) return false;
        grid_state const& g = grids_[pat.grid_id];
        if (row + pat.rows > g.rows || col + pat.cols > g.cols) return false;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                auto const& cell = pat.at(r, c);
                if (cell.what == compiled_cell::kind::wildcard) continue;
                int64_t stored = g.get(row + r, col + c);
                if (pat.is_number) {
                    if (stored != cell.val) return false;          // by value
                } else {
                    if ((stored & cell.val) == 0) return false;    // mask overlap (§4.1)
                }
            }
    }
    return true;
}

// The write footprint is every non-wildcard write cell (spec §5.7); the mask
// is (grid, cell)-keyed and write-only (§6.8).
bool machine::conflicts(compiled_pair const& pair, match const& m,
                        std::unordered_set<uint64_t> const& written) const {
    if (written.empty()) return false;
    for (auto const& pat : pair.writes) {
        if (pat.grid_id < 0) continue;
        int cols = grids_[pat.grid_id].cols;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                if (pat.at(r, c).what == compiled_cell::kind::wildcard) continue;
                if (written.count(mask_key(pat.grid_id, (m.row + r) * cols + (m.col + c))))
                    return true;
            }
    }
    return false;
}

void machine::apply(compiled_pair const& pair, match const& m,
                    std::unordered_set<uint64_t>& written) {
    for (auto const& pat : pair.writes) {
        if (pat.grid_id < 0) continue;
        grid_state& g = grids_[pat.grid_id];
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                auto const& cell = pat.at(r, c);
                if (cell.what == compiled_cell::kind::wildcard) continue;   // '*': preserve
                int flat = (m.row + r) * g.cols + (m.col + c);
                g.back[flat] = cell.val;
                written.insert(mask_key(pat.grid_id, flat));
            }
    }
}

std::shared_ptr<level_data const> machine::snapshot() const {
    auto d = std::make_shared<level_data>();
    d->info = prog_;
    if (!grids_.empty()) {
        d->width  = grids_[0].cols;
        d->height = grids_[0].rows;
    }
    for (int i = 0; i < (int)grids_.size(); ++i)
        d->layers.push_back({prog_->layers[i].tag_id, grids_[i].front});
    return d;
}

}  // namespace ls::internal
