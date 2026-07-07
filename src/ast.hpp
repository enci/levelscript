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

// A recursive write-side term (spec §5.2): a leaf pattern, an `{ all }` block
// (write every item simultaneously), or an `{ any }` block (pick one weighted
// item). Items nest to any depth.
struct write_term {
    enum class kind { leaf, all, any } what{kind::leaf};
    source_loc              loc;
    int                     weight{1};   // meaningful only as an `{ any }` item
    pattern                 pat;         // leaf
    std::vector<write_term> items;       // all / any
};

// One match-write sub-rule: LHS patterns (conjoined across grids at one
// anchor, `{ all … }` when more than one) => a write tree.
struct rule_pair {
    source_loc           loc;
    std::vector<pattern> lhs;
    write_term           rhs;
};

enum class body_combinator { none, all, any, ordered };

struct rule_decl {
    source_loc             loc;
    std::string            name;
    std::string            symmetry{"none"};   // validated in sema
    std::vector<long long> rotation_angles;    // besides identity; validated in sema
    body_combinator        body{body_combinator::none};
    std::vector<rule_pair> pairs;
};

// ── program ──────────────────────────────────────────────────────────────────

enum class strategy { one, some, all };

// Execution policy (spec §6.7): what each application sees and how conflicts
// are handled. Orthogonal to the count.
enum class exec_policy { snapshot, incremental, stabilize };

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
    // apply — count × policy (§6.7)
    strategy    strat{strategy::all};
    exec_policy pol{exec_policy::snapshot};
    bool        bad_policy{false};    // policy= had an unknown value (§7.3 #30)
    std::string policy_raw;           // its raw text, for the diagnostic
    bool        is_percent{false};    // some(percent=P) instead of max
    int         max_count{0};
    int         percent{0};
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
