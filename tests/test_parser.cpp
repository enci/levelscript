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
    CHECK(ast.rules[0].lhs.rows == 3);
    CHECK(ast.rules[0].lhs.cols == 3);
    CHECK(ast.rules[0].lhs.cells[1][1].kind == cell_kind::tag);
    CHECK(ast.rules[0].lhs.cells[1][1].tag == "wall");
    CHECK(ast.rules[0].lhs.cells[0][0].kind == cell_kind::any);
}

TEST_CASE("parser: cell kinds") {
    auto ast = parse_ok("rule r { t[* . 3 wall] => t[* . 3 wall] }");
    auto const& row = ast.rules[0].lhs.cells[0];
    REQUIRE(row.size() == 4);
    CHECK(row[0].kind == cell_kind::any);
    CHECK(row[1].kind == cell_kind::empty);
    CHECK(row[2].kind == cell_kind::number);
    CHECK(row[2].number == 3);
    CHECK(row[3].kind == cell_kind::tag);
}

TEST_CASE("parser: missing arrow is an error") {
    diagnostics diags;
    parse("rule r { g[.] g[floor] }", "test", diags);
    CHECK(diags.has_errors());
}

TEST_CASE("parser: two program blocks is an error") {
    diagnostics diags;
    parse("program { }\nprogram { }", "test", diags);
    CHECK(diags.has_errors());
}
