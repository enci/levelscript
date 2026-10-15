#include "machine.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <queue>
#include <tuple>

namespace ls::internal {

machine::machine(std::shared_ptr<compiled const> prog, uint64_t seed, int entry)
    : prog_(std::move(prog)), entry_(entry), rng_(seed) {
    grids_.resize(prog_->layers.size());
    for (int i = 0; i < (int)grids_.size(); ++i)
        grids_[i].is_number = prog_->layers[i].tag_id < 0;
    params_.assign(prog_->param_names.size(), 0);
    param_supplied_.assign(prog_->param_names.size(), 0);
    graphs_.resize(prog_->graphs.size());
    selections_.resize(prog_->selectors.size());
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

// The pure operators (neg .. clamp_) over already-evaluated operands; unused
// operands are 0.
static long long ce_pure(ce_kind k, long long a, long long b, long long c) {
    auto w32 = [](long long v) { return (long long)(int32_t)v; };
    switch (k) {
    case ce_kind::neg:    return w32(-a);
    case ce_kind::not_:   return a == 0;
    case ce_kind::add:    return w32(a + b);
    case ce_kind::sub:    return w32(a - b);
    case ce_kind::mul:    return w32(a * b);
    case ce_kind::div_:   return b == 0 ? 0 : w32(a / b);   // n / 0 == 0
    case ce_kind::lt:     return a <  b;
    case ce_kind::le:     return a <= b;
    case ce_kind::gt:     return a >  b;
    case ce_kind::ge:     return a >= b;
    case ce_kind::eq:     return a == b;
    case ce_kind::ne:     return a != b;
    case ce_kind::and_:   return a != 0 && b != 0;
    case ce_kind::or_:    return a != 0 || b != 0;
    case ce_kind::bit_or: return a | b;
    case ce_kind::if_:    return a != 0 ? b : c;
    case ce_kind::min_:   return a < b ? a : b;
    case ce_kind::max_:   return a > b ? a : b;
    case ce_kind::abs_:   return w32(a < 0 ? -a : a);
    case ce_kind::clamp_: return std::max(b, std::min(a, c));
    default:              return 0;   // leaves and random_: not pure operators
    }
}

// ── expression evaluation — total, deterministic per seed ────────────────────

long long machine::eval(int idx, int x, int y) {
    auto const& e = prog_->exprs[idx];
    auto w32 = [](long long v) { return (long long)(int32_t)v; };

    switch (e.kind) {
    case ce_kind::int_lit:
    case ce_kind::mask_lit: return e.val;
    case ce_kind::pos_x:    return x;
    case ce_kind::pos_y:    return y;
    case ce_kind::width:    return cols_;
    case ce_kind::height:   return rows_;
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
    case ce_kind::var_read:
        return cur_pair_ ? var_value(*cur_pair_, e.ref, anchor_row_, anchor_col_) : 0;
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
    case ce_kind::has: {
        if (e.ref < 0 || e.ref >= (int)grids_.size()) return 0;
        grid_state const& g = grids_[e.ref];
        if (y < 0 || y >= g.rows || x < 0 || x >= g.cols) return 0;
        return (g.get(y, x) & e.val) != 0;
    }
    case ce_kind::degree_:
    case ce_kind::size_:
    case ce_kind::distance_:
    case ce_kind::distance_raw:
    case ce_kind::neighbors_:
        return eval_graph(e, x, y);
    case ce_kind::and_: return eval(e.a, x, y) != 0 && eval(e.b, x, y) != 0;   // lazy
    case ce_kind::or_:  return eval(e.a, x, y) != 0 || eval(e.b, x, y) != 0;
    case ce_kind::random_: {   // inclusive [lo, hi]; exactly one draw per call
        auto lo = eval(e.a, x, y), hi = eval(e.b, x, y);
        // an empty range still draws once (uniform(1)) and yields lo
        uint64_t range = hi < lo ? 1 : (uint64_t)(hi - lo) + 1;
        return w32(lo + (long long)uniform(range));
    }
    default: {   // the pure operators; operands left to right (an eager `if`
                 // evaluates both branches)
        long long a = e.a >= 0 ? eval(e.a, x, y) : 0;
        long long b = e.b >= 0 ? eval(e.b, x, y) : 0;
        long long c = e.c >= 0 ? eval(e.c, x, y) : 0;
        return ce_pure(e.kind, a, b, c);
    }
    }
}

// ── graphs: extracted from the snapshot, cached until their inputs change ────

graph_data const& machine::graph(int id) {
    auto& c = graphs_[(size_t)id];
    auto const& g = prog_->graphs[(size_t)id];
    grid_state const& src = grids_[(size_t)g.source];
    if (c.version != src.version) {
        c.data = extract_graph(g, src.is_number, src.front, src.cols, src.rows);
        c.version = src.version;
    }
    return c.data;
}

std::vector<int> const& machine::selection(int id, int sel, bool distance) {
    auto& c = selections_[(size_t)sel];
    auto const& s = prog_->selectors[(size_t)sel];
    grid_state const& src = grids_[(size_t)prog_->graphs[(size_t)id].source];
    grid_state const& sg  = grids_[(size_t)s.grid];
    if (c.source_version == src.version && c.grid_version == sg.version) return c.per_node;
    c.source_version = src.version;
    c.grid_version   = sg.version;

    graph_data const& g = graph(id);
    std::vector<char> picked((size_t)g.node_count(), 0);
    for (size_t i = 0; i < g.node_of.size() && i < sg.front.size(); ++i) {
        int node = g.node_of[i];
        if (node < 0 || picked[(size_t)node]) continue;
        int64_t v = sg.front[i];
        if (sg.is_number ? (v == s.val) != s.ne : (v & s.val) != 0) picked[(size_t)node] = 1;
    }
    c.per_node.assign((size_t)g.node_count(), distance ? -1 : 0);
    if (distance) {   // one breadth-first search from every selected node
        std::queue<int> q;
        for (int n = 0; n < g.node_count(); ++n)
            if (picked[(size_t)n]) { c.per_node[(size_t)n] = 0; q.push(n); }
        while (!q.empty()) {
            int n = q.front();
            q.pop();
            for (int m : g.adj[(size_t)n])
                if (c.per_node[(size_t)m] < 0) {
                    c.per_node[(size_t)m] = c.per_node[(size_t)n] + 1;
                    q.push(m);
                }
        }
    } else {
        for (int n = 0; n < g.node_count(); ++n)
            for (int m : g.adj[(size_t)n]) c.per_node[(size_t)n] += picked[(size_t)m];
    }
    return c.per_node;
}

// A graph function at (x, y): about the node covering it (none reads as 0).
long long machine::eval_graph(compiled_expr const& e, int x, int y) {
    graph_data const& g = graph(e.ref);
    int node = g.node_at(x, y);
    if (node < 0) return 0;
    switch (e.kind) {
    case ce_kind::degree_: return (long long)g.adj[(size_t)node].size();
    case ce_kind::size_:   return g.sizes[(size_t)node];
    case ce_kind::neighbors_:
        return e.val < 0 ? 0 : selection(e.ref, (int)e.val, false)[(size_t)node];
    default: {   // distance: unreachable is empty, and reads as 0 unless raw
        int d = e.val < 0 ? -1 : selection(e.ref, (int)e.val, true)[(size_t)node];
        if (d >= 0) return d;
        return e.kind == ce_kind::distance_raw ? num_empty : 0;
    }
    }
}

// The one execution core. Statements run top to bottom; the mode algebra lives
// here and only here — batch generate(), progressive stepping, and observation
// all pull this coroutine.
//
//   scatter, everywhere — one frozen pass: collect, seeded-shuffle (+ `ordered`
//                 priority sort), apply non-conflicting matches to the back
//                 buffer under the write mask, swap. `P%` first sizes the
//                 applied set a full pass would make and keeps its prefix.
//   once, grow  — re-collect each application; each sees all prior writes.
//                 No mask. `grow` without a count runs to the fixpoint.
//   settle      — iterated batch sweeps until one changes nothing; the
//                 count unit is a sweep.
//
// Yields after every application and every completed statement — pullers
// filter to their granularity.
// A run binds the params, then applies the entry exactly as `once S` to the
// empty stack: one iteration of its body - no guard, no draw, no
// stability check. Its body statements are frame 0 of the statement stack.
sequence<step_event> machine::run() {
    if (entry_ < 0 || entry_ >= (int)prog_->sequences.size()) co_return;
    bind_params();   // defaults + derived, in canonical order — first draws

    auto const& body = prog_->sequences[(size_t)entry_].stmts;
    for (int si = 0; si < (int)body.size(); ++si) {
        auto const& st = body[(size_t)si];
        frames_.assign(1, frame{si, 0, 1});
        bool seq = st.what == compiled_stmt::kind::apply && st.seq_id >= 0;

        // A false `when` guard skips the statement in full. A skipped
        // leaf still yields its boundary so progress advances; applying a
        // sequence is never a step of its own.
        if (st.guard >= 0 && eval(st.guard, 0, 0) == 0) {
            if (!seq) co_yield step_event{step_event::kind::statement, si};
            continue;
        }
        int count = eval_count(st);
        co_yield begin_event(si);
        if (seq) {
            auto sub = run_sequence(st, si, count);
            while (sub.next()) co_yield sub.value();
            continue;
        }
        auto sub = run_leaf(st, si, count);
        while (sub.next()) co_yield sub.value();
        co_yield step_event{step_event::kind::statement, si};
    }
}

int machine::eval_count(compiled_stmt const& st) {
    if (st.count < 0) return -1;
    long long v = eval(st.count, 0, 0);
    char const* pct = st.percent ? "%" : "";
    if (v < 0) {
        std::cerr << "warning: a count of " << v << pct << " is below 0; treating it as 0\n";
        v = 0;
    }
    if (st.percent && v > 100) {
        std::cerr << "warning: a percentage of " << v << "% is above 100; treating it as 100%\n";
        v = 100;
    }
    return (int)v;
}

sequence<step_event> machine::run_leaf(compiled_stmt const& st, int top, int count) {
    if (st.what == compiled_stmt::kind::op_call) {
        exec_op(st.op);
    } else if (count == 0) {
        // a computed count of 0 (or 0%) applies nothing - and draws nothing
    } else if (is_step_mode(st.mode)) {
        auto const& rule = prog_->rules[st.rule_id];
        int cap = st.mode == apply_mode::once ? 1 : count;   // -1 = fixpoint
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
            co_yield step_event{step_event::kind::application, top};
        }
    } else {
        // Batch modes = one sweep; settle = sweeps to a fixpoint (or the
        // sweep cap).
        auto const& rule = prog_->rules[st.rule_id];
        bool stab = st.mode == apply_mode::settle;
        int sweep_cap = stab ? count : 1;
        int cap = st.mode == apply_mode::scatter && !st.percent ? count : -1;
        int sweeps = 0;
        while (sweep_cap < 0 || sweeps < sweep_cap) {
            auto ms = collect(rule);
            order_candidates(rule, ms);
            if (st.percent)
                ms = applicable_prefix(rule, ms, count);

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
                co_yield step_event{step_event::kind::application, top};
            }
            in_batch_ = false;
            bool changed = grids_differ();
            for (auto& g : grids_) std::swap(g.front, g.back);
            ++sweeps;
            if (!stab || !changed) break;
        }
    }

}

