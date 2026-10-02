#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace ls;

static ast_file parse_ok(std::string const& src) {
    diagnostics diags;
    auto ast = parse(src, "test", diags);
    INFO(diags.format_all());
    REQUIRE(ast.has_value());
    REQUIRE(!diags.has_errors());
    return std::move(*ast);
}

static bool parse_fails(std::string const& src) {
    diagnostics diags;
    parse(src, "test", diags);
    return diags.has_errors();
}

static const std::string skeleton = R"(
tag geo { wall, floor }

layers {
    level: grid of geo
    tiles: grid of number
}

rule fill {
    level[.]
    =>
    level[floor]
}

sequence main {
    resize(8, 4)
    all fill
    one fill
    some(max=5) fill
}
)";

TEST_CASE("parser: full skeleton file") {
    auto ast = parse_ok(skeleton);
    REQUIRE(ast.tags.size() == 1);
    CHECK(ast.tags[0].values.size() == 2);
    REQUIRE(ast.layers.layers.size() == 2);
    CHECK(ast.layers.layers[1].type == "number");
    REQUIRE(ast.rules.size() == 1);
    REQUIRE(ast.sequences.size() == 1);
    REQUIRE(ast.sequences.back().stmts.size() == 4);
    CHECK(ast.sequences.back().stmts[0].what == program_stmt::kind::op_call);
    CHECK(ast.sequences.back().stmts[0].op_name == "resize");
    CHECK(ast.sequences.back().stmts[1].strat == strategy::all);
    CHECK(ast.sequences.back().stmts[2].strat == strategy::one);
    CHECK(ast.sequences.back().stmts[3].strat == strategy::some);
    CHECK(ast.sequences.back().stmts[3].max_count == 5);
}

TEST_CASE("parser: multi-row pattern rows and cols") {
    auto ast = parse_ok(R"(
rule r {
    g[
        * * *
        * wall *
        * * * ]
    =>
    g[
        * * *
        * floor *
        * * * ]
}
)");
    REQUIRE(ast.rules.size() == 1);
    REQUIRE(ast.rules[0].pairs.size() == 1);
    auto const& lhs = ast.rules[0].pairs[0].lhs[0];
    CHECK(lhs.rows == 3);
    CHECK(lhs.cols == 3);
    CHECK(lhs.cells[1][1].kind == cell_kind::tag_mask);
    CHECK(lhs.cells[1][1].atoms[0].name == "wall");
    CHECK(lhs.cells[0][0].kind == cell_kind::any);
}

TEST_CASE("parser: cell kinds") {
    auto ast = parse_ok("rule r { t[* . 3 wall] => t[* . 3 wall] }");
    auto const& row = ast.rules[0].pairs[0].lhs[0].cells[0];
    REQUIRE(row.size() == 4);
    CHECK(row[0].kind == cell_kind::any);
    CHECK(row[1].kind == cell_kind::empty);
    CHECK(row[2].kind == cell_kind::number);
    CHECK(row[2].number == 3);
    CHECK(row[3].kind == cell_kind::tag_mask);
}

TEST_CASE("parser: missing arrow is an error") {
    CHECK(parse_fails("rule r { g[.] g[floor] }"));
}

TEST_CASE("parser: a removed 'program' block says how to migrate (0.7)") {
    diagnostics diags;
    auto ast = parse("program {\n    resize(2, 2)\n}\n", "test", diags);
    REQUIRE(diags.all.size() == 1);
    CHECK(diags.all[0].message.find("write 'sequence main { ... }'") != std::string::npos);
    // recovered as `sequence main`, so the rest of the file is still checked
    REQUIRE(ast->sequences.size() == 1);
    CHECK(ast->sequences[0].name == "main");
    CHECK(ast->sequences[0].stmts.size() == 1);
}

TEST_CASE("parser: use declarations head the file (2.6)") {
    auto ast = parse_ok("use \"schema.ls\"\nuse \"lib/rules.ls\"\ntag t { a }\n");
    REQUIRE(ast.uses.size() == 2);
    CHECK(ast.uses[0].path == "schema.ls");
    CHECK(ast.uses[1].path == "lib/rules.ls");
    CHECK(parse_fails("tag t { a }\nuse \"schema.ls\"\n"));   // after a declaration
    CHECK(parse_fails("use schema\n"));                          // a path is a string
    CHECK(parse_fails("use \"schema.ls\n"));                     // unterminated
}

