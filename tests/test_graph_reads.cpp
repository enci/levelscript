// Graph reads and graph rules in 0.17 (sections 5.12, 5.13): region reads,
// attributes at a cell, global queries, graph patterns relating two nodes,
// and symmetry=none.
#include "test_support.hpp"
#include "machine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <string>
#include <vector>

using ts::compile_result;
using ts::show;
using ts::stamp;
using ts::stamped;

namespace {

ls::level generate(std::string const& src, uint64_t seed = 1) {
    auto gen = ts::make(src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    return gen.generate(gen.sequence("main"), seed);
}

// Rooms R joined through halls H; marks on the other grids.
const std::string dungeon = R"(
tag algo { R, H, W }
tag room { Boss, Start, Dark }
tag item { chest, sword }
tag lamp { on }
layers {
    level: grid of algo
    rooms: grid of room
    items: grid of item
    light: grid of lamp
    depth: grid of number
    marks: grid of number
    map:   graph of level(nodes=R, edges=H)
}
)";

// Rooms A (2x2), B (2x2), C (1 cell): A-B and B-C through halls.
const std::vector<std::string> abc = {"R R H R R H R", "R R . R R . ."};

std::string run_abc(std::string const& setup, std::string const& stmt, std::string const& grid) {
    return show(generate(stamped(dungeon, "level", abc, setup + "\n    " + stmt)), grid);
}

}  // namespace

TEST_CASE("graph reads: a region read holds when every cell does") {
    // A holds two chests and two empty cells, B four chests, C nothing
    std::string chests = stamp("items", {"chest chest . chest chest . .", ". . . chest chest . ."});
    // every cell of the region a chest: only B
    CHECK(run_abc(chests, "everywhere { map { ?n } when (n.items is chest) => n.rooms = Boss }", "rooms")
          == "...BB../...BB..");
    // every cell empty: only C
    CHECK(run_abc(chests, "everywhere { map { ?n } when (n.items is .) => n.rooms = Boss }", "rooms")
          == "......B/.......");
    // `!(...)` negates the quantified read: not every cell a chest
    CHECK(run_abc(chests, "everywhere { map { ?n } when (!(n.items is chest)) => n.rooms = Boss }", "rooms")
          == "BB....B/BB.....");
    // `!n.g` complements each cell's read: no cell a chest
    CHECK(run_abc(chests, "everywhere { map { ?n } when (!n.items is chest) => n.rooms = Boss }", "rooms")
          == "......B/.......");
}

TEST_CASE("graph reads: region reads over a number grid") {
    std::string depths = stamp("depth", {"1 2 . 4 5 . 9", "3 4 . 6 7 . ."});
    // every cell deeper than 3: B (4 5 6 7) and C (9)
    CHECK(run_abc(depths, "everywhere { map { ?n } when (n.depth > 3) => n.rooms = Boss }", "rooms")
          == "...BB.B/...BB..");
    // the region on the right reads the same
    CHECK(run_abc(depths, "everywhere { map { ?n } when (3 < n.depth) => n.rooms = Boss }", "rooms")
          == "...BB.B/...BB..");
    // `== .` tests the cells raw: only an all-empty region
    std::string partial = stamp("depth", {". . . 4 5 . .", ". . . 6 . . ."});
    CHECK(run_abc(partial, "everywhere { map { ?n } when (n.depth == .) => n.rooms = Boss }", "rooms")
          == "BB....B/BB.....");
    // ... while in arithmetic an empty cell reads as 0
    CHECK(run_abc(partial, "everywhere { map { ?n } when (n.depth >= 0) => n.rooms = Boss }", "rooms")
          == "BB.BB.B/BB.BB..");
}