// A sequence application: `once` = 1 iteration; `settle(N)` = up to N, stopping after a
// stable one; `settle` = until one is stable. Body statements run exactly as in
// the program; the frames record where each step happened.
sequence<step_event> machine::run_sequence(compiled_stmt const& st, int top, int count) {
    auto const& body = prog_->sequences[st.seq_id].stmts;
    int cap = st.mode == apply_mode::once ? 1 : count;   // -1 = fixpoint
    size_t depth = frames_.size();
    for (int it = 0; cap < 0 || it < cap; ++it) {
        bool check = cap != 1;   // `once S` never needs the comparison
        stack_state start;
        if (check) start = capture();
        for (int j = 0; j < (int)body.size(); ++j) {
            auto const& bs = body[j];
            frames_.resize(depth);
            frames_.push_back(frame{j, it, cap});
            bool seq = bs.what == compiled_stmt::kind::apply && bs.seq_id >= 0;
            if (bs.guard >= 0 && eval(bs.guard, 0, 0) == 0) {   // once per iteration
                if (!seq) co_yield step_event{step_event::kind::statement, top};
                continue;
            }
            int n = eval_count(bs);   // after the guard, once per iteration
            co_yield begin_event(top);
            if (seq) {
                auto sub = run_sequence(bs, top, n);
                while (sub.next()) co_yield sub.value();
                continue;
            }
            auto sub = run_leaf(bs, top, n);
            while (sub.next()) co_yield sub.value();
            co_yield step_event{step_event::kind::statement, top};
        }
        frames_.resize(depth);
        if (check && unchanged_since(start)) break;
    }
}

