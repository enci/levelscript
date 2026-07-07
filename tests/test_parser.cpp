#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace ls;

static ast_file parse_ok(std::string const& src) {
    diagnostics diags;
    auto ast = parse(src, "test", diags);
    INFO(diags.format_all());
    REQUIRE(ast.has_value());
    REQUIRE(!diags.has_errors());
    return *ast;
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

program {
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
    REQUIRE(ast.has_program);
    REQUIRE(ast.program.stmts.size() == 4);
    CHECK(ast.program.stmts[0].what == program_stmt::kind::op_call);
    CHECK(ast.program.stmts[0].op_name == "resize");
    CHECK(ast.program.stmts[1].strat == strategy::all);
    CHECK(ast.program.stmts[2].strat == strategy::one);
    CHECK(ast.program.stmts[3].strat == strategy::some);
    CHECK(ast.program.stmts[3].max_count == 5);
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
    CHECK(lhs.cells[1][1].kind == cell_kind::tag);
    CHECK(lhs.cells[1][1].tag == "wall");
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
    CHECK(row[3].kind == cell_kind::tag);
}

TEST_CASE("parser: missing arrow is an error") {
    CHECK(parse_fails("rule r { g[.] g[floor] }"));
}

TEST_CASE("parser: two program blocks is an error") {
    CHECK(parse_fails("program { }\nprogram { }"));
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

TEST_CASE("parser: single-item combinator block is an error") {
    CHECK(parse_fails("rule r { g[.] => { any g[a] } }"));
    CHECK(parse_fails("rule r { { all g[a] } => g[b] }"));
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
    CHECK(ast.rules[0].pairs[2].lhs[0].cells[0][0].tag == "S");
}

TEST_CASE("parser: single sub-rule under a body combinator is an error") {
    CHECK(parse_fails("rule r { all\n g[.] => g[x]\n}"));
}
