#include "machine.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <tuple>

namespace ls::internal {

machine::machine(std::shared_ptr<compiled const> prog, uint64_t seed, int entry,
                 std::function<void(std::string const&)> warn)
    : prog_(std::move(prog)), entry_(entry), warn_(std::move(warn)), rng_(seed) {
    grids_.resize(prog_->layers.size());
    for (int i = 0; i < (int)grids_.size(); ++i)
        grids_[i].is_number = prog_->layers[i].tag_id < 0;
    params_.assign(prog_->param_names.size(), 0);
    param_supplied_.assign(prog_->param_names.size(), 0);
    graphs_.resize(prog_->graphs.size());
    selections_.resize(prog_->selectors.size());
}

void machine::warn(std::string msg) const {
    if (!warn_) return;
    auto const& labels = prog_->module_labels;
    std::string file = stmt_loc_.mod >= 0 && stmt_loc_.mod < (int)labels.size()
                     ? labels[(size_t)stmt_loc_.mod] : "";
    warn_(diagnostic{false, file, stmt_loc_.line, stmt_loc_.col,
                     stmt_loc_.end_line, stmt_loc_.end_col, std::move(msg)}.format());
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
    stmt_loc_ = st.loc;
    if (st.count < 0) return -1;
    long long v = eval(st.count, 0, 0);
    char const* pct = st.percent ? "%" : "";
    if (v < 0) {
        warn("a count of " + std::to_string(v) + std::string(pct) + " is below 0; treating it as 0");
        v = 0;
    }
    if (st.percent && v > 100) {
        warn("a percentage of " + std::to_string(v) + "% is above 100; treating it as 100%");
        v = 100;
    }
    return (int)v;
}

sequence<step_event> machine::run_leaf(compiled_stmt const& st, int top, int count) {
    stmt_loc_ = st.loc;
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
            warn("resize(" + std::to_string(arg[0]) + ", " + std::to_string(arg[1]) + ") has a dimension below 1; leaving grids unchanged");
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
            warn("upscale(" + std::to_string(arg[0]) + ", " + std::to_string(arg[1]) + ") has a factor below 1; treating it as 1");
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
            warn("pad(" + std::to_string(arg[0]) + ") has a margin below 0; treating it as 0");
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
            warn("trim() on an entirely empty stack; leaving grids unchanged");
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
            warn("path() connectivity " + std::to_string(conn) + " is not 4 or 8; treating it as 4");
        run_path(op, conn == 8 ? 8 : 4);
        return;
    }

    case op_kind::bsp: {
        long long min = arg[compiled_op::bsp_min], split = arg[compiled_op::bsp_split];
        if (min < 1) {
            warn("bsp() min " + std::to_string(min) + " is below 1; treating it as 1");
            min = 1;
        }
        if (split < 0 || split > 100) {
            warn("bsp() split " + std::to_string(split) + " is outside 0-100; clamping it");
            split = std::clamp(split, 0LL, 100LL);
        }
        run_bsp(op, min, arg[compiled_op::bsp_max], split);
        return;
    }

    case op_kind::voronoi:
        run_voronoi(op);
        return;

    case op_kind::noise: {
        long long scale = arg[compiled_op::noise_scale], octaves = arg[compiled_op::noise_octaves];
        long long persistence = arg[compiled_op::noise_persistence];
        if (scale < 1) {
            warn("noise() scale " + std::to_string(scale) + " is below 1; treating it as 1");
            scale = 1;
        }
        if (octaves < 1) {
            warn("noise() octaves " + std::to_string(octaves) + " is below 1; treating it as 1");
            octaves = 1;
        }
        long long fit = 0;   // octaves whose spacing scale >> k is at least 1
        while (fit < 63 && (scale >> fit) >= 1) ++fit;
        if (octaves > fit) {
            warn("noise() octaves " + std::to_string(octaves) + " exceeds the " +
                 std::to_string(fit) + " that scale " + std::to_string(scale) +
                 " allows; dropping the rest");
            octaves = fit;
        }
        if (persistence < 0 || persistence > 100) {
            warn("noise() persistence " + std::to_string(persistence) +
                 " is outside 0-100; clamping it");
            persistence = std::clamp(persistence, 0LL, 100LL);
        }
        run_noise(op, arg[compiled_op::noise_lo], arg[compiled_op::noise_hi], scale, octaves,
                  persistence);
        return;
    }
    }
}

void machine::write_field(int id, std::vector<int64_t> const& cells) {
    grid_state& g = grids_[(size_t)id];
    g.front = cells;
    g.back = g.front;
    ++g.version;
}