machine::stack_state machine::capture() const {
    stack_state s{rows_, cols_, {}};
    s.cells.reserve(grids_.size());
    for (auto const& g : grids_) s.cells.push_back(g.front);
    return s;
}

// Stability compares states, not writes: dimensions plus every cell of every
// layer. A cell written back to its old value is no change.
bool machine::unchanged_since(stack_state const& s) const {
    if (s.rows != rows_ || s.cols != cols_) return false;
    for (size_t i = 0; i < grids_.size(); ++i)
        if (grids_[i].front != s.cells[i]) return false;
    return true;
}

void machine::order_candidates(compiled_rule const& rule, std::vector<match>& ms) {
    shuffle(ms);
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
        return pool[(size_t)uniform(pool.size())];
    }
    return ms[(size_t)uniform(ms.size())];
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

// Rebuild one grid to nr x nc, each new cell from src(r, c) — the shared
// skeleton of every geometric operation, so each op is just a coordinate
// mapping.
template <class F>
static void rebuild(grid_state& g, int nr, int nc, F&& src) {
    std::vector<int64_t> nf((size_t)nr * nc, g.empty_raw());
    for (int r = 0; r < nr; ++r)
        for (int c = 0; c < nc; ++c)
            nf[r * nc + c] = src(r, c);
    g.rows = nr;
    g.cols = nc;
    g.front = std::move(nf);
    g.back.assign((size_t)nr * nc, g.empty_raw());
    ++g.version;
}

