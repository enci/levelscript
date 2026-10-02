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
sequence main { resize(4, 3)  all fill }
)");
    INFO(r.diags.format_all());
    REQUIRE(r.ok);
    CHECK(r.prog.layer_id("level") == 0);
    CHECK(r.prog.layer_id("tiles") == 1);
    CHECK(r.prog.layers[1].tag_id == -1);
    CHECK(r.prog.value_id(0, "floor") == 1);
    REQUIRE(r.prog.rules.size() == 1);
    REQUIRE(r.prog.sequences.size() == 1);
    REQUIRE(r.prog.sequences[0].stmts.size() == 2);
}

TEST_CASE("sema: duplicate declarations") {
    CHECK(compile_result("tag a { x }\ntag a { y }\nsequence main { }").has_error("duplicate tag"));
    CHECK(compile_result("tag a { x, x }\nsequence main { }").has_error("duplicate tag value"));
    CHECK(compile_result(prelude +
        "rule r { level[.] => level[wall] }\nrule r { level[.] => level[wall] }\nsequence main { }")
        .has_error("duplicate rule"));
}

TEST_CASE("sema: unknown references") {
    CHECK(compile_result("layers { g: grid of nope }\nsequence main { }")
          .has_error("undeclared tag"));
    CHECK(compile_result(prelude + "rule r { nope[.] => nope[.] }\nsequence main { }")
          .has_error("undeclared grid"));
    CHECK(compile_result(prelude + "rule r { level[lava] => level[wall] }\nsequence main { }")
          .has_error("unknown tag value"));
    CHECK(compile_result(prelude + "rule r { level[.] => level[wall] }\nsequence main { all nope }")
          .has_error("undeclared rule"));
}

TEST_CASE("sema: cell/grid type mismatches") {
    CHECK(compile_result(prelude + "rule r { level[3] => level[wall] }\nsequence main { }")
          .has_error("integer cell in a tag grid"));
    CHECK(compile_result(prelude + "rule r { tiles[wall] => tiles[1] }\nsequence main { }")
          .has_error("number' grid cell"));
}

TEST_CASE("sema: shape mismatch") {
    CHECK(compile_result(prelude + "rule r { level[. .] => level[wall] }\nsequence main { }")
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
sequence main { }
)");
    CHECK(r.has_error("inconsistent row widths"));
}

TEST_CASE("sema: operation table") {
    CHECK(compile_result(prelude + "sequence main { warp(3, 3) }").has_error("unknown operation"));
    CHECK(compile_result(prelude + "sequence main { resize(3) }").has_error("requires argument 'h'"));
    CHECK(compile_result(prelude + "sequence main { resize(0, 5) }").has_error("must be positive"));
}

TEST_CASE("sema: some(max=0) is rejected") {
    CHECK(compile_result(prelude +
        "rule r { level[.] => level[wall] }\nsequence main { some(max=0) r }")
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
sequence main { }
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
sequence main { }
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
sequence main { }
)").has_error("dimension mismatch"));
}

TEST_CASE("sema: invalid attribute values") {
    CHECK(compile_result(prelude +
        "rule r(symmetry=diagonal) { level[.] => level[wall] }\nsequence main { }")
        .has_error("invalid value 'diagonal'"));
    CHECK(compile_result(prelude +
        "rule r(rotation=45) { level[.] => level[wall] }\nsequence main { }")
        .has_error("invalid rotation angle"));
}

static const std::string quad = R"(
tag t4 { a, b, c, d }
layers { q: grid of t4 }
)";