// bsp(...): recursively split the stack into rectangular leaves.
//
// Draw contract: nodes in pre-order (first child's subtree first), each
// drawing - in this order, and only where the rule leaves a choice - the
// split decision, the side, the cut. So the draws depend on the stack size
// and the arguments alone, never on grid content.
void machine::run_bsp(compiled_op const& op, long long min, long long max, long long split) {
    if (grids_.empty() || grids_[0].rows <= 0 || op.into_grid < 0) {
        warn("bsp() before any resize; leaving grids unchanged");
        return;
    }
    int W = grids_[0].cols, H = grids_[0].rows;
    struct rect { int x, y, w, h; };
    std::vector<rect> leaves, stack{{0, 0, W, H}};
    while (!stack.empty()) {
        rect n = stack.back();
        stack.pop_back();
        long long s = std::max(n.w, n.h);
        bool splits = false;
        if (s >= 2 * min) {
            bool forced = op.has_max && (n.w > max || n.h > max);
            splits = forced || split >= 100 || (split > 0 && (long long)uniform(100) < split);
        }
        if (!splits) {
            leaves.push_back(n);
            continue;
        }
        bool cut_width = n.w > n.h || (n.w == n.h && uniform(2) == 0);
        long long range = s - 2 * min + 1;
        int c = (int)(min + (range > 1 ? (long long)uniform((uint64_t)range) : 0));
        rect a = n, b = n;
        if (cut_width) { a.w = c; b.x += c; b.w -= c; }
        else           { a.h = c; b.y += c; b.h -= c; }
        stack.push_back(b);   // popped after a's whole subtree: pre-order
        stack.push_back(a);
    }

    // ids: row-major order of top-left cells - graph node order
    std::sort(leaves.begin(), leaves.end(), [](rect const& p, rect const& q) {
        return p.y != q.y ? p.y < q.y : p.x < q.x;
    });
    std::vector<int64_t> cells((size_t)W * H);
    for (size_t id = 0; id < leaves.size(); ++id) {
        auto const& l = leaves[id];
        for (int y = l.y; y < l.y + l.h; ++y)
            for (int x = l.x; x < l.x + l.w; ++x)
                cells[(size_t)y * W + x] = (int64_t)id;
    }
    write_field(op.into_grid, cells);
}

// voronoi's neighbour offsets: the first 4 orthogonal, all 8 for chebyshev.
static const int vdx[] = {1, -1, 0, 0, 1, 1, -1, -1};
static const int vdy[] = {0, 0, 1, -1, 1, -1, 1, -1};

// voronoi(...): partition the cells by nearest site, through `within`.
//
// Draw contract: `sites` then `within` row-major, per-cell `cost` under a
// step metric, then ONE raw draw; a site's tie key is uniform_hash(d, site
// index), so two sites resolve every tie between them alike. Either metric's
// claims are a pure function, so the search order is unobservable.
void machine::run_voronoi(compiled_op const& op) {
    if (grids_.empty() || grids_[0].rows <= 0 || op.into_grid < 0) {
        warn("voronoi() before any resize; leaving grids unchanged");
        return;
    }
    int W = grids_[0].cols, H = grids_[0].rows;
    size_t n = (size_t)W * H;

    std::vector<char> is_site(n), within(n);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            is_site[(size_t)y * W + x] = eval(op.sites, x, y) != 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            size_t i = (size_t)y * W + x;
            within[i] = op.within < 0 || eval(op.within, x, y) != 0 || is_site[i];
        }

    std::vector<long long> costv(n, 1);
    if (op.cost >= 0)   // step metrics only
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                size_t i = (size_t)y * W + x;
                if (!within[i]) continue;
                long long cv = eval(op.cost, x, y);
                costv[i] = cv < 1 ? 1 : cv;
            }

    uint64_t d = rng_();

    // sites: 4-connected site regions, indexed in row-major order of their first cell
    std::vector<int> site_of(n, -1);
    int nsites = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!is_site[i] || site_of[i] >= 0) continue;
        std::vector<size_t> stack{i};
        site_of[i] = nsites;
        while (!stack.empty()) {
            size_t c = stack.back();
            stack.pop_back();
            int x = (int)(c % W), y = (int)(c / W);
            for (int k = 0; k < 4; ++k) {
                int nx = x + vdx[k], ny = y + vdy[k];
                if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                size_t j = (size_t)ny * W + nx;
                if (is_site[j] && site_of[j] < 0) { site_of[j] = nsites; stack.push_back(j); }
            }
        }
        ++nsites;
    }
    if (nsites == 0) {
        warn("voronoi() found no site cell; writing every cell empty");
        write_field(op.into_grid, std::vector<int64_t>(n, num_empty));
        return;
    }

    std::vector<int> claim(n, -1);
    for (size_t i = 0; i < n; ++i)
        if (site_of[i] >= 0) claim[i] = site_of[i];
    if (op.w == 0) grow_euclidean(W, H, d, nsites, site_of, within, claim);
    else           grow_steps(W, H, d, op.w, site_of, within, costv, claim);

    // zone ids: row-major order of each zone's first cell - graph node order
    std::vector<int64_t> zone_of(nsites, -1), cells(n, num_empty);
    int64_t nzones = 0;
    for (size_t i = 0; i < n; ++i) {
        if (claim[i] < 0) continue;
        int64_t& z = zone_of[(size_t)claim[i]];
        if (z < 0) z = nzones++;
        cells[i] = z;
    }
    write_field(op.into_grid, cells);
}

