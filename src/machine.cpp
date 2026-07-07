#include "machine.hpp"
#include <algorithm>

namespace ls::internal {

machine::machine(std::shared_ptr<compiled const> prog, uint64_t seed)
    : prog_(std::move(prog)), rng_(seed) {
    grids_.resize(prog_->layers.size());
    for (int i = 0; i < (int)grids_.size(); ++i)
        grids_[i].is_number = prog_->layers[i].tag_id < 0;
    params_.assign(prog_->param_names.size(), 0);
    param_supplied_.assign(prog_->param_names.size(), 0);
}

void machine::set_param(std::string const& name, long long value) {
    int id = prog_->param_id(name);
    if (id < 0) return;   // unknown names are ignored
    params_[id] = value;
    param_supplied_[id] = 1;
}

void machine::bind_params() {
    for (auto const& se : prog_->startup_exprs) {
        if (se.is_default && param_supplied_[se.param]) continue;
        params_[se.param] = eval(se.expr, 0, 0);
    }
}

// ── expression evaluation (§5.8) — total, deterministic per seed ─────────────

long long machine::eval(int idx, int x, int y) {
    auto const& e = prog_->exprs[idx];
    auto w32 = [](long long v) { return (long long)(int32_t)v; };

    switch (e.kind) {
    case ce_kind::int_lit:
    case ce_kind::mask_lit: return e.val;
    case ce_kind::pos_x:    return x;
    case ce_kind::pos_y:    return y;
    case ce_kind::width:    return grids_.empty() ? 0 : grids_[0].cols;
    case ce_kind::height:   return grids_.empty() ? 0 : grids_[0].rows;
    case ce_kind::param_read:
        return e.ref >= 0 && e.ref < (int)params_.size() ? params_[e.ref] : 0;
    case ce_kind::grid_read: {
        if (e.ref < 0 || e.ref >= (int)grids_.size()) return 0;
        grid_state const& g = grids_[e.ref];
        if (y < 0 || y >= g.rows || x < 0 || x >= g.cols) return 0;
        int64_t raw = g.get(y, x);   // the snapshot (front buffer)
        if (g.is_number && raw == num_empty) return 0;   // empty number reads as 0
        return raw;
    }
    case ce_kind::is_empty:
    case ce_kind::is_not_empty: {
        bool empty = true;
        if (e.ref >= 0 && e.ref < (int)grids_.size()) {
            grid_state const& g = grids_[e.ref];
            if (y >= 0 && y < g.rows && x >= 0 && x < g.cols) {
                int64_t raw = g.get(y, x);   // raw — no number→0 coercion
                empty = e.val ? (raw == num_empty) : ((raw & 0x1) != 0);
            }
        }
        return (e.kind == ce_kind::is_empty) == empty ? 1 : 0;
    }
    case ce_kind::neg:  return w32(-eval(e.a, x, y));
    case ce_kind::not_: return eval(e.a, x, y) == 0 ? 1 : 0;
    case ce_kind::add:  return w32(eval(e.a, x, y) + eval(e.b, x, y));
    case ce_kind::sub:  return w32(eval(e.a, x, y) - eval(e.b, x, y));
    case ce_kind::mul:  return w32(eval(e.a, x, y) * eval(e.b, x, y));
    case ce_kind::div_: {
        long long d = eval(e.b, x, y);
        long long n = eval(e.a, x, y);
        return d == 0 ? 0 : w32(n / d);   // n / 0 == 0 (G2)
    }
    case ce_kind::lt:  return eval(e.a, x, y) <  eval(e.b, x, y) ? 1 : 0;
    case ce_kind::le:  return eval(e.a, x, y) <= eval(e.b, x, y) ? 1 : 0;
    case ce_kind::gt:  return eval(e.a, x, y) >  eval(e.b, x, y) ? 1 : 0;
    case ce_kind::ge:  return eval(e.a, x, y) >= eval(e.b, x, y) ? 1 : 0;
    case ce_kind::eq:  return eval(e.a, x, y) == eval(e.b, x, y) ? 1 : 0;
    case ce_kind::ne:  return eval(e.a, x, y) != eval(e.b, x, y) ? 1 : 0;
    case ce_kind::and_: return (eval(e.a, x, y) != 0 && eval(e.b, x, y) != 0) ? 1 : 0;
    case ce_kind::or_:  return (eval(e.a, x, y) != 0 || eval(e.b, x, y) != 0) ? 1 : 0;
    case ce_kind::bit_or: return eval(e.a, x, y) | eval(e.b, x, y);
    case ce_kind::if_:   // eager: both branches evaluate (a random in the dead
                         // branch still draws — defined behaviour, §5.10)
        return eval(e.a, x, y) != 0 ? eval(e.b, x, y) : eval(e.c, x, y);
    case ce_kind::min_: { auto a = eval(e.a, x, y), b = eval(e.b, x, y); return a < b ? a : b; }
    case ce_kind::max_: { auto a = eval(e.a, x, y), b = eval(e.b, x, y); return a > b ? a : b; }
    case ce_kind::abs_: { auto a = eval(e.a, x, y); return w32(a < 0 ? -a : a); }
    case ce_kind::clamp_: {
        auto v = eval(e.a, x, y), lo = eval(e.b, x, y), hi = eval(e.c, x, y);
        auto m = v < hi ? v : hi;
        return lo > m ? lo : m;
    }
    case ce_kind::random_: {   // inclusive [lo, hi]; exactly one draw per call
        auto lo = eval(e.a, x, y), hi = eval(e.b, x, y);
        uint64_t draw = rng_();
        if (hi < lo) return lo;
        uint64_t range = (uint64_t)(hi - lo) + 1;
        return w32(lo + (long long)(draw % range));
    }
    }
    return 0;
}

// The one execution core (spec §6.7). Statements run top to bottom; the
// count × policy algebra lives here and only here — batch generate(),
// progressive stepping, and observation all pull this coroutine.
//
//   snapshot    — one frozen pass: collect, seeded-shuffle (+ `ordered`
//                 priority sort), apply non-conflicting matches to the back
//                 buffer under the write mask (§6.8), swap. `percent` first
//                 sizes the applied set a full pass would make (§6.7) and
//                 keeps its prefix.
//   incremental — re-collect each application; each sees all prior writes.
//                 No mask. `all` runs to the fixpoint.
//   stabilize   — iterated snapshot sweeps until one changes nothing; the
//                 count unit is a sweep.
//
// Yields after every application and every completed statement — pullers
// filter to their granularity.
sequence<step_event> machine::run() {
    bind_params();   // defaults + derived, in declaration order — first draws

    for (int si = 0; si < (int)prog_->stmts.size(); ++si) {
        auto const& st = prog_->stmts[si];

        // A false `when` guard skips the statement in full (§6); it still
        // yields its statement boundary so progress advances.
        if (st.guard >= 0 && eval(st.guard, 0, 0) == 0) {
            co_yield step_event{step_event::kind::statement, si};
            continue;
        }

        if (st.what == compiled_stmt::kind::op_call) {
            exec_op(st.op);
        } else if (st.pol == exec_policy::incremental) {
            auto const& rule = prog_->rules[st.rule_id];
            int cap = st.strat == strategy::one  ? 1
                    : st.strat == strategy::some ? st.max_count
                    : -1;   // all = fixpoint
            int applied = 0;
            while (cap < 0 || applied < cap) {
                auto ms = collect(rule);
                if (ms.empty()) break;
                match m = pick_candidate(rule, ms);
                auto const& pair = rule.pairs[m.pair];
                for (auto& g : grids_) g.back = g.front;
                std::unordered_set<uint64_t> written;
                record_highlights(pair, m);
                apply(pair, m, written);
                for (auto& g : grids_) std::swap(g.front, g.back);
                ++applied;
                co_yield step_event{step_event::kind::application, si};
            }
        } else {
            // Batch family: snapshot = one sweep; stabilize = sweeps to a
            // fixpoint (or the sweep cap).
            auto const& rule = prog_->rules[st.rule_id];
            bool stab = st.pol == exec_policy::stabilize;
            int sweep_cap = stab ? (st.strat == strategy::all ? -1 : st.max_count) : 1;
            int cap = (stab || st.is_percent) ? -1
                    : st.strat == strategy::one  ? 1
                    : st.strat == strategy::some ? st.max_count
                    : -1;
            int sweeps = 0;
            while (sweep_cap < 0 || sweeps < sweep_cap) {
                auto ms = collect(rule);
                order_candidates(rule, ms);
                if (st.is_percent)
                    ms = applicable_prefix(rule, ms, st.percent);

                for (auto& g : grids_) g.back = g.front;
                std::unordered_set<uint64_t> written;
                int applied = 0;
                in_batch_ = true;
                for (auto const& m : ms) {
                    if (cap >= 0 && applied >= cap) break;
                    auto const& pair = rule.pairs[m.pair];
                    if (conflicts(pair, m, written)) continue;
                    record_highlights(pair, m);
                    apply(pair, m, written);
                    ++applied;
                    co_yield step_event{step_event::kind::application, si};
                }
                in_batch_ = false;
                bool changed = grids_differ();
                for (auto& g : grids_) std::swap(g.front, g.back);
                ++sweeps;
                if (!stab || !changed) break;
            }
        }

        co_yield step_event{step_event::kind::statement, si};
    }
}

void machine::order_candidates(compiled_rule const& rule, std::vector<match>& ms) {
    std::shuffle(ms.begin(), ms.end(), rng_);
    if (rule.body == body_combinator::ordered)
        std::stable_sort(ms.begin(), ms.end(), [&rule](match const& a, match const& b) {
            return rule.pairs[a.pair].sub_rule_idx < rule.pairs[b.pair].sub_rule_idx;
        });
}

machine::match machine::pick_candidate(compiled_rule const& rule,
                                       std::vector<match> const& ms) {
    if (rule.body == body_combinator::ordered) {
        int best = rule.pairs[ms[0].pair].sub_rule_idx;
        for (auto const& m : ms)
            best = std::min(best, rule.pairs[m.pair].sub_rule_idx);
        std::vector<match> pool;
        for (auto const& m : ms)
            if (rule.pairs[m.pair].sub_rule_idx == best) pool.push_back(m);
        return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng_)];
    }
    return ms[std::uniform_int_distribution<size_t>(0, ms.size() - 1)(rng_)];
}

