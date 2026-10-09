#pragma once
#include <memory>
#include <string>
#include <vector>

namespace ls {

struct source_loc {
    int line{0}, col{0};
    int mod{0};   // module id within a compile (section 2.6); labels diagnostics
};

// ── expressions (spec section 5.8) ──────────────────────────────────────────────────

enum class expr_kind {
    int_lit,       // integer literal
    empty_lit,     // '.' — polymorphic empty value (typed from context)
    ident,         // grid read / param / x,y,width,height / tag value or union
    call,          // built-in function call
    neg, not_,
    add, sub, mul, div_,
    lt, le, gt, ge, eq, ne,
    and_, or_,
    bit_or,        // '|' tag union
};

struct expr;
using expr_ptr = std::unique_ptr<expr>;

struct expr {
    expr_kind             kind;
    source_loc            loc;
    long long             int_val{0};   // int_lit
    std::string           ident;        // ident / call name
    std::vector<expr_ptr> args;         // unary [a]; binary [a,b]; call args
};

// ── declarations ─────────────────────────────────────────────────────────────

// A named union (spec section 3): `blocker = wall | door` — a mask alias over the
// tagset's own members (values or earlier unions). Consumes no bit.
struct tag_union {
    source_loc               loc;
    std::string              name;
    std::vector<std::string> members;
};

struct tag_value {
    source_loc  loc;
    std::string name;
};

struct tag_decl {
    source_loc               loc;
    std::string              name;
    std::vector<tag_value>   values;
    std::vector<tag_union>   unions;
};

struct layer_decl {
    source_loc  loc;
    std::string name;
    std::string type;   // tagset name, or "number"
};

struct layers_decl {
    source_loc              loc;
    std::vector<layer_decl> layers;
};

// ── params (spec section 4.2) ───────────────────────────────────────────────────────

// input:   `name: number = expr` — supplied at runtime, default is mandatory
// derived: `name = expr`         — computed once at startup
struct param_decl {
    source_loc  loc;
    std::string name;
    bool        is_derived{false};
    expr_ptr    value;   // the default (input) or the definition (derived)
};

// ── patterns ─────────────────────────────────────────────────────────────────

// One whitespace-free mask atom: `wall` or `!wall` (complement, LHS only).
struct mask_atom {
    source_loc  loc;
    std::string name;
    bool        negate{false};
};

enum class cell_kind { any, empty, number, tag_mask, expr_cell };

struct cell {
    cell_kind              kind{cell_kind::any};
    source_loc             loc;
    long long              number{0};   // number
    std::vector<mask_atom> atoms;       // tag_mask: atoms OR'd together
    expr_ptr               value;       // expr_cell: '(' expr ')'
};

struct pattern {
    source_loc                     loc;
    source_loc                     grid_loc;
    std::string                    grid;       // "" when is_where
    bool                           is_where{false};
    int                            rows{0}, cols{0};
    std::vector<std::vector<cell>> cells;   // [row][col]; row widths checked in sema
};

// A recursive write-side term (spec section 5.2): a leaf pattern, an `{ all }` block
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
// anchor; consecutive patterns before `=>`) => a write tree.
struct rule_pair {
    source_loc           loc;
    std::vector<pattern> lhs;
    write_term           rhs;
};

enum class body_combinator { none, all, any, ordered };

struct rule_decl {
    source_loc             loc;
    source_loc             name_loc;
    std::string            name;
    std::string            symmetry{"none"};   // validated in sema
    std::vector<long long> rotation_angles;    // besides identity; validated in sema
    body_combinator        body{body_combinator::none};
    std::vector<rule_pair> pairs;
};

// ── program ──────────────────────────────────────────────────────────────────

enum class strategy { one, some, all };

// Execution policy (spec section 6.7): what each application sees and how conflicts
// are handled. Orthogonal to the count.
enum class exec_policy { snapshot, incremental, stabilize };

// One operation-call argument (spec section 6.0). Positional when `name` is empty;
// the value is an integer, a bare identifier (grid, tag value, enum word), or
// a parenthesized expression.
struct op_arg {
    source_loc  loc;
    std::string name;   // "" = positional
    enum class kind { int_, ident, expr } what{kind::int_};
    long long   int_val{0};
    std::string ident;
    expr_ptr    value;
};

struct program_stmt {
    source_loc loc;
    enum class kind { op_call, apply } what{kind::apply};
    // op_call — resolved against the operation table in sema
    std::string         op_name;
    std::vector<op_arg> op_args;
    // apply — count × policy (section 6.7)
    strategy    strat{strategy::all};
    exec_policy pol{exec_policy::snapshot};
    bool        policy_given{false};  // policy= written at all (section 7.3, check 37 on sequences)
    bool        bad_policy{false};    // policy= had an unknown value (section 7.3, check 30)
    std::string policy_raw;           // its raw text, for the diagnostic
    bool        is_percent{false};    // some(percent=P) instead of max
    int         max_count{0};
    int         percent{0};
    std::string rule_name;            // a rule or a sequence (section 6.10); resolved in sema
    source_loc  rule_name_loc;
    // optional `when (expr)` guard (section 6): boolean, params only, evaluated once
    expr_ptr    guard;
};

// A named statement list, applied by name like a rule (section 6.10).
struct sequence_decl {
    source_loc                loc;
    source_loc                name_loc;
    std::string               name;
    std::vector<program_stmt> stmts;
};

// ── file ─────────────────────────────────────────────────────────────────────

// `use "path"` (section 2.6) - resolved by the module loader, not the analyzer.
struct use_decl {
    source_loc  loc;
    std::string path;
};

// One module's declarations - or, after loading, the whole closure's,
// merged in canonical order (each declaration's loc.mod says whose it was).
struct ast_file {
    std::vector<use_decl>   uses;
    std::vector<tag_decl>   tags;
    layers_decl             layers;
    std::vector<param_decl> params;
    std::vector<rule_decl>  rules;
    std::vector<sequence_decl> sequences;
    bool                    has_layers{false};
    bool                    has_params{false};
};

}  // namespace ls