TEST_CASE("graph reads: attributes at a cell read the node covering it") {
    // sizes 4, 4, 1; an uncovered cell reads 0
    CHECK(run_abc("", "everywhere { level[*] => marks[ (map.size) ] }", "marks")
          == "4404401/4404400");
    CHECK(run_abc("", "everywhere { level[*] => marks[ (map.degree) ] }", "marks")
          == "1102201/1102200");
    // `map.size > 0` tests coverage
    CHECK(run_abc("", "everywhere { level[*] where[ (map.size > 0) ] => rooms[Start] }", "rooms")
          == "SS.SS.S/SS.SS..");
    // value reads the source; uncovered, it is empty
    CHECK(run_abc("", "everywhere { level[*] where[ (map.value == .) ] => rooms[Dark] }", "rooms")
          == "..D..D./..D..DD");
}

TEST_CASE("graph reads: a grid rule relates two nodes through a graph pattern") {
    // the spec's open_if_unconnected: rooms A, B, C behind walls, A-B joined
    // by a hall below; only the B-C wall opens
    std::string decls = R"(
tag geo { floor, wall, door, hall }
layers {
    level: grid of geo
    map:   graph of level(nodes=floor, edges=hall|door)
}
rule open_if_unconnected(rotation=all) {
    level[floor wall floor]
    map[?a * ?b]
    when (distance(a, b) > 1)
    =>
    level[* door *]
}
)";
    auto lv = generate(stamped(decls, "level", {"floor wall floor wall floor", "hall hall hall . ."},
                               "everywhere open_if_unconnected"));
    CHECK(show(lv, "level") == "fwfdf/hhh..");
    // the nodes are nodes like a block's: attributes and region reads
    compile_result ok(dungeon + "rule r { map[?a * ?b] when (a != b && a.size < b.size && "
                                "a.items is .) => level[* W *] }\n");
    INFO(ok.diags.format_all());
    CHECK(ok.ok);
}

TEST_CASE("graph reads: global queries") {
    // a count from the stack: scatter one item per two room cells
    auto lv = generate(stamped(dungeon, "level", abc,
        "scatter(count(level[R]) / 2) { level[R] items[.] => items[chest] }"));
    int chests = 0;
    for (char c : show(lv, "items")) chests += c == 'c';
    CHECK(chests == 4);   // 9 room cells
    // nodes and edges, in a statement guard
    CHECK(run_abc("", "everywhere { level[R] => rooms[Boss] } when (nodes(map) == 3 && edges(map) == 2)",
                  "rooms") == "BB.BB.B/BB.BB..");
    CHECK(run_abc("", "everywhere { level[R] => rooms[Boss] } when (nodes(map) > 3)", "rooms")
          == "......./.......");
    // a rule guard reads its pass's snapshot: under grow each application
    // sees the ones before it, under everywhere the statement-entry state
    std::string chest = "{ level[R] items[.] when (count(items[chest]) < 3) => items[chest] }";
    auto count_chests = [&](std::string const& mode) {
        int n = 0;
        for (char c : run_abc("", mode + " " + chest, "items")) n += c == 'c';
        return n;
    };
    CHECK(count_chests("grow") == 3);
    CHECK(count_chests("everywhere") == 9);
}

TEST_CASE("graph reads: global queries are not param expressions") {
    // checks 23, 26
    CHECK(compile_result(dungeon + "params { k = nodes(map) }").has_error("which a param expression cannot"));
    CHECK(compile_result(dungeon + "params { k: number = count(level[R]) }")
              .has_error("which a param expression cannot"));
    // check 53: nodes and edges take a graph name
    CHECK(compile_result(dungeon + "sequence main { everywhere { level[R] => level[H] } when (nodes(level) > 0) }")
              .has_error("takes a graph name"));
}