void machine::exec_op(compiled_op const& op) {
    // `int` arguments evaluate now, after the guard, in written order;
    // out-of-range values are handled below.
    long long arg[8] = {};   // by the parameter's position in the operation table
    for (auto const& ia : op.int_args) arg[ia.param] = eval(ia.expr, 0, 0);

    switch (op.kind) {

    case op_kind::resize: { // content-preserving, top-left anchored
        if (arg[0] < 1 || arg[1] < 1) {
            std::cerr << "warning: resize(" << arg[0] << ", " << arg[1]
                      << ") has a dimension below 1; leaving grids unchanged\n";
            return;
        }
        int w = (int)arg[0], h = (int)arg[1];
        rows_ = h;
        cols_ = w;
        for (auto& g : grids_)
            rebuild(g, h, w, [&g](int r, int c) {
                return r < g.rows && c < g.cols ? g.get(r, c) : g.empty_raw();
            });
        return;
    }

    case op_kind::upscale: { // duplicate every cell into an n x m block
        if (arg[0] < 1 || arg[1] < 1)
            std::cerr << "warning: upscale(" << arg[0] << ", " << arg[1]
                      << ") has a factor below 1; treating it as 1\n";
        int n = arg[0] < 1 ? 1 : (int)arg[0], m = arg[1] < 1 ? 1 : (int)arg[1];
        rows_ *= m;
        cols_ *= n;
        for (auto& g : grids_)
            rebuild(g, g.rows * m, g.cols * n, [&g, n, m](int r, int c) {
                return g.get(r / m, c / n);
            });
        return;
    }

    case op_kind::pad: {    // uniform empty border on all sides
        if (arg[0] < 0)
            std::cerr << "warning: pad(" << arg[0]
                      << ") has a margin below 0; treating it as 0\n";
        int n = arg[0] < 0 ? 0 : (int)arg[0];
        if (n == 0) return;
        rows_ += 2 * n;
        cols_ += 2 * n;
        for (auto& g : grids_)
            rebuild(g, g.rows + 2 * n, g.cols + 2 * n, [&g, n](int r, int c) {
                return r >= n && r < g.rows + n && c >= n && c < g.cols + n
                     ? g.get(r - n, c - n) : g.empty_raw();
            });
        return;
    }

    case op_kind::trim: {   // crop all layers to the union content box
        bool found = false;
        int min_r = 0, min_c = 0, max_r = 0, max_c = 0;
        for (auto const& g : grids_)
            for (int r = 0; r < g.rows; ++r)
                for (int c = 0; c < g.cols; ++c) {
                    if (g.get(r, c) == g.empty_raw()) continue;
                    if (!found) { min_r = max_r = r; min_c = max_c = c; found = true; }
                    else {
                        min_r = std::min(min_r, r); max_r = std::max(max_r, r);
                        min_c = std::min(min_c, c); max_c = std::max(max_c, c);
                    }
                }
        if (!found) {
            std::cerr << "warning: trim() on an entirely empty stack; "
                         "leaving grids unchanged\n";
            return;
        }
        rows_ = max_r - min_r + 1;
        cols_ = max_c - min_c + 1;
        for (auto& g : grids_)
            rebuild(g, max_r - min_r + 1, max_c - min_c + 1,
                    [&g, min_r, min_c](int r, int c) {
                        return g.get(min_r + r, min_c + c);
                    });
        return;
    }

    case op_kind::mirror:   // fold the origin-side half onto the far side
        for (auto& g : grids_) {
            if (op.w == 1) {   // horizontal: left half onto the right, reflected
                for (int r = 0; r < g.rows; ++r)
                    for (int c = 0; c < g.cols / 2; ++c)
                        g.front[r * g.cols + (g.cols - 1 - c)] = g.get(r, c);
            } else {           // vertical: top half onto the bottom
                for (int r = 0; r < g.rows / 2; ++r)
                    for (int c = 0; c < g.cols; ++c)
                        g.front[(g.rows - 1 - r) * g.cols + c] = g.get(r, c);
            }
            g.back = g.front;
            ++g.version;
        }
        return;

    case op_kind::path: {
        long long conn = arg[compiled_op::path_connectivity];
        if (conn != 4 && conn != 8)
            std::cerr << "warning: path() connectivity " << conn
                      << " is not 4 or 8; treating it as 4\n";
        run_path(op, conn == 8 ? 8 : 4);
        return;
    }
    }
}