std::vector<machine::match> machine::applicable_prefix(
        compiled_rule const& rule, std::vector<match> const& ordered_ms,
        int pct) const {
    std::vector<match> app;
    std::unordered_set<uint64_t> sim;
    for (auto const& m : ordered_ms) {
        auto const& pair = rule.pairs[m.pair];
        if (conflicts(pair, m, sim)) continue;
        app.push_back(m);
        add_write_footprint(pair, m, sim);
    }
    int want = (int)((long long)pct * (long long)app.size() / 100);
    if ((int)app.size() > want) app.resize(want);
    return app;
}

void machine::add_write_footprint(compiled_pair const& pair, match const& m,
                                  std::unordered_set<uint64_t>& out) const {
    std::vector<compiled_pattern const*> leaves;
    collect_write_leaves(pair.rhs, leaves);
    for (auto const* pp : leaves) {
        auto const& pat = *pp;
        if (pat.grid_id < 0) continue;
        int cols = grids_[pat.grid_id].cols;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                if (pat.at(r, c).what == compiled_cell::kind::wildcard) continue;
                out.insert(mask_key(pat.grid_id, (m.row + r) * cols + (m.col + c)));
            }
    }
}

bool machine::grids_differ() const {
    for (auto const& g : grids_)
        if (g.front != g.back) return true;
    return false;
}