// ── step 2: match blocks, write trees, attributes, body combinators ──────────

TEST_CASE("parser: { all } match side") {
    auto ast = parse_ok(R"(
rule place {
    { all
      algo[F]
      enemies[.]
    }
    =>
    enemies[goblin]
}
)");
    auto const& pair = ast.rules[0].pairs[0];
    REQUIRE(pair.lhs.size() == 2);
    CHECK(pair.lhs[0].grid == "algo");
    CHECK(pair.lhs[1].grid == "enemies");
    CHECK(pair.rhs.what == write_term::kind::leaf);
}

TEST_CASE("parser: { any } write side with weights") {
    auto ast = parse_ok(R"(
rule reward {
    algo[S]
    =>
    { any
      (weight=8) items[.]
      items[chest]
    }
}
)");
    auto const& rhs = ast.rules[0].pairs[0].rhs;
    REQUIRE(rhs.what == write_term::kind::any);
    REQUIRE(rhs.items.size() == 2);
    CHECK(rhs.items[0].weight == 8);
    CHECK(rhs.items[1].weight == 1);   // default
}

TEST_CASE("parser: nested all-of-any write tree") {
    auto ast = parse_ok(R"(
rule decorate {
    algo[F]
    =>
    { all
      level[floor]
      { any
        (weight=3) items[.]
        items[chest]
      }
    }
}
)");
    auto const& rhs = ast.rules[0].pairs[0].rhs;
    REQUIRE(rhs.what == write_term::kind::all);
    REQUIRE(rhs.items.size() == 2);
    CHECK(rhs.items[0].what == write_term::kind::leaf);
    CHECK(rhs.items[1].what == write_term::kind::any);
    CHECK(rhs.items[1].items.size() == 2);
}

