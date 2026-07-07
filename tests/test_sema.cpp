#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>

using ts::compile_result;

static const std::string prelude = R"(
tag geo { wall, floor }
layers {
    level: grid of geo
    tiles: grid of number
}
)";

TEST_CASE("sema: skeleton compiles into tables") {
    compile_result r(prelude + R"(
rule fill { level[.] => level[floor] }
program { resize(4, 3)  all fill }
)");
    INFO(r.diags.format_all());
    REQUIRE(r.ok);
    CHECK(r.prog.layer_id("level") == 0);
    CHECK(r.prog.layer_id("tiles") == 1);
    CHECK(r.prog.layers[1].tag_id == -1);
    CHECK(r.prog.value_id(0, "floor") == 1);
    REQUIRE(r.prog.rules.size() == 1);
    REQUIRE(r.prog.stmts.size() == 2);
}

TEST_CASE("sema: duplicate declarations") {
    CHECK(compile_result("tag a { x }\ntag a { y }\nprogram { }").has_error("duplicate tag"));
    CHECK(compile_result("tag a { x, x }\nprogram { }").has_error("duplicate tag value"));
    CHECK(compile_result(prelude +
        "rule r { level[.] => level[wall] }\nrule r { level[.] => level[wall] }\nprogram { }")
        .has_error("duplicate rule"));
}

TEST_CASE("sema: unknown references") {
    CHECK(compile_result("layers { g: grid of nope }\nprogram { }")
          .has_error("undeclared tag"));
    CHECK(compile_result(prelude + "rule r { nope[.] => nope[.] }\nprogram { }")
          .has_error("undeclared grid"));
    CHECK(compile_result(prelude + "rule r { level[lava] => level[wall] }\nprogram { }")
          .has_error("unknown tag value"));
    CHECK(compile_result(prelude + "rule r { level[.] => level[wall] }\nprogram { all nope }")
          .has_error("undeclared rule"));
}

TEST_CASE("sema: cell/grid type mismatches") {
    CHECK(compile_result(prelude + "rule r { level[3] => level[wall] }\nprogram { }")
          .has_error("integer cell in a tag grid"));
    CHECK(compile_result(prelude + "rule r { tiles[wall] => tiles[1] }\nprogram { }")
          .has_error("number' grid cell"));
}

TEST_CASE("sema: shape mismatch") {
    CHECK(compile_result(prelude + "rule r { level[. .] => level[wall] }\nprogram { }")
          .has_error("dimension mismatch"));
}

TEST_CASE("sema: inconsistent row widths") {
    compile_result r(prelude + R"(
rule r {
    level[
        . .
        . ]
    =>
    level[
        wall wall
        wall ]
}
program { }
)");
    CHECK(r.has_error("inconsistent row widths"));
}

TEST_CASE("sema: operation table") {
    CHECK(compile_result(prelude + "program { warp(3, 3) }").has_error("unknown operation"));
    CHECK(compile_result(prelude + "program { resize(3) }").has_error("takes 2 argument(s)"));
    CHECK(compile_result(prelude + "program { resize(0, 5) }").has_error("must be positive"));
}

TEST_CASE("sema: some(max=0) is rejected") {
    CHECK(compile_result(prelude +
        "rule r { level[.] => level[wall] }\nprogram { some(max=0) r }")
        .has_error("some(max=0)"));
}

TEST_CASE("sema: tagset over the 30-value cap") {
    std::string big = "tag t { ";
    for (int i = 0; i < 31; ++i) big += "v" + std::to_string(i) + ", ";
    big += "}\nprogram { }";
    CHECK(compile_result(big).has_error("maximum is 30"));
}