// Matched cells (full LHS rects) + possible write cells (all leaves).
void machine::record_highlights(compiled_pair const& pair, match const& m) {
    if (!observe_) return;
    highlights_.clear();
    for (auto const& pat : pair.lhs) {
        if (pat.grid_id < 0) continue;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c)
                highlights_.push_back({pat.grid_id, m.row + r, m.col + c, false});
    }
    std::vector<compiled_pattern const*> leaves;
    collect_write_leaves(pair.rhs, leaves);
    for (auto const* pp : leaves) {
        if (pp->grid_id < 0) continue;
        for (int r = 0; r < pp->rows; ++r)
            for (int c = 0; c < pp->cols; ++c)
                highlights_.push_back({pp->grid_id, m.row + r, m.col + c, true});
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

std::vector<machine::match> machine::collect(compiled_rule const& rule) {
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

bool machine::match_at(compiled_pair const& pair, int row, int col) {
    for (auto const& pat : pair.lhs) {
        if (pat.is_where) {   // §5.9: every cell's boolean must hold
            for (int r = 0; r < pat.rows; ++r)
                for (int c = 0; c < pat.cols; ++c) {
                    auto const& cell = pat.at(r, c);
                    if (cell.expr < 0) continue;
                    if (eval(cell.expr, col + c, row + r) == 0) return false;
                }
            continue;
        }
        if (pat.grid_id < 0) return false;
        grid_state const& g = grids_[pat.grid_id];
        if (row + pat.rows > g.rows || col + pat.cols > g.cols) return false;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                auto const& cell = pat.at(r, c);
                if (cell.what == compiled_cell::kind::wildcard) continue;
                int64_t stored = g.get(row + r, col + c);
                long long want = cell.what == compiled_cell::kind::expr
                               ? eval(cell.expr, col + c, row + r)
                               : cell.val;
                if (pat.is_number) {
                    if (stored != want) return false;          // by value
                } else {
                    if ((stored & want) == 0) return false;    // mask overlap (§4.1)
                }
            }
    }
    return true;
}

// The write footprint is every non-wildcard cell of every write-tree leaf —
// conservative across { any } branches (spec §5.7); the mask is (grid,
// cell)-keyed and write-only (§6.8).
bool machine::conflicts(compiled_pair const& pair, match const& m,
                        std::unordered_set<uint64_t> const& written) const {
    if (written.empty()) return false;
    std::vector<compiled_pattern const*> leaves;
    collect_write_leaves(pair.rhs, leaves);
    for (auto const* pp : leaves) {
        auto const& pat = *pp;
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

void machine::resolve_write(compiled_write_term const& t,
                            std::vector<compiled_pattern const*>& out) {
    switch (t.what) {
    case compiled_write_term::kind::leaf:
        out.push_back(&t.pattern);
        return;
    case compiled_write_term::kind::all:
        for (auto const& it : t.items) resolve_write(it, out);
        return;
    case compiled_write_term::kind::any: {
        if (t.items.empty()) return;
        int total = 0;
        for (auto const& it : t.items) total += it.weight;
        int chosen = 0;
        if (total > 0) {
            int roll = (int)std::uniform_int_distribution<int>(0, total - 1)(rng_);
            int acc = 0;   // the node's one draw, before recursing (outer-first)
            for (int i = 0; i < (int)t.items.size(); ++i) {
                acc += t.items[i].weight;
                if (roll < acc) { chosen = i; break; }
            }
        }
        resolve_write(t.items[chosen], out);
        return;
    }
    }
}

void machine::apply(compiled_pair const& pair, match const& m,
                    std::unordered_set<uint64_t>& written) {
    std::vector<compiled_pattern const*> writes;
    resolve_write(pair.rhs, writes);
    for (auto const* pp : writes) {
        auto const& pat = *pp;
        if (pat.grid_id < 0) continue;
        grid_state& g = grids_[pat.grid_id];
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                auto const& cell = pat.at(r, c);
                if (cell.what == compiled_cell::kind::wildcard) continue;   // '*': preserve
                int flat = (m.row + r) * g.cols + (m.col + c);
                g.back[flat] = cell.what == compiled_cell::kind::expr
                             ? eval(cell.expr, m.col + c, m.row + r)
                             : cell.val;
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
    // Mid-batch the back buffer is the visible state: committed statements
    // plus this batch's applications so far.
    for (int i = 0; i < (int)grids_.size(); ++i)
        d->layers.push_back({prog_->layers[i].tag_id,
                             in_batch_ ? grids_[i].back : grids_[i].front});
    return d;
}

}  // namespace ls::internal