TEST_CASE("parser: weight inside { all } is an error") {
    CHECK(parse_fails(R"(
rule r {
    g[.]
    =>
    { all
      (weight=2) g[a]
      h[b]
    }
}
)"));
}

TEST_CASE("parser: { any } on the match side is an error") {
    CHECK(parse_fails("rule r { { any g[a] h[b] } => g[c] }"));
}

TEST_CASE("parser: single-item combinator blocks are allowed") {
    auto ast = parse_ok("rule r { { all g[a] } => { any g[b] } }");
    CHECK(ast.rules[0].pairs[0].lhs.size() == 1);
    CHECK(ast.rules[0].pairs[0].rhs.items.size() == 1);
}

TEST_CASE("parser: rule attributes") {
    auto ast = parse_ok(R"(
rule a(symmetry=horizontal) { g[.] => g[x] }
rule b(rotation=all) { g[.] => g[x] }
rule c(symmetry=all, rotation=180) { g[.] => g[x] }
rule d(rotation={90, 270}) { g[.] => g[x] }
)");
    REQUIRE(ast.rules.size() == 4);
    CHECK(ast.rules[0].symmetry == "horizontal");
    CHECK(ast.rules[1].rotation_angles == std::vector<long long>{90, 180, 270});
    CHECK(ast.rules[2].symmetry == "all");
    CHECK(ast.rules[2].rotation_angles == std::vector<long long>{180});
    CHECK(ast.rules[3].rotation_angles == std::vector<long long>{90, 270});
}

TEST_CASE("parser: body-level combinator with sub-rules") {
    auto ast = parse_ok(R"(
rule fill_geo { all
    algo[W] => level[wall]
    algo[F] => level[floor]
    algo[S] => level[floor]
}
)");
    REQUIRE(ast.rules.size() == 1);
    CHECK(ast.rules[0].body == body_combinator::all);
    REQUIRE(ast.rules[0].pairs.size() == 3);
    CHECK(ast.rules[0].pairs[2].lhs[0].cells[0][0].atoms[0].name == "S");
}

TEST_CASE("parser: commas separate body pairs like newlines (list_sep)") {
    auto ast = parse_ok("rule r { all\n g[a] => g[b], g[b] => g[c]\n}");
    CHECK(ast.rules[0].pairs.size() == 2);
}

TEST_CASE("parser: a single sub-rule under a body combinator is allowed") {
    auto ast = parse_ok("rule r { all\n g[.] => g[x]\n}");
    CHECK(ast.rules[0].pairs.size() == 1);
}

// ── step 3: policies, percent, ordered ────────────────────────────────────────

TEST_CASE("parser: policy arguments") {
    auto ast = parse_ok(R"(
sequence main {
    all fill
    all(policy=stabilize) smooth
    one(policy=incremental) start
    some(max=200, policy=incremental) walk
    some(percent=50) carve
}
)");
    auto const& s = ast.sequences.back().stmts;
    REQUIRE(s.size() == 5);
    CHECK(s[0].pol == exec_policy::snapshot);   // default
    CHECK(s[1].pol == exec_policy::stabilize);
    CHECK(s[2].pol == exec_policy::incremental);
    CHECK(s[3].pol == exec_policy::incremental);
    CHECK(s[3].max_count == 200);
    CHECK(s[4].is_percent);
    CHECK(s[4].percent == 50);
    CHECK(s[4].pol == exec_policy::snapshot);
}

TEST_CASE("parser: unknown policy is recorded for sema") {
    diagnostics diags;
    auto ast = parse("sequence main { all(policy=ranked) r }", "test", diags);
    REQUIRE(ast.has_value());
    CHECK(!diags.has_errors());   // parse accepts; sema rejects (#30)
    CHECK(ast->sequences.back().stmts[0].bad_policy);
    CHECK(ast->sequences.back().stmts[0].policy_raw == "ranked");
}

TEST_CASE("parser: ordered body combinator") {
    auto ast = parse_ok(R"(
rule grow { ordered
    g[a] => g[b]
    g[.] => g[a]
}
)");
    CHECK(ast.rules[0].body == body_combinator::ordered);
}

TEST_CASE("parser: ordered on a match or write side is an error") {
    CHECK(parse_fails("rule r { { ordered g[a] h[b] } => g[c] }"));
    CHECK(parse_fails("rule r { g[.] => { ordered g[a] g[b] } }"));
}

// ── step 4: expressions, mask cells, where, params, when ─────────────────────

TEST_CASE("parser: mask cells — unions and complements are whitespace-free") {
    auto ast = parse_ok("rule r { g[wall|door !wall] => g[floor floor] }");
    auto const& row = ast.rules[0].pairs[0].lhs[0].cells[0];
    REQUIRE(row.size() == 2);
    REQUIRE(row[0].atoms.size() == 2);
    CHECK(row[0].atoms[0].name == "wall");
    CHECK(row[0].atoms[1].name == "door");
    CHECK(row[1].atoms[0].negate);
    // spaced 'a | b' is NOT one cell — it fails to parse as cells
    CHECK(parse_fails("rule r { g[wall | door] => g[floor] }"));
}

TEST_CASE("parser: expression cells and precedence") {
    auto ast = parse_ok("rule r { g[ (tiles + 2 * 3 > 7) ] => g[ (if(d > 1, wall, floor)) ] }");
    auto const& c = ast.rules[0].pairs[0].lhs[0].cells[0][0];
    REQUIRE(c.kind == cell_kind::expr_cell);
    REQUIRE(c.value->kind == expr_kind::gt);
    auto const& add = *c.value->args[0];
    REQUIRE(add.kind == expr_kind::add);            // + binds looser than *
    CHECK(add.args[1]->kind == expr_kind::mul);
    auto const& w = ast.rules[0].pairs[0].rhs.pat.cells[0][0];
    REQUIRE(w.kind == cell_kind::expr_cell);
    CHECK(w.value->kind == expr_kind::call);
    CHECK(w.value->ident == "if");
    CHECK(w.value->args.size() == 3);
}

TEST_CASE("parser: where pseudo-layer") {
    auto ast = parse_ok(R"(
rule edge {
    { all
      level[floor]
      where[ (x == 0) ]
    }
    =>
    level[wall]
}
)");
    auto const& pair = ast.rules[0].pairs[0];
    REQUIRE(pair.lhs.size() == 2);
    CHECK(pair.lhs[1].is_where);
    CHECK(pair.lhs[1].cells[0][0].kind == cell_kind::expr_cell);
}

TEST_CASE("parser: params block") {
    auto ast = parse_ok(R"(
params {
    difficulty: number = 3
    rooms: number = random(2, 5)
    budget = difficulty * 10
}
)");
    REQUIRE(ast.params.size() == 3);
    CHECK(!ast.params[0].is_derived);
    CHECK(ast.params[0].value->kind == expr_kind::int_lit);
    CHECK(!ast.params[1].is_derived);
    CHECK(ast.params[2].is_derived);
    CHECK(ast.params[2].value->kind == expr_kind::mul);
    CHECK(parse_fails("params { a: number = 1 }\nparams { b: number = 2 }"));
}

TEST_CASE("parser: when guards") {
    auto ast = parse_ok(R"(
sequence main {
    resize(4, 4)  when (difficulty > 3)
    all fill      when (style == 0)
}
)");
    REQUIRE(ast.sequences.back().stmts.size() == 2);
    CHECK(ast.sequences.back().stmts[0].guard != nullptr);
    CHECK(ast.sequences.back().stmts[0].guard->kind == expr_kind::gt);
    CHECK(ast.sequences.back().stmts[1].guard->kind == expr_kind::eq);
}

TEST_CASE("parser: named unions in the tag block") {
    auto ast = parse_ok("tag geo { wall, door, floor, blocker = wall | door }");
    REQUIRE(ast.tags[0].values.size() == 3);
    REQUIRE(ast.tags[0].unions.size() == 1);
    CHECK(ast.tags[0].unions[0].name == "blocker");
    CHECK(ast.tags[0].unions[0].members == std::vector<std::string>{"wall", "door"});
}

TEST_CASE("parser: contextual names in their grammar slots (spec section 2.4)") {
    auto ast = parse_ok(R"(
rule a(symmetry=none, rotation=none) { g[.] => g[x] }
rule b(rotation=all, symmetry=vertical) { g[.] => g[x] }
rule c { g[ (max(g, 1)) ] => g[x] }
sequence main {
    some(max=3) a
    mirror(horizontal)
    mirror(vertical)
}
)");
    REQUIRE(ast.rules.size() == 3);
    CHECK(ast.rules[0].symmetry == "none");
    CHECK(ast.rules[0].rotation_angles.empty());
    CHECK(ast.rules[1].symmetry == "vertical");
    CHECK(ast.rules[1].rotation_angles == std::vector<long long>{90, 180, 270});
    auto const& s = ast.sequences.back().stmts;
    REQUIRE(s.size() == 3);
    CHECK(s[0].max_count == 3);
    REQUIRE(s[1].op_args.size() == 1);
    CHECK(s[1].op_args[0].ident == "horizontal");
    CHECK(s[2].op_args[0].ident == "vertical");
}

TEST_CASE("parser: contextual names are legal declaration names (spec section 2.4)") {
    auto ast = parse_ok(R"(
tag none { horizontal, vertical, symmetry, rotation }
layers { none: grid of none }
params { rotation: number = 1 }
)");
    REQUIRE(ast.tags.size() == 1);
    CHECK(ast.tags[0].name == "none");
}

TEST_CASE("parser: an unclosed pattern reports the missing ']' once") {
    diagnostics diags;
    parse("rule r {\n    g[.\n    =>\n    g[a]\n}\n", "test", diags);
    REQUIRE(diags.all.size() == 1);
    CHECK(diags.all[0].message.find("expected ']'") != std::string::npos);
}

TEST_CASE("parser: an unknown attribute is parsed whole — one diagnostic (section 5.1)") {
    for (char const* attrs : {"colour=red", "colour=3", "colour=all", "colour={90, 180}"}) {
        diagnostics diags;
        parse(std::string("rule r(") + attrs + ") { g[.] => g[a] }\n", "test", diags);
        INFO(attrs);
        REQUIRE(diags.all.size() == 1);
        CHECK(diags.all[0].message.find("unknown rule attribute 'colour'") != std::string::npos);
    }
}