// metric=euclidean: zones grow 4-adjacently, each claim the least (squared
// distance to the claiming site's nearest cell, site key, cell) pair - so
// zones are 4-connected, and on open ground the cell-sampled Euclidean
// Voronoi diagram. A queued pair's priority never changes.
void machine::grow_euclidean(int W, int H, uint64_t d, int nsites, std::vector<int> const& site_of,
                             std::vector<char> const& within, std::vector<int>& claim) {
    size_t n = (size_t)W * H;
    // a site's nearest cell to any outside cell has a non-site 4-neighbour
    // (a step from it toward that cell is closer), so only those are measured
    std::vector<std::vector<std::pair<int, int>>> rim((size_t)nsites);
    for (size_t i = 0; i < n; ++i) {
        if (site_of[i] < 0) continue;
        int x = (int)(i % W), y = (int)(i / W);
        for (int k = 0; k < 4; ++k) {
            int nx = x + vdx[k], ny = y + vdy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            if (site_of[(size_t)ny * W + nx] != site_of[i]) {
                rim[(size_t)site_of[i]].push_back({x, y});
                break;
            }
        }
    }
    auto dist2 = [&](int s, int x, int y) {
        long long best = std::numeric_limits<long long>::max();
        for (auto [px, py] : rim[(size_t)s]) {
            long long ddx = x - px, ddy = y - py;
            best = std::min(best, ddx * ddx + ddy * ddy);
        }
        return best;
    };
    std::vector<uint64_t> key((size_t)nsites);
    for (int s = 0; s < nsites; ++s) key[(size_t)s] = uniform_hash(d, (uint64_t)s);

    using entry = std::tuple<long long, uint64_t, int, int>;   // D, key, cell, site
    std::priority_queue<entry, std::vector<entry>, std::greater<entry>> pq;
    auto offer = [&](int c, int s) {   // the unclaimed within neighbours of c, for s
        int x = c % W, y = c / W;
        for (int k = 0; k < 4; ++k) {
            int nx = x + vdx[k], ny = y + vdy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            int j = ny * W + nx;
            if (within[(size_t)j] && claim[(size_t)j] < 0)
                pq.push({dist2(s, nx, ny), key[(size_t)s], j, s});
        }
    };
    for (size_t i = 0; i < n; ++i)
        if (site_of[i] >= 0) offer((int)i, site_of[i]);
    while (!pq.empty()) {
        auto [dd, kk, c, s] = pq.top();
        pq.pop();
        if (claim[(size_t)c] >= 0) continue;   // claimed by a smaller pair
        claim[(size_t)c] = s;
        offer(c, s);
    }
}

// metric=manhattan / chebyshev: multi-source Dijkstra over (distance, key) -
// keys are per site, so the lexicographic minimum propagates along routes and
// one search gives every cell its claim.
void machine::grow_steps(int W, int H, uint64_t d, int ndirs, std::vector<int> const& site_of,
                         std::vector<char> const& within, std::vector<long long> const& costv,
                         std::vector<int>& claim) {
    size_t n = (size_t)W * H;
    const long long inf = std::numeric_limits<long long>::max();
    std::vector<long long> dist(n, inf);
    std::vector<uint64_t>  key(n, 0);
    using entry = std::tuple<long long, uint64_t, int>;
    std::priority_queue<entry, std::vector<entry>, std::greater<entry>> pq;
    for (size_t i = 0; i < n; ++i)
        if (site_of[i] >= 0) {
            dist[i] = 0;
            key[i] = uniform_hash(d, (uint64_t)site_of[i]);
            pq.push({0, key[i], (int)i});
        }
    while (!pq.empty()) {
        auto [dd, kk, i] = pq.top();
        pq.pop();
        if (dd != dist[i] || kk != key[i]) continue;   // stale
        int x = i % W, y = i / W;
        for (int k = 0; k < ndirs; ++k) {
            int nx = x + vdx[k], ny = y + vdy[k];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            int j = ny * W + nx;
            if (!within[j]) continue;
            long long nd = dd + costv[j];
            if (nd < dist[j] || (nd == dist[j] && kk < key[j])) {
                dist[j] = nd;
                key[j] = kk;
                claim[j] = claim[i];
                pq.push({nd, kk, j});
            }
        }
    }
}

