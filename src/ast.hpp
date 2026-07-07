#pragma once
#include <string>
#include <vector>

namespace ls {

struct source_loc {
    int line{0}, col{0};
};

// ── declarations ─────────────────────────────────────────────────────────────

struct tag_decl {
    source_loc               loc;
    std::string              name;
    std::vector<std::string> values;
};

struct layer_decl {
    std::string name;
    std::string type;   // tagset name, or "number"
};

struct layers_decl {
    source_loc              loc;
    std::vector<layer_decl> layers;
};

// ── patterns (step 1: constant cells only) ───────────────────────────────────

enum class cell_kind { any, empty, number, tag };

struct cell {
    cell_kind   kind{cell_kind::any};
    source_loc  loc;
    long long   number{0};   // cell_kind::number
    std::string tag;         // cell_kind::tag
};

struct pattern {
    source_loc                     loc;
    std::string                    grid;
    int                            rows{0}, cols{0};
    std::vector<std::vector<cell>> cells;   // [row][col]; row widths checked in sema
};

struct rule_decl {
    source_loc  loc;
    std::string name;
    pattern     lhs;
    pattern     rhs;
};

// ── program ──────────────────────────────────────────────────────────────────

enum class strategy { one, some, all };

struct op_arg {                 // step 1: integer arguments only
    source_loc loc;
    long long  int_val{0};
};

struct program_stmt {
    source_loc loc;
    enum class kind { op_call, apply } what{kind::apply};
    // op_call — resolved against the operation table in sema
    std::string         op_name;
    std::vector<op_arg> op_args;
    // apply — count under the (implicit, step 1) snapshot policy
    strategy    strat{strategy::all};
    int         max_count{0};   // some(max=N)
    std::string rule_name;
};

struct program_decl {
    source_loc                loc;
    std::vector<program_stmt> stmts;
};

// ── file ─────────────────────────────────────────────────────────────────────

struct ast_file {
    std::vector<tag_decl>  tags;
    layers_decl            layers;
    std::vector<rule_decl> rules;
    program_decl           program;
    bool                   has_layers{false};
    bool                   has_program{false};
};

}  // namespace ls
