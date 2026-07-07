#include "sema.hpp"
#include <unordered_set>

namespace ls {

namespace {

// ── pattern transforms (spec §5.6, §10.3) ────────────────────────────────────
//
// Applied at compile time to expand symmetry/rotation variants. The both-axis
// flip IS the 180° rotation, so flip_both doubles as rot180; the per-sub-rule
// dedup below collapses the coincidence (v0.6.2 semantics).

enum class transform { identity, flip_h, flip_v, flip_both, rot90, rot270 };

compiled_pattern transform_pattern(transform k, compiled_pattern const& p) {
    compiled_pattern out = p;
    bool swaps = (k == transform::rot90 || k == transform::rot270);
    out.rows = swaps ? p.cols : p.rows;
    out.cols = swaps ? p.rows : p.cols;
    for (int r = 0; r < out.rows; ++r)
        for (int c = 0; c < out.cols; ++c) {
            int sr = r, sc = c;
            switch (k) {
            case transform::identity:                                     break;
            case transform::flip_h:    sc = p.cols - 1 - c;               break;
            case transform::flip_v:    sr = p.rows - 1 - r;               break;
            case transform::flip_both: sr = p.rows - 1 - r;
                                       sc = p.cols - 1 - c;               break;
            case transform::rot90:     sr = p.rows - 1 - c; sc = r;       break;   // cw
            case transform::rot270:    sr = c; sc = p.cols - 1 - r;       break;   // ccw
            }
            out.cells[r * out.cols + c] = p.cells[sr * p.cols + sc];
        }
    return out;
}

compiled_write_term transform_write_term(transform k, compiled_write_term const& t) {
    compiled_write_term out;
    out.what   = t.what;
    out.weight = t.weight;
    if (t.what == compiled_write_term::kind::leaf)
        out.pattern = transform_pattern(k, t.pattern);
    else
        for (auto const& it : t.items)
            out.items.push_back(transform_write_term(k, it));
    return out;
}

compiled_pair transform_pair(transform k, compiled_pair const& pair) {
    compiled_pair out;
    out.sub_rule_idx = pair.sub_rule_idx;
    for (auto const& pat : pair.lhs)
        out.lhs.push_back(transform_pattern(k, pat));
    out.rhs = transform_write_term(k, pair.rhs);
    return out;
}

bool patterns_equal(compiled_pattern const& a, compiled_pattern const& b) {
    if (a.grid_id != b.grid_id || a.rows != b.rows || a.cols != b.cols) return false;
    for (int i = 0; i < (int)a.cells.size(); ++i)
        if (a.cells[i].what != b.cells[i].what || a.cells[i].val != b.cells[i].val)
            return false;
    return true;
}

// Variant dedup key: two variants are duplicates when every LHS pattern is
// cell-identical (they would produce the same candidates).
bool pair_lhs_equal(compiled_pair const& a, compiled_pair const& b) {
    if (a.lhs.size() != b.lhs.size()) return false;
    for (int i = 0; i < (int)a.lhs.size(); ++i)
        if (!patterns_equal(a.lhs[i], b.lhs[i])) return false;
    return true;
}

struct analyzer {
    ast_file const&  ast;
    compiled&        out;
    diagnostics&     diags;
    std::string_view file;

    void error(source_loc loc, std::string msg) {
        diags.error(file, loc.line, loc.col, std::move(msg));
    }

    // ── symbol tables ────────────────────────────────────────────────────────

    void build_tables() {
        for (auto const& t : ast.tags) {
            if (out.tag_id(t.name) >= 0)
                error(t.loc, "duplicate tag '" + t.name + "'");
            if (t.values.empty())
                error(t.loc, "tagset '" + t.name + "' has no values");
            if ((int)t.values.size() > 30)   // §3: bits 1..30; 0 = empty, 31 reserved
                error(t.loc, "tagset '" + t.name + "' has " +
                      std::to_string(t.values.size()) + " values; the maximum is 30");
            std::vector<std::string> vals;
            for (auto const& v : t.values) {
                for (auto const& seen : vals)
                    if (seen == v) {
                        error(t.loc, "duplicate tag value '" + v + "' in '" + t.name + "'");
                        break;
                    }
                vals.push_back(v);
            }
            out.tag_names.push_back(t.name);
            out.tag_values.push_back(std::move(vals));
        }

        for (auto const& l : ast.layers.layers) {
            if (out.layer_id(l.name) >= 0) {
                error(ast.layers.loc, "duplicate grid '" + l.name + "'");
                continue;
            }
            int tag = -1;
            if (l.type != "number") {
                tag = out.tag_id(l.type);
                if (tag < 0)
                    error(ast.layers.loc, "grid '" + l.name +
                          "' references undeclared tag '" + l.type + "'");
            }
            out.layers.push_back({l.name, tag});
        }
    }