TEST_CASE("sema: variant expansion counts") {
    SECTION("rotation=all on an asymmetric 1x2 gives 4 variants") {
        compile_result r(prelude +
            "rule r(rotation=all) { level[wall floor] => level[floor wall] }\nsequence main { }");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 4);
    }
    SECTION("symmetry=all on a 1x2 gives 2 (vertical flip is identity)") {
        compile_result r(prelude +
            "rule r(symmetry=all) { level[wall floor] => level[floor wall] }\nsequence main { }");
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
sequence main { }
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
sequence main { }
)");
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 8);
    }
    SECTION("a symmetric pattern collapses to 1 variant") {
        compile_result r(prelude +
            "rule r(symmetry=all, rotation=all) { level[wall] => level[floor] }\nsequence main { }");
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
sequence main { }
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
    CHECK(compile_result(prelude + rfill + "sequence main { all(policy=ranked) fill }")
          .has_error("unknown policy 'ranked'"));
    CHECK(compile_result(prelude + rfill +
          "sequence main { some(percent=50, policy=incremental) fill }")
          .has_error("'percent' requires the default 'snapshot' policy"));
    CHECK(compile_result(prelude + rfill + "sequence main { one(policy=stabilize) fill }")
          .has_error("contradictory"));
    compile_result ok(prelude + rfill + R"(
sequence main {
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
sequence main { all(policy=incremental) mark }
)");
    REQUIRE(bad.ok);   // a warning, not an error
    CHECK(has_warning(bad, "may never terminate"));

    // The write invalidates its own match ??? reductive, no warning.
    compile_result good(prelude + rfill +
        "sequence main { all(policy=incremental) fill }");
    REQUIRE(good.ok);
    CHECK(!has_warning(good, "may never terminate"));

    // Bounded counts never warn, even for the self-sustaining rule.
    compile_result bounded(prelude + R"(
rule mark { level[floor] => tiles[1] }
sequence main { some(max=5, policy=incremental) mark }
)");
    REQUIRE(bounded.ok);
    CHECK(!has_warning(bounded, "may never terminate"));
}

// -- step 4: expression types, scopes, params, unions --

static const std::string rprog = "sequence main { }\n";

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
          "params { d: number = 0 }\nsequence main { all fill when (d + 1) }")
          .has_error("must be a boolean expression"));
    CHECK(compile_result(prelude + rfill +
          "sequence main { all fill when ((level == floor)) }")
          .has_error("cannot be read here"));
    CHECK(compile_result(prelude + rfill +
          "sequence main { all fill when (x > 0) }")
          .has_error("cannot be read here"));
}

TEST_CASE("sema: named unions") {
    compile_result ok(R"(
tag geo { wall, door, floor, blocker = wall | door, solid = blocker | floor }
layers { level: grid of geo }
rule r { level[blocker] => level[floor] }
sequence main { }
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

// -- step 5: operation table binding, kinds, path --

static const std::string path_pre = R"(
tag algo { door, exit, road }
layers {
    algo:  grid of algo
    tiles: grid of number
}
)";

TEST_CASE("sema: operation argument binding") {
    // positional may also be supplied by name; named-only rejects positional
    compile_result named_ok(prelude + "sequence main { resize(w=4, h=3) }");
    INFO(named_ok.diags.format_all());
    CHECK(named_ok.ok);
    CHECK(compile_result(prelude + "sequence main { resize(4, w=3) }")
          .has_error("supplied twice"));
    CHECK(compile_result(prelude + "sequence main { resize(w=4, 3) }")
          .has_error("positional argument after a named argument"));
    CHECK(compile_result(prelude + "sequence main { resize(4, 3, 2) }")
          .has_error("too many positional arguments"));
    CHECK(compile_result(prelude + "sequence main { resize(4, depth=3) }")
          .has_error("unknown parameter 'depth'"));
    CHECK(compile_result(prelude + "sequence main { trim(3) }")
          .has_error("takes no arguments"));
    CHECK(compile_result(path_pre + "sequence main { path(door, exit) }")
          .has_error("named-only"));
    CHECK(compile_result(path_pre + "sequence main { path(from=door, to=exit, into=algo) }")
          .has_error("requires argument 'write='"));
}

TEST_CASE("sema: operation argument kinds and constraints") {
    CHECK(compile_result(prelude + "sequence main { mirror(diagonal) }")
          .has_error("invalid mirror axis"));
    CHECK(compile_result(prelude + "sequence main { mirror(3) }")
          .has_error("invalid mirror axis '3'"));
    CHECK(compile_result(prelude + "sequence main { pad(-1) }")
          .has_error("must be non-negative"));
    CHECK(compile_result(prelude + "sequence main { upscale(0, 2) }")
          .has_error("must be positive"));
    CHECK(compile_result(path_pre +
          "sequence main { path(from=door, to=exit, into=nope, write=road) }")
          .has_error("not a declared grid"));
    CHECK(compile_result(path_pre +
          "sequence main { path(from=lava, to=exit, into=algo, write=road) }")
          .has_error("unknown tag value 'lava'"));
    CHECK(compile_result(path_pre +
          "sequence main { path(from=door, to=exit, into=algo, write=road, connectivity=5) }")
          .has_error("invalid connectivity"));
    CHECK(compile_result(path_pre +
          "sequence main { path(from=door, to=exit, into=tiles, write=road) }")
          .has_error("expected an integer"));   // number grid wants a number write
    compile_result ok(path_pre + R"(
sequence main {
    resize(8, 8)
    path(from=door, to=exit, into=algo, write=road,
         passable=((tiles > 0)), connectivity=8, cost=(1 + tiles))
}
)");
    INFO(ok.diags.format_all());
    CHECK(ok.ok);
}

TEST_CASE("sema: ambiguous bare predicate needs the expression form") {
    CHECK(compile_result(R"(
tag a { door }
tag b { door, road }
layers {
    g1: grid of a
    g2: grid of b
}
sequence main { path(from=door, to=road, into=g2, write=road) }
)").has_error("ambiguous"));
}

TEST_CASE("sema: tagset over the 30-value cap") {
    std::string big = "tag t { ";
    for (int i = 0; i < 31; ++i) big += "v" + std::to_string(i) + ", ";
    big += "}\nsequence main { }";
    CHECK(compile_result(big).has_error("maximum is 30"));
}

// -- spec 0.5 delta: Δ3 variant dedup key, Δ1 contextual names --

static const std::string trio = R"(
tag t3 { a, b, c }
layers { g: grid of t3 }
)";

TEST_CASE("sema: variant dedup keys on the (match, write) pair (spec §5.6.2)") {
    SECTION("same LHS, different writes: the H-flip survives") {
        compile_result r(trio + "rule r(symmetry=horizontal) { g[a a] => g[b c] }\n" + rprog);
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 2);
    }
    SECTION("same LHS, same writes: collapses to 1") {
        compile_result r(trio + "rule r(symmetry=horizontal) { g[a a] => g[b b] }\n" + rprog);
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 1);
    }
    SECTION("a symmetric LHS with a single-corner write keeps all four rotations") {
        compile_result r(trio + R"(
rule r(rotation=all) {
    g[
        a a
        a a ]
    =>
    g[
        a b
        b b ]
}
)" + rprog);
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 4);
    }
    SECTION("{ any } item order is part of the key (accepted cost, §10.2)") {
        compile_result r(trio +
            "rule r(symmetry=horizontal) { g[a a] => { any g[b c] g[c b] } }\n" + rprog);
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 2);
    }
    SECTION("a flip-invariant weighted { any } write collapses to 1") {
        compile_result r(trio +
            "rule r(symmetry=horizontal) { g[a a] => { any (weight=3) g[b b]  g[c c] } }\n" + rprog);
        REQUIRE(r.ok);
        CHECK(r.prog.rules[0].pairs.size() == 1);
    }
}

