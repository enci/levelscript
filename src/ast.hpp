#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ls {

// Where a construct is written: from (line, col) to (end_line, end_col), the
// end one past its last character. Diagnostics point at the start.
struct source_loc {
    int line{0}, col{0};
    int end_line{0}, end_col{0};
    int mod{0};   // module id within a compile; labels diagnostics
};

// A name as written, and where.
struct ident {
    std::string text;
    source_loc  loc;

    friend bool operator==(ident const& a, std::string_view b) { return a.text == b; }
};

// ── expressions ──────────────────────────────────────────────────────────────

enum class expr_kind {
    int_lit,       // integer literal
    empty_lit,     // '.' — polymorphic empty value (typed from context)
    ident,         // grid read / param / x,y,width,height / tag value or union
    variable,      // '?name' pattern variable; ident holds '?name'
    call,          // built-in function call
    neg, not_,
    add, sub, mul, div_,
    lt, le, gt, ge, eq, ne,
    and_, or_,
    bit_or,        // '|' tag union
    // pattern cells only
    wildcard,      // '*'
    paren,         // '(' expr ')': a computed cell; args [the expr]
    // graph-function arguments only
    selector,      // 'g[m]': ident = the grid, args [the one cell]
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

// A named union: `blocker = wall | door` — a mask alias over the
// tagset's own members (values or earlier unions). Consumes no bit.
struct tag_union {
    ident                    name;
    std::vector<std::string> members;
};

struct tag_decl {
    source_loc             loc;
    ident                  name;
    std::vector<ident>     values;
    std::vector<tag_union> unions;
};

// A graph entry's argument: `nodes=M` or `edges=E`.
struct graph_arg {
    ident    name;
    expr_ptr mask;   // a whitespace-free tag mask
};

struct layer_decl {
    ident                  name;
    std::string            type;   // grid: tagset name, or "number"; graph: its source grid
    bool                   is_graph{false};
    std::vector<graph_arg> args;   // graph only
};

struct layers_decl {
    source_loc              loc;
    std::vector<layer_decl> layers;
};

// ── params ───────────────────────────────────────────────────────────────────

// input:   `name: number = expr` — supplied at runtime, default is mandatory
// derived: `name = expr`         — computed once at startup
struct param_decl {
    ident       name;
    bool        is_derived{false};
    expr_ptr    value;   // the default (input) or the definition (derived)
};

// ── patterns ─────────────────────────────────────────────────────────────────

// A pattern cell is an expression:
//   *            wildcard
//   .  3  ?v     empty_lit, int_lit, variable
//   wall|!door   ident, bit_or, not_ - a whitespace-free mask, where '!' is the
//                complement within the grid's tagset, not boolean not
//   (expr)       paren - a computed cell; `(?v)` reads a variable, `?v` binds it

struct pattern {
    source_loc                         loc;
    ident                              grid;       // "" when is_where
    bool                               is_where{false};
    int                                rows{0}, cols{0};
    std::vector<std::vector<expr_ptr>> cells;      // [row][col]; row widths checked in sema
    // A sized pattern `g(W, H)[c]` repeats its one body cell over W x H.
    // rows/cols above are the body as written; these are the declared shape.
    bool                               sized{false};
    source_loc                         size_loc;
    long long                          size_cols{0}, size_rows{0};
    // A node extent `g(?n)[c]` writes its one body cell over node ?n's
    // region; `extent` holds '?n' ("" when none), `size_loc` its span.
    ident                              extent;
};

// A recursive write-side term: a leaf pattern, an `{ all }` block
// (write every item simultaneously), or an `{ any }` block (pick one weighted
// item). Items nest to any depth.
struct write_term {
    enum class kind { leaf, all, any } what{kind::leaf};
    source_loc              loc;
    int                     weight{1};   // meaningful only as an `{ any }` item
    pattern                 pat;         // leaf
    std::vector<write_term> items;       // all / any
};

// A rule guard: `when (expr)` on the match side, evaluated once per
// candidate. `after` is how many patterns precede it (at least one; 0 in a
// graph pair, after its block) - its place in match-side order.
struct rule_guard {
    source_loc loc;
    expr_ptr   expr;
    int        after{1};
};

// One line of a graph block: `?n` declares a node variable, `?a -- ?b` two
// joined by an edge, `?n[m]` one whose node's value overlaps (or equals) `m`.
struct graph_item {
    enum class kind { node, edge, test } what{kind::node};
    source_loc loc;
    ident      a, b;   // b: edge only
    expr_ptr   test;   // test only: an int_lit or a whitespace-free tag mask
};

// The match side of a graph pair: `plan { ... }`, structure only.
struct graph_block {
    source_loc              loc;   // the name through its '}'
    ident                   graph;
    std::vector<graph_item> items;
};

// One match-write sub-rule: LHS patterns (conjoined across grids at one
// anchor; consecutive patterns before `=>`) and rule guards => a write tree.
// A graph pair matches a graph block instead of patterns (`lhs` is empty and
// every guard's `after` is 0), and writes node extents.
struct rule_pair {
    source_loc              loc;
    std::vector<pattern>    lhs;
    bool                    is_graph{false};
    graph_block             block;    // is_graph only
    std::vector<rule_guard> guards;   // in match-side order
    write_term              rhs;
};

// Every pattern of a write tree, in source order.
template <class F>
void for_each_pattern(write_term const& t, F&& f) {
    if (t.what == write_term::kind::leaf) f(t.pat);
    for (auto const& it : t.items) for_each_pattern(it, f);
}
// Every pattern of a sub-rule, in source order: the match side, then the writes.
template <class F>
void for_each_pattern(rule_pair const& pr, F&& f) {
    for (auto const& p : pr.lhs) f(p);
    for_each_pattern(pr.rhs, f);
}

enum class body_combinator { none, all, any, ordered };

struct rule_decl {
    source_loc             loc;
    ident                  name;               // "" for an inline rule
    std::string            symmetry{"none"};   // validated in sema
    std::vector<long long> rotation_angles;    // besides identity; validated in sema
    body_combinator        body{body_combinator::none};
    std::vector<rule_pair> pairs;
};

// ── program ──────────────────────────────────────────────────────────────────

// The mode of an application: how many applications occur and
// what each one sees. Over a sequence only `once` and `settle` are valid.
//   once        - one application (a single uniform pick) / one iteration
//   scatter     - batch: up to N applications of one snapshot, or P% of it
//   everywhere  - batch: every non-conflicting application of one snapshot
//   grow        - steps, each seeing the last; N steps, or to the fixpoint
//   settle      - batch sweeps (or sequence iterations) until one changes nothing
enum class apply_mode { once, scatter, everywhere, grow, settle };

inline char const* mode_name(apply_mode m) {
    static char const* const names[] = {"once", "scatter", "everywhere", "grow", "settle"};
    return names[(int)m];
}

// Step modes re-snapshot after every application and use no mask; the batch
// modes (scatter, everywhere) and each settle sweep apply one snapshot.
inline bool is_step_mode(apply_mode m) { return m == apply_mode::once || m == apply_mode::grow; }

// One operation-call argument: an expression, positional when `name` is empty.
// `grid` and `enum` parameters read a bare identifier (value->kind ==
// expr_kind::ident) as a name.
struct op_arg {
    source_loc  loc;
    std::string name;   // "" = positional
    expr_ptr    value;
};

struct program_stmt {
    source_loc loc;
    enum class kind { op_call, apply } what{kind::apply};
    // op_call — resolved against the operation table in sema
    std::string         op_name;
    std::vector<op_arg> op_args;
    // apply — a mode, its count where it takes one, and a target
    apply_mode  mode{apply_mode::everywhere};
    expr_ptr    count;                // the parenthesized count; null = none
    bool        count_percent{false}; // scatter(P%)
    ident       rule_name;            // a rule or a sequence; resolved in sema
    std::unique_ptr<rule_decl> inline_rule;   // target written in place; else null
    // optional `when (expr)` guard: boolean, params and width/height
    expr_ptr    guard;
};

// A named statement list, applied by name like a rule.
struct sequence_decl {
    source_loc                loc;
    ident                     name;
    std::vector<program_stmt> stmts;
};

// ── file ─────────────────────────────────────────────────────────────────────

// `use "path"` - resolved by the module loader, not the analyzer.
struct use_decl {
    source_loc  loc;
    std::string path;
};

// A comment whose text starts with '@' (after spaces) - tool metadata, kept
// by the lexer for read_directives(); the language ignores it.
struct directive_comment {
    int         line{0}, col{0}, end_col{0};   // the whole comment, '//' included
    std::string text;            // after the '//'
    bool        trailing{false}; // code before it on its line
    bool        leading{false};  // no code anywhere before it in the file
    int         code_line{0};    // trailing: its own line; else the next line
                                 // with code (0: none)
    int         mod{0};
};

// One module's declarations - or, after loading, the whole closure's,
// merged in canonical order (each declaration's loc.mod says whose it was;
// the block locations and `uses` are the root module's).
struct ast_file {
    std::vector<use_decl>      uses;
    std::vector<tag_decl>      tags;
    layers_decl                layers;
    source_loc                 params_loc;   // the `params { ... }` block
    std::vector<param_decl>    params;
    std::vector<rule_decl>     rules;
    std::vector<sequence_decl> sequences;
    std::vector<directive_comment> directive_comments;   // every module's, after loading
    bool                       has_layers{false};
    bool                       has_params{false};
};

}  // namespace ls