    // ── rules ────────────────────────────────────────────────────────────────

    // Compile one pattern; `exp_rows/exp_cols` enforce the shape constraint
    // (spec §5.4, recursive over the write tree) — 0 means "sets the reference".
    compiled_pattern compile_pattern(pattern const& p, int exp_rows, int exp_cols) {
        compiled_pattern cp;
        cp.grid_id = out.layer_id(p.grid);
        if (cp.grid_id < 0) {
            error(p.loc, "undeclared grid '" + p.grid + "'");
            return cp;
        }
        if (exp_rows > 0 && (p.rows != exp_rows || p.cols != exp_cols))
            error(p.loc, "pattern dimension mismatch: expected " +
                  std::to_string(exp_rows) + "x" + std::to_string(exp_cols) +
                  ", got " + std::to_string(p.rows) + "x" + std::to_string(p.cols));
        int tag = out.layers[cp.grid_id].tag_id;
        cp.is_number = tag < 0;

        if (p.rows == 0 || p.cols == 0) {
            error(p.loc, "empty pattern body");
            return cp;
        }
        for (int r = 0; r < p.rows; ++r)
            if ((int)p.cells[r].size() != p.cols)
                error(p.loc, "inconsistent row widths in pattern: row " +
                      std::to_string(r + 1) + " has " +
                      std::to_string(p.cells[r].size()) + " cells, expected " +
                      std::to_string(p.cols));

        cp.rows = p.rows;
        cp.cols = p.cols;
        cp.cells.assign((size_t)p.rows * p.cols, {});
        for (int r = 0; r < p.rows; ++r) {
            int w = (int)p.cells[r].size() < p.cols ? (int)p.cells[r].size() : p.cols;
            for (int c = 0; c < w; ++c) {
                cell const& in = p.cells[r][c];
                compiled_cell& cc = cp.cells[r * cp.cols + c];
                switch (in.kind) {
                case cell_kind::any:
                    cc.what = compiled_cell::kind::wildcard;
                    break;
                case cell_kind::empty:
                    cc.val = cp.is_number ? num_empty : tag_empty;
                    break;
                case cell_kind::number:
                    if (!cp.is_number)
                        error(in.loc, "integer cell in a tag grid");
                    cc.val = in.number;
                    break;
                case cell_kind::tag: {
                    if (cp.is_number) {
                        error(in.loc, "expected an integer or wildcard in a 'number' grid cell");
                        break;
                    }
                    int vid = out.value_id(tag, in.tag);
                    if (vid < 0)
                        error(in.loc, "unknown tag value '" + in.tag + "' for this grid");
                    else
                        cc.val = tag_bit(vid);
                    break;
                }
                }
            }
        }
        return cp;
    }

    compiled_write_term compile_write_term(write_term const& t,
                                           int exp_rows, int exp_cols) {
        compiled_write_term ct;
        ct.weight = t.weight;
        if (t.what == write_term::kind::leaf) {
            ct.what    = compiled_write_term::kind::leaf;
            ct.pattern = compile_pattern(t.pat, exp_rows, exp_cols);
            return ct;
        }
        ct.what = t.what == write_term::kind::all ? compiled_write_term::kind::all
                                                  : compiled_write_term::kind::any;
        for (auto const& it : t.items)
            ct.items.push_back(compile_write_term(it, exp_rows, exp_cols));
        if (ct.what == compiled_write_term::kind::all)
            check_all_no_overlap(ct, t.loc);
        return ct;
    }

    // Within one { all } write block, two items may not write the same grid at
    // the same cell — a write-write ambiguity (spec §7.3 #31).
    void check_all_no_overlap(compiled_write_term const& all_node, source_loc loc) {
        auto key = [](int g, int r, int c) {
            return ((int64_t)g << 40) | ((int64_t)r << 20) | (int64_t)c;
        };
        std::unordered_set<int64_t> seen;
        for (auto const& item : all_node.items) {
            std::vector<compiled_pattern const*> leaves;
            collect_write_leaves(item, leaves);
            std::vector<int64_t> mine;
            for (auto const* p : leaves) {
                if (p->grid_id < 0) continue;
                for (int r = 0; r < p->rows; ++r)
                    for (int c = 0; c < p->cols; ++c) {
                        if (p->at(r, c).what == compiled_cell::kind::wildcard) continue;
                        int64_t k = key(p->grid_id, r, c);
                        if (seen.count(k)) {
                            error(loc, "same-grid simultaneous write in '{ all }': grid '" +
                                  out.layers[p->grid_id].name +
                                  "' is written twice at the same cell");
                            return;
                        }
                        mine.push_back(k);
                    }
            }
            for (int64_t k : mine) seen.insert(k);
        }
    }