TEST_CASE("sema: contextual names (spec §2.4)") {
    SECTION("none/horizontal/vertical/symmetry/rotation are legal names") {
        compile_result r(R"(
tag none { horizontal, vertical }
layers { symmetry: grid of none }
params { rotation: number = 1 }
rule r(symmetry=horizontal) { symmetry[horizontal vertical] => symmetry[vertical horizontal] }
sequence main { mirror(horizontal) }
)");
        INFO(r.diags.format_all());
        CHECK(r.ok);
    }
    SECTION("max is still unavailable as a name: it is a built-in (check 21)") {
        CHECK(compile_result("tag t { max }\nlayers { g: grid of t }\n" + rprog)
              .has_error("built-in"));
        CHECK(compile_result("params { max: number = 3 }\n" + rprog)
              .has_error("built-in function name"));
        CHECK(compile_result("tag t { a }\nlayers { max: grid of t }\n" + rprog)
              .has_error("built-in"));
    }
    SECTION("attribute and axis values are still validated") {
        CHECK(compile_result(trio + "rule r(symmetry=rotation) { g[.] => g[a] }\n" + rprog)
              .has_error("invalid value 'rotation'"));
        CHECK(compile_result(trio + "sequence main { mirror(none) }\n").has_error("none"));
    }
}

TEST_CASE("sema: a module needs no entry - an empty file compiles (0.7)") {
    CHECK(compile_result(prelude).ok);
    CHECK(compile_result("").ok);
}

TEST_CASE("sema: variants record the rotation and flip that produced them (5.6)") {
    using mirror = ls::compiled_pair::mirror;
    // an asymmetric 1x2: symmetry=all keeps identity and the H flip (the V flip
    // of one row is the identity); rotation=90 adds its rotated variants
    compile_result r(prelude +
        "rule r(symmetry=all, rotation=90) { level[wall floor] => level[floor wall] }\n"
        "sequence main { }\n");
    REQUIRE(r.ok);
    std::vector<std::pair<int, mirror>> got;
    for (auto const& p : r.prog.rules[0].pairs) got.push_back({p.rotation, p.flip});
    CHECK(got == std::vector<std::pair<int, mirror>>{
        {0, mirror::none}, {0, mirror::h}, {90, mirror::none}, {90, mirror::v}});
}