TEST_CASE("graph reads: symmetry=none finds the pair's symmetries") {
    auto symmetries = [&](std::string const& rule) {
        compile_result r(dungeon + rule);
        INFO(r.diags.format_all());
        REQUIRE(r.ok);
        return r.prog.rules[0].pairs[0].symmetries.size();
    };
    // the attribute is opt-in
    CHECK(symmetries("rule r { map { ?a -- ?b } => { all a.rooms = Boss b.rooms = Boss } }") == 0);
    // a symmetric edge: a <-> b
    CHECK(symmetries("rule r(symmetry=none) { map { ?a -- ?b } => { all a.rooms = Boss b.rooms = Boss } }") == 1);
    // however the edge, the guard, and the writes are ordered
    CHECK(symmetries("rule r(symmetry=none) { map { ?b -- ?a } when (b.items is chest && a.items is chest) "
                     "=> { all b.rooms = Boss a.rooms = Boss } }") == 1);
    CHECK(symmetries("rule r(symmetry=none) { map { ?a -- ?b } when (a.size > b.size || b.size > a.size) "
                     "=> { all a.rooms = Boss b.rooms = Boss } }") == 1);
    CHECK(symmetries("rule r(symmetry=none) { map { ?a ?b } when (distance(a, b) > 2) "
                     "=> { all a.rooms = Boss b.rooms = Boss } }") == 1);
    // writes that tell the roles apart are no symmetry
    CHECK(symmetries("rule r(symmetry=none) { map { ?a -- ?b } => a.rooms = Boss }") == 0);
    CHECK(symmetries("rule r(symmetry=none) { map { ?a -- ?b } when (a.size > b.size) "
                     "=> { all a.rooms = Boss b.rooms = Boss } }") == 0);
    // market_hub: b <-> c
    CHECK(symmetries("rule r(symmetry=none) { map { ?h -- ?a ?h -- ?b ?h -- ?c } "
                     "when (h.degree == 4 && a.rooms is Start) => { all h.rooms = Boss a.items = chest } }") == 1);
    // three interchangeable nodes: every permutation but the identity
    CHECK(symmetries("rule r(symmetry=none) { map { ?a ?b ?c } "
                     "=> { all a.rooms = Boss b.rooms = Boss c.rooms = Boss } }") == 5);
    // a symmetry only after algebra is not found
    CHECK(symmetries("rule r(symmetry=none) { map { ?a -- ?b } when (a.size - b.size != 0) "
                     "=> { all a.rooms = Boss b.rooms = Boss } }") == 0);
}

TEST_CASE("graph reads: symmetry=none drops duplicate assignments") {
    // Two rooms and one edge: `?a -- ?b` matches it twice, (0,1) and (1,0).
    // A batch shuffles its candidates with uniform(k), ..., uniform(2), so the
    // draw after it tells how many there were: a `once` stamp of a random
    // number, replayed here.
    std::string decls = dungeon;
    auto stamped_value = [&](std::string const& attrs) {
        auto lv = generate(stamped(decls, "level", {"R H R"},
            "everywhere { map" + attrs + " }\n"
            "    once { marks[.] where[ (x == 0) ] => marks[ (random(0, 999)) ] }"), 3);
        return lv["marks"].at(0, 0);
    };
    auto replay = [](int candidates) {
        std::mt19937_64 rng(3);
        ls::internal::uniform_draw(rng, 1);                     // the level stamp's pick
        for (int i = candidates; i > 1; --i) ls::internal::uniform_draw(rng, (uint64_t)i);
        ls::internal::uniform_draw(rng, 1);                     // the marks stamp's pick
        return (long long)ls::internal::uniform_draw(rng, 1000);
    };
    std::string pair = " { ?a -- ?b } => { all a.rooms = Boss b.rooms = Boss }";
    CHECK(stamped_value(pair) == replay(2));
    decls = dungeon + "rule dedup(symmetry=none) { map { ?a -- ?b } => { all a.rooms = Boss b.rooms = Boss } }\n";
    auto lv = generate(stamped(decls, "level", {"R H R"},
        "everywhere dedup\n"
        "    once { marks[.] where[ (x == 0) ] => marks[ (random(0, 999)) ] }"), 3);
    CHECK(lv["marks"].at(0, 0) == replay(1));
    CHECK(show(lv, "rooms") == "B.B");
}