    compiled_pair compile_base_pair(rule_pair const& pr) {
        compiled_pair cp;
        int rows = 0, cols = 0;
        for (auto const& p : pr.lhs) {
            cp.lhs.push_back(compile_pattern(p, rows, cols));
            if (rows == 0) { rows = cp.lhs.back().rows; cols = cp.lhs.back().cols; }
        }
        cp.rhs = compile_write_term(pr.rhs, rows, cols);
        return cp;
    }

    void compile_rules() {
        for (auto const& r : ast.rules) {
            for (auto const& seen : out.rules)
                if (seen.name == r.name)
                    error(r.loc, "duplicate rule '" + r.name + "'");

            // attribute validation (spec §7.3 #7/#18)
            if (r.symmetry != "none" && r.symmetry != "horizontal" &&
                r.symmetry != "vertical" && r.symmetry != "all")
                error(r.loc, "invalid value '" + r.symmetry +
                      "' for attribute 'symmetry'; allowed: none, horizontal, vertical, all");
            for (long long a : r.rotation_angles)
                if (a != 90 && a != 180 && a != 270)
                    error(r.loc, "invalid rotation angle '" + std::to_string(a) +
                          "'; allowed angles are 90, 180, 270");

            // symmetry=all is four variants: identity + H + V + both-axis; the
            // both-axis/180° coincidence dedups below (spec §5.6.1, v0.6.2).
            std::vector<transform> syms = {transform::identity};
            if (r.symmetry == "horizontal") syms.push_back(transform::flip_h);
            else if (r.symmetry == "vertical") syms.push_back(transform::flip_v);
            else if (r.symmetry == "all")
                syms = {transform::identity, transform::flip_h,
                        transform::flip_v, transform::flip_both};

            std::vector<transform> rots = {transform::identity};
            for (long long a : r.rotation_angles) {
                if (a == 90)       rots.push_back(transform::rot90);
                else if (a == 180) rots.push_back(transform::flip_both);   // rot180
                else if (a == 270) rots.push_back(transform::rot270);
            }

            compiled_rule cr;
            cr.name = r.name;
            cr.body = r.body;
            // Expand each sub-rule into its variants; dedup by LHS equality,
            // scoped per sub-rule so same-LHS sub-rules both survive (§10.3).
            for (int bi = 0; bi < (int)r.pairs.size(); ++bi) {
                compiled_pair base = compile_base_pair(r.pairs[bi]);
                base.sub_rule_idx = bi;
                std::vector<compiled_pair> variants;
                for (auto rk : rots)
                    for (auto sk : syms) {
                        compiled_pair cand = transform_pair(sk, transform_pair(rk, base));
                        cand.sub_rule_idx = bi;
                        bool dup = false;
                        for (auto const& v : variants)
                            if (pair_lhs_equal(v, cand)) { dup = true; break; }
                        if (!dup) variants.push_back(std::move(cand));
                    }
                for (auto& v : variants) cr.pairs.push_back(std::move(v));
            }
            out.rules.push_back(std::move(cr));
        }
    }

    // ── reductivity check (LevelScript addition; MGSL §6.9 left this to the
    //    author). A sub-rule *definitely* sustains the fixpoint when no write
    //    that happens on every resolution ({ any } branches don't count)
    //    overwrites one of its own LHS-constrained cells with a value that no
    //    longer matches — the applied anchor then re-matches forever, and
    //    `all(policy=incremental)` never terminates. Conservative: warns only
    //    on the guaranteed case. ─────────────────────────────────────────────

    static void collect_unconditional_leaves(compiled_write_term const& t,
                                             std::vector<compiled_pattern const*>& out) {
        if (t.what == compiled_write_term::kind::leaf) { out.push_back(&t.pattern); return; }
        if (t.what == compiled_write_term::kind::any) return;   // may not happen
        for (auto const& it : t.items) collect_unconditional_leaves(it, out);
    }

