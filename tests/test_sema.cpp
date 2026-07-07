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

// ── step 2: write trees, attributes, variant expansion ────────────────────────

TEST_CASE("sema: same-grid simultaneous write in { all } is rejected") {
    CHECK(compile_result(prelude + R"(
rule r {
    level[.]
    =>
    { all
      level[wall]
      level[floor]
    }
}
program { }
)").has_error("written twice at the same cell"));
}

TEST_CASE("sema: different grids at one cell in { all } are fine") {
    compile_result r(prelude + R"(
rule r {
    level[.]
    =>
    { all
      level[wall]
      tiles[1]
    }
}
program { }
)");
    INFO(r.diags.format_all());
    CHECK(r.ok);
}

TEST_CASE("sema: nested write leaf shape mismatch is rejected") {
    CHECK(compile_result(prelude + R"(
rule r {
    level[. .]
    =>
    { any
      level[wall wall]
      level[floor]
    }
}
program { }
)").has_error("dimension mismatch"));
}

TEST_CASE("sema: invalid attribute values") {
    CHECK(compile_result(prelude +
        "rule r(symmetry=diagonal) { level[.] => level[wall] }\nprogram { }")
        .has_error("invalid value 'diagonal'"));
    CHECK(compile_result(prelude +
        "rule r(rotation=45) { level[.] => level[wall] }\nprogram { }")
        .has_error("invalid rotation angle"));
}

static const std::string quad = R"(
tag t4 { a, b, c, d }
layers { q: grid of t4 }
)";

TEST_CASE("sema: variant expansion counts") {
    SECTION("rotation=all on an asymmetric 1x2 gives 4 variants") {
        compile_result r(prelude +
            "rule r(rotation=all) { level[wall floor] => level[floor wall] }\nprogram { }");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 4);
    }
    SECTION("symmetry=all on a 1x2 gives 2 (vertical flip is identity)") {
        compile_result r(prelude +
            "rule r(symmetry=all) { level[wall floor] => level[floor wall] }\nprogram { }");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 2);
    }
    SECTION("symmetry=all + rotation=180 dedups the both-axis/180 coincidence") {
        compile_result r(quad + R"(
rule r(symmetry=all, rotation=180) {
    q[
        a b
        c d ]
    =>
    q[
        d c
        b a ]
}
program { }
)");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 4);
    }
    SECTION("symmetry=all + rotation=all is the 8-variant D4") {
        compile_result r(quad + R"(
rule r(symmetry=all, rotation=all) {
    q[
        a b
        c d ]
    =>
    q[
        d c
        b a ]
}
program { }
)");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 8);
    }
    SECTION("a symmetric pattern collapses to 1 variant") {
        compile_result r(prelude +
            "rule r(symmetry=all, rotation=all) { level[wall] => level[floor] }\nprogram { }");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 1);
    }
}

TEST_CASE("sema: sub-rule indices survive expansion") {
    compile_result r(prelude + R"(
rule fill_geo { all
    level[wall]  => tiles[2]
    level[floor] => tiles[1]
}
program { }
)");
    REQUIRE(r.ok);
    REQUIRE(r.prog.rules[0].pairs.size() == 2);
    CHECK(r.prog.rules[0].pairs[0].sub_rule_idx == 0);
    CHECK(r.prog.rules[0].pairs[1].sub_rule_idx == 1);
}

// ── step 3: count × policy validity, reductivity ──────────────────────────────

static const std::string rfill =
    "rule fill { level[.] => level[floor] }\n";

TEST_CASE("sema: count/policy combinations") {
    CHECK(compile_result(prelude + rfill + "program { all(policy=ranked) fill }")
          .has_error("unknown policy 'ranked'"));
    CHECK(compile_result(prelude + rfill +
          "program { some(percent=50, policy=incremental) fill }")
          .has_error("'percent' requires the default 'snapshot' policy"));
    CHECK(compile_result(prelude + rfill + "program { one(policy=stabilize) fill }")
          .has_error("contradictory"));
    compile_result ok(prelude + rfill + R"(
program {
    resize(4, 4)
    some(max=3, policy=stabilize) fill
    all(policy=incremental) fill
}
)");
    INFO(ok.diags.format_all());
    CHECK(ok.ok);
}

static bool has_warning(compile_result const& r, std::string const& needle) {
    for (auto const& d : r.diags.all)
        if (!d.is_error && d.message.find(needle) != std::string::npos)
            return true;
    return false;
}

TEST_CASE("sema: reductivity warning for a self-sustaining fixpoint") {
    // The write never touches the matched cell — the anchor re-matches forever.
    compile_result bad(prelude + R"(
rule mark { level[floor] => tiles[1] }
program { all(policy=incremental) mark }
)");
    REQUIRE(bad.ok);   // a warning, not an error
    CHECK(has_warning(bad, "may never terminate"));

    // The write invalidates its own match — reductive, no warning.
    compile_result good(prelude + rfill +
        "program { all(policy=incremental) fill }");
    REQUIRE(good.ok);
    CHECK(!has_warning(good, "may never terminate"));

    // Bounded counts never warn, even for the self-sustaining rule.
    compile_result bounded(prelude + R"(
rule mark { level[floor] => tiles[1] }
program { some(max=5, policy=incremental) mark }
)");
    REQUIRE(bounded.ok);
    CHECK(!has_warning(bounded, "may never terminate"));
}

TEST_CASE("sema: tagset over the 30-value cap") {
    std::string big = "tag t { ";
    for (int i = 0; i < 31; ++i) big += "v" + std::to_string(i) + ", ";
    big += "}\nprogram { }";
    CHECK(compile_result(big).has_error("maximum is 30"));
}
