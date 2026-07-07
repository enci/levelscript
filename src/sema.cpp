#include "sema.hpp"

namespace ls {

namespace {

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

    compiled_pattern compile_pattern(pattern const& p) {
        compiled_pattern cp;
        cp.grid_id = out.layer_id(p.grid);
        if (cp.grid_id < 0) {
            error(p.loc, "undeclared grid '" + p.grid + "'");
            return cp;
        }
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

    void compile_rules() {
        for (auto const& r : ast.rules) {
            for (auto const& seen : out.rules)
                if (seen.name == r.name)
                    error(r.loc, "duplicate rule '" + r.name + "'");

            compiled_pair pair;
            pair.lhs.push_back(compile_pattern(r.lhs));
            pair.writes.push_back(compile_pattern(r.rhs));

            auto const& l = pair.lhs[0];
            auto const& w = pair.writes[0];
            if (l.rows > 0 && w.rows > 0 && (l.rows != w.rows || l.cols != w.cols))
                error(r.loc, "pattern dimension mismatch: match is " +
                      std::to_string(l.rows) + "x" + std::to_string(l.cols) +
                      " but write is " +
                      std::to_string(w.rows) + "x" + std::to_string(w.cols));

            compiled_rule cr;
            cr.name = r.name;
            cr.pairs.push_back(std::move(pair));
            out.rules.push_back(std::move(cr));
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
                cs.max_count = s.max_count;
                for (int i = 0; i < (int)out.rules.size(); ++i)
                    if (out.rules[i].name == s.rule_name) { cs.rule_id = i; break; }
                if (cs.rule_id < 0) {
                    error(s.loc, "undeclared rule '" + s.rule_name + "'");
                    continue;
                }
                if (s.strat == strategy::some && s.max_count == 0)
                    error(s.loc, "'some(max=0)' applies no matches; did you mean a different strategy?");
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