// Stateless per-cell mixer (splitmix64) for path tie keys.
static uint64_t mix64(uint64_t v) {
    v += 0x9e3779b97f4a7c15ull;
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ull;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebull;
    return v ^ (v >> 31);
}

// path(...): stamp a minimum-cost route from any `from` cell to the
// nearest `to` cell over the traversable cells, endpoints included.
//
// Draw contract (the algorithm-unobservable design): predicate passes and
// per-cell costs evaluate row-major over fixed extents; tie-breaking uses ONE
// stream draw hashed per cell (never per discovery); the route is derived
// from the distance field alone — walk from the goal through the min-key
// optimal predecessor — so any correct shortest-path search yields the same
// output, and a future A* is a pure optimization.
void machine::run_path(compiled_op const& op, int ndirs) {
    if (grids_.empty() || grids_[0].rows <= 0 || op.into_grid < 0) {
        std::cerr << "warning: path() before any resize; leaving grids unchanged\n";
        return;
    }
    int W = grids_[0].cols, H = grids_[0].rows;
    size_t n = (size_t)W * H;

    auto pred_at = [&](int expr, int x, int y) { return expr >= 0 && eval(expr, x, y) != 0; };
    auto default_passable = [&](int x, int y) -> bool {
        for (auto const& g : grids_)   // non-empty in at least one layer
            if (g.get(y, x) != g.empty_raw()) return true;
        return false;
    };

    // predicate sets: one row-major pass each, in from / to / passable order
    std::vector<char> is_from(n), is_to(n), pass(n);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            is_from[y * W + x] = pred_at(op.from, x, y);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            is_to[y * W + x] = pred_at(op.to, x, y);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            size_t i = (size_t)y * W + x;
            bool p = op.passable >= 0 ? pred_at(op.passable, x, y)
                                       : default_passable(x, y);
            pass[i] = p || is_from[i] || is_to[i];   // endpoints always traversable
        }

    // per-cell entry cost: row-major over traversable cells, clamped >= 1
    std::vector<long long> costv(n, 1);
    if (op.cost >= 0)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                size_t i = (size_t)y * W + x;
                if (!pass[i]) continue;
                long long cv = eval(op.cost, x, y);
                costv[i] = cv < 1 ? 1 : cv;
            }

    // ONE stream draw; every tie key is a stateless hash of it and the cell
    uint64_t tie_seed = rng_();
    auto key = [tie_seed](int i) { return mix64(tie_seed ^ (uint64_t)i); };

    // multi-source Dijkstra → the distance field (unique; the search is a
    // black box, nothing downstream observes its expansion order)
    const long long inf = std::numeric_limits<long long>::max();
    std::vector<long long> dist(n, inf);
    using entry = std::pair<long long, int>;
    std::priority_queue<entry, std::vector<entry>, std::greater<entry>> pq;
    for (size_t i = 0; i < n; ++i)
        if (is_from[i]) { dist[i] = 0; pq.push({0, (int)i}); }

    static const int dx[] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!pq.empty()) {
        auto [d, i] = pq.top();
        pq.pop();
        if (d != dist[i]) continue;   // stale
        int x = i % W, y = i / W;
        for (int k = 0; k < ndirs; ++k) {
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            int j = ny * W + nx;
            if (!pass[j]) continue;
            long long nd = d + costv[j];
            if (nd < dist[j]) { dist[j] = nd; pq.push({nd, j}); }
        }
    }

    // nearest reachable goal: min (dist, key)
    int goal = -1;
    for (size_t i = 0; i < n; ++i) {
        if (!is_to[i] || dist[i] == inf) continue;
        if (goal < 0 || dist[i] < dist[goal] ||
            (dist[i] == dist[goal] && key((int)i) < key(goal)))
            goal = (int)i;
    }
    if (goal < 0) {
        std::cerr << "warning: path() found no route from 'from' to 'to'; "
                     "leaving grids unchanged\n";
        return;
    }

    // route: walk goal -> start via the min-key optimal predecessor — a pure
    // function of the distance field and the keys
    std::vector<int> route{goal};
    int c = goal;
    while (!is_from[c] || dist[c] != 0) {
        int x = c % W, y = c / W, best = -1;
        for (int k = 0; k < ndirs; ++k) {
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            int j = ny * W + nx;
            if (!pass[j] || dist[j] == inf) continue;
            if (dist[j] + costv[c] != dist[c]) continue;   // not an optimal predecessor
            if (best < 0 || key(j) < key(best)) best = j;
        }
        if (best < 0) break;   // unreachable in a well-formed field
        route.push_back(best);
        c = best;
    }
    std::reverse(route.begin(), route.end());   // stamp start -> goal

    grid_state& g = grids_[op.into_grid];
    for (int i : route) {
        long long v = eval(op.write, i % W, i / W);
        g.front[i] = v;
    }
    g.back = g.front;
    ++g.version;
}