TEST_CASE("graph reads: symmetry=none drops duplicates after the guards draw") {
    // Each assignment draws its guard; without the attribute an edge is up to
    // two candidates, with it at most one, the first that matches. The guards
    // draw the same either way, so the edge matches in the same runs.
    std::string rule = "{ ?a -- ?b } when (random(0, 1) == 1) => { all a.marks = 1 b.marks = 1 }";
    std::string decls = dungeon + "rule plain { map " + rule + " }\nrule dedup(symmetry=none) { map " + rule + " }\n";
    auto gen = ts::make(stamped(decls, "level", {"R H R"}, "everywhere plain"));
    auto dedup = ts::make(stamped(decls, "level", {"R H R"}, "everywhere dedup"));
    REQUIRE(static_cast<bool>(gen));
    REQUIRE(static_cast<bool>(dedup));
    for (uint64_t seed = 1; seed <= 50; ++seed) {
        INFO("seed " << seed);
        CHECK(show(gen.generate(gen.sequence("main"), seed), "marks") ==
              show(dedup.generate(dedup.sequence("main"), seed), "marks"));
    }
}

// ── path over a graph (0.17.1, section 6.6) ──────────────────────────────────

namespace {
// Start in one cell of A, Boss in one cell of C.
const std::string start_boss = stamp("rooms", {". Start . . . . Boss", ". . . . . . ."});
}  // namespace

TEST_CASE("path over a graph: stamps the route's node regions, not the connectors") {
    CHECK(run_abc(start_boss,
                  "path(from=(rooms is Start), to=(rooms is Boss), over=map, into=marks, write=1)",
                  "marks") == "11.11.1/11.11..");
}

TEST_CASE("path over a graph: node sets use selector semantics") {
    // passable only where Dark is: B qualifies through its one Dark cell
    std::string rooms = stamp("rooms", {". Start . . Dark . Boss", ". . . . . . ."});
    CHECK(run_abc(rooms,
                  "path(from=(rooms is Start), to=(rooms is Boss), over=map, into=marks, write=1, "
                  "passable=(rooms is Dark))",
                  "marks") == "11.11.1/11.11..");
    // B has no Dark cell: no route, `into` is left as it is
    CHECK(run_abc(start_boss,
                  "path(from=(rooms is Start), to=(rooms is Boss), over=map, into=marks, write=1, "
                  "passable=(rooms is Dark))",
                  "marks") == "......./.......");
}

TEST_CASE("path over a graph: a node both from and to is a one-node route") {
    CHECK(run_abc(stamp("rooms", {". . . Start Boss . .", ". . . . . . ."}),
                  "path(from=(rooms is Start), to=(rooms is Boss), over=map, into=marks, write=1)",
                  "marks") == "...11../...11..");
}

TEST_CASE("path over a graph: write evaluates per stamped cell; a later rule marks the doors") {
    std::string decls = dungeon + R"(
rule route_doors(rotation=90) {
    marks[1 * 1]
    level[* H *]
    =>
    marks[* 1 *]
}
)";
    auto lv = generate(stamped(decls, "level", abc,
                               start_boss + "\n    path(from=(rooms is Start), to=(rooms is Boss), "
                                            "over=map, into=marks, write=1)\n    everywhere route_doors"));
    CHECK(show(lv, "marks") == "1111111/11.11..");
}

TEST_CASE("path over a graph: ties break by seed, reproducibly, on a shortest route") {
    // a ring of four rooms: S top-left, G bottom-right; two equal routes
    std::vector<std::string> ring = {"R H R", "H . H", "R H R"};
    std::string rooms = stamp("rooms", {"Start . .", ". . .", ". . Boss"});
    std::string src = stamped(dungeon, "level", ring,
                              rooms + "\n    path(from=(rooms is Start), to=(rooms is Boss), "
                                      "over=map, into=marks, write=1)");
    bool via_right = false, via_left = false;
    for (uint64_t seed = 1; seed <= 32; ++seed) {
        std::string m = show(generate(src, seed), "marks");
        CHECK(m == show(generate(src, seed), "marks"));
        CHECK((m == "1.1/.../..1" || m == "1../.../1.1"));
        via_right |= m == "1.1/.../..1";
        via_left  |= m == "1../.../1.1";
    }
    CHECK(via_right);
    CHECK(via_left);
}