    void check_reductive(compiled_rule const& rule, source_loc loc) {
        for (auto const& pair : rule.pairs) {
            std::vector<compiled_pattern const*> writes;
            collect_unconditional_leaves(pair.rhs, writes);

            bool invalidates = false;
            for (auto const& lp : pair.lhs) {
                if (lp.grid_id < 0 || invalidates) continue;
                for (int r = 0; r < lp.rows && !invalidates; ++r)
                    for (int c = 0; c < lp.cols && !invalidates; ++c) {
                        auto const& req = lp.at(r, c);
                        if (req.what == compiled_cell::kind::wildcard) continue;
                        for (auto const* wp : writes) {
                            if (wp->grid_id != lp.grid_id) continue;
                            auto const& w = wp->at(r, c);
                            if (w.what == compiled_cell::kind::wildcard) continue;
                            bool still = lp.is_number ? (w.val == req.val)
                                                      : ((w.val & req.val) != 0);
                            if (!still) { invalidates = true; break; }
                        }
                    }
            }
            if (!invalidates) {
                diags.warning(file, loc.line, loc.col,
                    "'all(policy=incremental)' over rule '" + rule.name +
                    "' may never terminate: a sub-rule's write leaves its own "
                    "match intact, so the fixpoint is unreachable");
                return;
            }
        }
    }

    // ── program (operation table, §6.0) ──────────────────────────────────────

    struct op_spec {
        char const* name;
        op_kind     kind;
        int         arg_count;   // step 1: fixed positional int arguments
    };

    static op_spec const* find_op(std::string const& name) {
        static const op_spec table[] = {
            {"resize", op_kind::resize, 2},
        };
        for (auto const& s : table)
            if (name == s.name) return &s;
        return nullptr;
    }

    void compile_program() {
        for (auto const& s : ast.program.stmts) {
            compiled_stmt cs;
            if (s.what == program_stmt::kind::op_call) {
                cs.what = compiled_stmt::kind::op_call;
                op_spec const* spec = find_op(s.op_name);
                if (!spec) {
                    error(s.loc, "unknown operation '" + s.op_name + "'");
                    continue;
                }
                if ((int)s.op_args.size() != spec->arg_count) {
                    error(s.loc, "'" + s.op_name + "' takes " +
                          std::to_string(spec->arg_count) + " argument(s), got " +
                          std::to_string(s.op_args.size()));
                    continue;
                }
                cs.op.kind = spec->kind;
                if (spec->kind == op_kind::resize) {
                    cs.op.w = (int)s.op_args[0].int_val;
                    cs.op.h = (int)s.op_args[1].int_val;
                    if (cs.op.w <= 0 || cs.op.h <= 0) {
                        error(s.loc, "'resize' dimensions must be positive; got " +
                              std::to_string(cs.op.w) + "x" + std::to_string(cs.op.h));
                        continue;
                    }
                }
            } else {
                cs.what = compiled_stmt::kind::apply;
                cs.strat = s.strat;
                cs.pol = s.pol;
                cs.is_percent = s.is_percent;
                cs.max_count = s.max_count;
                cs.percent = s.percent;
                for (int i = 0; i < (int)out.rules.size(); ++i)
                    if (out.rules[i].name == s.rule_name) { cs.rule_id = i; break; }
                if (cs.rule_id < 0) {
                    error(s.loc, "undeclared rule '" + s.rule_name + "'");
                    continue;
                }
                if (s.strat == strategy::some && !s.is_percent && s.max_count == 0)
                    error(s.loc, "'some(max=0)' applies no matches; did you mean a different strategy?");
                // §7.3 #30: unknown policy value.
                if (s.bad_policy)
                    error(s.loc, "unknown policy '" + s.policy_raw +
                          "'; expected snapshot, incremental, or stabilize");
                // §7.3 #28: invalid count/policy combination.
                if (s.is_percent && s.pol != exec_policy::snapshot)
                    error(s.loc, "'percent' requires the default 'snapshot' policy");
                if (s.strat == strategy::one && s.pol == exec_policy::stabilize)
                    error(s.loc, "'one' with 'policy=stabilize' is contradictory "
                          "(a single application cannot reach a sweep fixpoint)");
                // Reductivity warning (§6.9 — LevelScript addition): an
                // `all(policy=incremental)` fixpoint over a rule whose write
                // never invalidates its own match cannot terminate.
                if (s.strat == strategy::all && s.pol == exec_policy::incremental &&
                    cs.rule_id >= 0)
                    check_reductive(out.rules[cs.rule_id], s.loc);
            }
            out.stmts.push_back(cs);
        }
    }

    void run() {
        build_tables();
        if (diags.has_errors()) return;
        compile_rules();
        if (diags.has_errors()) return;
        if (ast.has_program) compile_program();
    }
};

}  // namespace

bool analyze(ast_file const& ast, compiled& out, diagnostics& diags,
             std::string_view file) {
    analyzer a{ast, out, diags, file};
    a.run();
    return !diags.has_errors();
}

}  // namespace ls