// Candidates in the pinned collection order: anchors row-major, and at each
// anchor every variant whose pattern fits there, in declaration order.
// Match-side `random` draws fire in exactly this order.
std::vector<machine::match> machine::collect(compiled_rule const& rule) {
    std::vector<match> out;
    if (rows_ <= 0 || cols_ <= 0) return out;
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c)
            for (int pi = 0; pi < (int)rule.pairs.size(); ++pi) {
                auto const& pair = rule.pairs[pi];
                if (pair.lhs.empty()) continue;
                if (r + pair.lhs[0].rows > rows_ || c + pair.lhs[0].cols > cols_) continue;
                if (match_at(pair, r, c)) out.push_back({pi, r, c});
            }
    return out;
}

// Two phases: every bare cell of every pattern first - pure, so
// their order is unobservable - then expression and `where` cells in pattern
// order, row-major. Only phase 2 can draw, so a candidate its bare cells
// reject consumes no draws.
bool machine::match_at(compiled_pair const& pair, int row, int col) {
    cur_pair_ = &pair;
    anchor_row_ = row;
    anchor_col_ = col;
    for (auto const& pat : pair.lhs) {
        if (pat.is_where) continue;
        if (pat.grid_id < 0) return false;
        grid_state const& g = grids_[pat.grid_id];
        if (row + pat.rows > g.rows || col + pat.cols > g.cols) return false;
        if (pat.is_number) {
            for (auto const& p : pat.bare)   // by value
                if (g.get(row + p.r, col + p.c) != p.val) return false;
            for (auto const& p : pat.bare_ne)   // `!.`: any stored number
                if (g.get(row + p.r, col + p.c) == p.val) return false;
        } else {
            for (auto const& p : pat.bare)   // mask overlap
                if ((g.get(row + p.r, col + p.c) & p.val) == 0) return false;
        }
    }
    // Binding cells: each site is non-empty, every other binding cell equals
    // its site exactly. Their patterns' bounds were checked above.
    for (auto const& s : pair.var_sites) {
        grid_state const& g = grids_[(size_t)s.grid];
        int64_t v = g.get(row + s.r, col + s.c);
        if (g.is_number ? v == num_empty : (v & tag_empty) != 0) return false;
    }
    for (auto const& k : pair.var_checks)
        if (grids_[(size_t)k.at.grid].get(row + k.at.r, col + k.at.c) !=
            var_value(pair, k.slot, row, col)) return false;
    for (auto const& pat : pair.lhs) {
        if (pat.computed.empty()) continue;
        if (pat.graph_id >= 0) {   // each cell tests the node covering it
            for (auto const& p : pat.computed) {
                if (graph(pat.graph_id).node_at(col + p.c, row + p.r) < 0) return false;
                if (eval(p.expr, col + p.c, row + p.r) == 0) return false;
            }
            continue;
        }
        if (pat.is_where) {   // every cell's boolean must hold
            for (auto const& p : pat.computed)
                if (eval(p.expr, col + p.c, row + p.r) == 0) return false;
            continue;
        }
        grid_state const& g = grids_[pat.grid_id];
        for (auto const& p : pat.computed) {
            int64_t stored = g.get(row + p.r, col + p.c);
            long long want = eval(p.expr, col + p.c, row + p.r);
            if (pat.is_number ? stored != want : (stored & want) == 0) return false;
        }
    }
    return true;
}