// noise(...): fixed-point gradient noise (section 6.13), every step in
// signed 64-bit integers so the output is identical on every platform.
//
// Draw contract: ONE raw draw; every gradient (and rank tie key) is a hash of it.
void machine::run_noise(compiled_op const& op, long long lo, long long hi, long long scale,
                        long long octaves, long long persistence) {
    if (grids_.empty() || grids_[0].rows <= 0 || op.into_grid < 0) {
        warn("noise() before any resize; leaving grids unchanged");
        return;
    }
    int W = grids_[0].cols, H = grids_[0].rows;
    size_t n = (size_t)W * H;
    uint64_t d = rng_();

    if (hi < lo) {
        warn("noise() hi " + std::to_string(hi) + " is below lo " + std::to_string(lo) +
             "; writing lo everywhere");
        write_field(op.into_grid, std::vector<int64_t>(n, lo));
        return;
    }

    static const int64_t grad[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                       {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    auto fade = [](int64_t t) {
        int64_t q = ((t * (6 * t - 983040)) >> 16) + 655360;
        return (((((t * t) >> 16) * t) >> 16) * q) >> 16;
    };
    auto lerp = [](int64_t a, int64_t b, int64_t f) { return a + (((b - a) * f) >> 16); };

    std::vector<int64_t> raw(n, 0);
    int64_t amp = 65536;
    for (long long k = 0; k < octaves; ++k) {
        int64_t s = scale >> k;
        uint64_t dk = uniform_hash(d, (uint64_t)k);
        auto g = [&](int64_t i, int64_t j) {
            return grad[uniform_hash(dk, (uint64_t)i | ((uint64_t)j << 32)) & 7];
        };
        for (int y = 0; y < H; ++y) {
            int64_t fy = ((2 * (int64_t)y + 1) * 65536) / (2 * s);
            int64_t iy = fy >> 16, ty = fy & 65535;
            for (int x = 0; x < W; ++x) {
                int64_t fx = ((2 * (int64_t)x + 1) * 65536) / (2 * s);
                int64_t ix = fx >> 16, tx = fx & 65535;
                auto corner = [&](int64_t cx, int64_t cy) {
                    auto const& gr = g(ix + cx, iy + cy);
                    return gr[0] * (tx - 65536 * cx) + gr[1] * (ty - 65536 * cy);
                };
                int64_t u = fade(tx);
                int64_t v = lerp(lerp(corner(0, 0), corner(1, 0), u),
                                 lerp(corner(0, 1), corner(1, 1), u), fade(ty));
                raw[(size_t)y * W + x] += (v * amp) >> 16;
            }
        }
        amp = amp * persistence / 100;
    }

    std::vector<int64_t> cells(n);
    uint64_t R = (uint64_t)(hi - lo) + 1;
    if (op.w == 1) {   // rank: order by (raw, uniform_hash(d, i), i); position r gets lo + r*R/N
        std::vector<int> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (raw[a] != raw[b]) return raw[a] < raw[b];
            uint64_t ka = uniform_hash(d, (uint64_t)a), kb = uniform_hash(d, (uint64_t)b);
            return ka != kb ? ka < kb : a < b;
        });
        for (size_t r = 0; r < n; ++r)
            cells[(size_t)order[r]] = lo + (int64_t)mul_div(r, R, n);
    } else {           // range: the field's extremes map to lo and hi
        auto [mn, mx] = std::minmax_element(raw.begin(), raw.end());
        int64_t lo_raw = *mn, span = *mx - *mn;
        for (size_t i = 0; i < n; ++i)
            cells[i] = span == 0 ? lo
                     : lo + (int64_t)mul_div((uint64_t)(raw[i] - lo_raw), R - 1, (uint64_t)span);
    }
    write_field(op.into_grid, cells);
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
        warn("path() before any resize; leaving grids unchanged");
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
    auto key = [tie_seed](int i) { return uniform_hash(tie_seed, (uint64_t)i); };

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
        warn("path() found no route from 'from' to 'to'; leaving grids unchanged");
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
