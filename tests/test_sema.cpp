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

// -- step 2: write trees, attributes, variant expansion --

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

// -- step 3: count x policy validity, reductivity --

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
    // The write never touches the matched cell ??? the anchor re-matches forever.
    compile_result bad(prelude + R"(
rule mark { level[floor] => tiles[1] }
program { all(policy=incremental) mark }
)");
    REQUIRE(bad.ok);   // a warning, not an error
    CHECK(has_warning(bad, "may never terminate"));

    // The write invalidates its own match ??? reductive, no warning.
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

// -- step 4: expression types, scopes, params, unions --

static const std::string rprog = "program { }\n";

TEST_CASE("sema: expression type errors") {
    CHECK(compile_result(prelude + "rule r { level[ (wall + 1) ] => level[floor] }\n" + rprog)
          .has_error("arithmetic operator requires numeric"));
    CHECK(compile_result(prelude + "rule r { level[ (wall == 1) ] => level[floor] }\n" + rprog)
          .has_error("require two numbers or two tag values"));
    CHECK(compile_result(prelude + "rule r { tiles[ (wall) ] => tiles[1] }\n" + rprog)
          .has_error("must evaluate to a number"));
    CHECK(compile_result(prelude + "rule r { level[ (tiles > 5) ] => level[floor] }\n" + rprog)
          .has_error("must evaluate to a tag value"));
    CHECK(compile_result(prelude + "rule r { level[ (. == .) ] => level[floor] }\n" + rprog)
          .has_error("no inferable type"));
    CHECK(compile_result(prelude + "rule r { level[ (nope + 1) ] => level[floor] }\n" + rprog)
          .has_error("unknown identifier 'nope'"));
}

TEST_CASE("sema: built-in calls") {
    CHECK(compile_result(prelude + "rule r { tiles[ (warp(1)) ] => tiles[1] }\n" + rprog)
          .has_error("unknown function 'warp'"));
    CHECK(compile_result(prelude + "rule r { tiles[ (min(1)) ] => tiles[1] }\n" + rprog)
          .has_error("takes 2 argument(s)"));
    CHECK(compile_result(prelude + "rule r { tiles[ (if(1, 2, 3)) ] => tiles[1] }\n" + rprog)
          .has_error("condition must be boolean"));
    CHECK(compile_result(prelude +
          "rule r { tiles[ (if(tiles > 0, 1, wall)) ] => tiles[1] }\n" + rprog)
          .has_error("branches must have the same type"));
    compile_result ok(prelude +
        "rule r { tiles[ (clamp(random(0, 9), 1, abs(-5))) ] => tiles[ (max(tiles, 1)) ] }\n" + rprog);
    INFO(ok.diags.format_all());
    CHECK(ok.ok);
}

TEST_CASE("sema: where discipline") {
    CHECK(compile_result(prelude + "rule r { level[.] => where[ (x == 0) ] }\n" + rprog)
          .has_error("cannot appear on the write side"));
    CHECK(compile_result(prelude + R"(
rule r {
    { all
      level[.]
      where[ (x + 1) ]
    }
    =>
    level[floor]
}
)" + rprog).has_error("must evaluate to a boolean"));
}

TEST_CASE("sema: complement is match-side only") {
    CHECK(compile_result(prelude + "rule r { level[wall] => level[!wall] }\n" + rprog)
          .has_error("not allowed on the write side"));
}

TEST_CASE("sema: param scopes") {
    // input param without a default (??7.3 #27)
    CHECK(compile_result("params { d: number }\n" + rprog)
          .has_error("must have a default"));
    // derived params read earlier params only
    CHECK(compile_result("params { a = b * 2\n b: number = 1 }\n" + rprog)
          .has_error("referenced before it is declared"));
    // no grids, no position at param scope
    CHECK(compile_result(prelude + "params { a = level + 1 }\n" + rprog)
          .has_error("cannot be read here"));
    CHECK(compile_result("params { a = x + 1 }\n" + rprog)
          .has_error("cannot be read here"));
    // reserved / collision names
    CHECK(compile_result("params { width: number = 3 }\n" + rprog)
          .has_error("reserved expression identifier"));
    CHECK(compile_result("params { random: number = 3 }\n" + rprog)
          .has_error("built-in function name"));
    CHECK(compile_result(prelude + "params { level: number = 3 }\n" + rprog)
          .has_error("collides with a grid"));
    // valid: defaults may use earlier params and random
    compile_result ok("params { a: number = 2\n b: number = random(0, a)\n c = a + b }\n" + rprog);
    INFO(ok.diags.format_all());
    CHECK(ok.ok);
}

TEST_CASE("sema: when guard discipline") {
    std::string pre = prelude + rfill + "params { d: number = 0 }\n";
    CHECK(compile_result(prelude + rfill +
          "params { d: number = 0 }\nprogram { all fill when (d + 1) }")
          .has_error("must be a boolean expression"));
    CHECK(compile_result(prelude + rfill +
          "program { all fill when ((level == floor)) }")
          .has_error("cannot be read here"));
    CHECK(compile_result(prelude + rfill +
          "program { all fill when (x > 0) }")
          .has_error("cannot be read here"));
}

TEST_CASE("sema: named unions") {
    compile_result ok(R"(
tag geo { wall, door, floor, blocker = wall | door, solid = blocker | floor }
layers { level: grid of geo }
rule r { level[blocker] => level[floor] }
program { }
)");
    INFO(ok.diags.format_all());
    REQUIRE(ok.ok);
    CHECK(ok.prog.mask_of(0, "blocker") == (ls::tag_bit(0) | ls::tag_bit(1)));
    CHECK(ok.prog.mask_of(0, "solid") ==
          (ls::tag_bit(0) | ls::tag_bit(1) | ls::tag_bit(2)));
    // the rule's LHS cell carries the union mask
    CHECK(ok.prog.rules[0].pairs[0].lhs[0].at(0, 0).val == (ls::tag_bit(0) | ls::tag_bit(1)));

    CHECK(compile_result("tag geo { wall, b = nope }\n" + rprog)
          .has_error("not a value or earlier union"));
    CHECK(compile_result("tag geo { wall, b = c, c = wall }\n" + rprog)
          .has_error("not a value or earlier union"));   // forward reference
    CHECK(compile_result("tag geo { wall, wall = wall }\n" + rprog)
          .has_error("redeclares"));
}

TEST_CASE("sema: tagset over the 30-value cap") {
    std::string big = "tag t { ";
    for (int i = 0; i < 31; ++i) big += "v" + std::to_string(i) + ", ";
    big += "}\nprogram { }";
    CHECK(compile_result(big).has_error("maximum is 30"));
}