// The write footprint is every non-wildcard cell of every write-tree leaf —
// conservative across { any } branches; the mask is (grid, cell)-keyed and
// write-only.
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
            int roll = (int)uniform((uint64_t)total);
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
    cur_pair_ = &pair;
    anchor_row_ = m.row;
    anchor_col_ = m.col;
    std::vector<compiled_pattern const*> writes;
    resolve_write(pair.rhs, writes);
    for (auto const* pp : writes) {
        auto const& pat = *pp;
        if (pat.grid_id < 0) continue;
        grid_state& g = grids_[pat.grid_id];
        ++g.version;
        for (int r = 0; r < pat.rows; ++r)
            for (int c = 0; c < pat.cols; ++c) {
                auto const& cell = pat.at(r, c);
                if (cell.what == compiled_cell::kind::wildcard) continue;   // '*': preserve
                int flat = (m.row + r) * g.cols + (m.col + c);
                g.back[flat] = cell.what == compiled_cell::kind::expr
                             ? eval(cell.expr, m.col + c, m.row + r)
                             : cell.what == compiled_cell::kind::variable
                             ? var_value(pair, cell.var, m.row, m.col)
                             : cell.val;
                written.insert(mask_key(pat.grid_id, flat));
            }
    }
}

std::shared_ptr<level_data const> machine::snapshot() const {
    auto d = std::make_shared<level_data>();
    d->info = prog_;
    d->width  = cols_;
    d->height = rows_;
    // Mid-batch the back buffer is the visible state: committed statements
    // plus this batch's applications so far.
    for (int i = 0; i < (int)grids_.size(); ++i)
        d->layers.push_back({prog_->layers[i].tag_id,
                             in_batch_ ? grids_[i].back : grids_[i].front});
    return d;
}

}  // namespace ls::internal
