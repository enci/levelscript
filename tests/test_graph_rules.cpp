// Graph rules (section 5.13): a graph block of node variables, edges, and
// value tests matches a subgraph; node extents write whole regions. Also
// `value(n)`, the graph function graph rules rely on.
#include "test_support.hpp"
#include "machine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <set>
#include <string>

using ts::compile_result;

namespace {

// Rooms on a tag grid, zones on a number grid, a graph of each.
const std::string world = R"(
tag room { Hall, Shop, Inn, Start, Treasure, Market, Closet }
tag deco { stall }
layers {
    rooms: grid of room
    decor: grid of deco
    zones: grid of number
    plan:  graph of rooms
    areas: graph of zones
}
)";

compile_result rule_with(std::string const& body) {
    return compile_result(world + "rule r { " + body + " }\n");
}

void ok(std::string const& src) {
    compile_result r(src);
    INFO(src);
    INFO(r.diags.format_all());
    CHECK(r.ok);
}

}  // namespace

TEST_CASE("graph rules: the spec's examples compile") {
    ok(world + R"(
rule treasure_room {
    plan { ?n }
    when (degree(?n) == 1 && distance(?n, rooms[Start]) > 3)
    =>
    rooms(?n)[Treasure]
}
rule market_hub {
    plan {
        ?hub[Hall]
        ?a[Shop]
        ?hub -- ?a
        ?hub -- ?b
        ?hub -- ?c
    }
    when (degree(?hub) == 4)
    =>
    { all
        rooms(?hub)[Market]
        decor(?a)[stall]
    }
}
)");
}

TEST_CASE("graph rules: the compiled pair") {
    compile_result r = rule_with("plan {\n ?a -- ?b\n ?c\n ?b[Hall|Inn]\n ?c -- ?a\n}"
                                 " when (?a != ?c) => { all rooms(?a)[Shop] decor(?b)[stall] }");
    INFO(r.diags.format_all());
    REQUIRE(r.ok);
    REQUIRE(r.prog.rules[0].pairs.size() == 1);   // no variants
    auto const& p = r.prog.rules[0].pairs[0];
    CHECK(p.graph == r.prog.graph_id("plan"));
    CHECK(p.lhs.empty());
    // slots in order of first appearance: the enumeration order
    CHECK(p.var_names == std::vector<std::string>{"?a", "?b", "?c"});
    REQUIRE(p.node_edges.size() == 2);
    CHECK((p.node_edges[0].a == 0 && p.node_edges[0].b == 1));
    CHECK((p.node_edges[1].a == 2 && p.node_edges[1].b == 0));
    REQUIRE(p.node_tests.size() == 1);
    CHECK(p.node_tests[0].slot == 1);
    int room = r.prog.tag_id("room");
    CHECK(p.node_tests[0].val == (r.prog.mask_of(room, "Hall") | r.prog.mask_of(room, "Inn")));
    REQUIRE(p.guards.size() == 1);
    CHECK(p.guards[0].after == 0);
    std::vector<ls::compiled_pattern const*> leaves;
    ls::collect_write_leaves(p.rhs, leaves);
    REQUIRE(leaves.size() == 2);
    CHECK(leaves[0]->extent == 0);
    CHECK(leaves[0]->grid_id == r.prog.layer_id("rooms"));
    CHECK(leaves[1]->extent == 1);
    CHECK(leaves[1]->grid_id == r.prog.layer_id("decor"));
}

TEST_CASE("graph rules: writes and tests on number-grid sources") {
    ok(world + "rule r { areas { ?z[3] ?w } when (adjacent(?z, ?w)) => zones(?z)[(value(?w) + 1)] }\n");
    // writes may target any grid, the source included, and read the cell
    ok(world + "rule r { plan { ?n } => { all rooms(?n)[(if(x > y, Hall, Inn))] zones(?n)[(size(?n))] } }\n");
    // sub-rules may name different graphs
    ok(world + "rule r { ordered\n plan { ?n[Hall] } => rooms(?n)[Inn]\n areas { ?z } => zones(?z)[0]\n}\n");
}

TEST_CASE("graph rules: value()") {
    // a tag mask of the source's tagset, or a number
    ok(world + "rule r { plan { ?n } when (value(?n) == Hall) => rooms(?n)[Inn] }\n");
    ok(world + "rule r { areas { ?z } when (value(?z) > 2) => zones(?z)[0] }\n");
    // in a grid rule, through a graph pattern
    ok(world + "rule r { areas[?z] when (value(?z) > 2) => zones[ (value(?z) - 1) ] }\n");
    CHECK(rule_with("areas { ?z } when (value(?z) == Hall) => zones(?z)[0]").has_error("'==' / '!=' require"));
    CHECK(rule_with("plan { ?n } when (value(?n) > 2) => rooms(?n)[Inn]").has_error("relational operator"));
    CHECK(rule_with("plan { ?n } when (value(?n, ?n) == Hall) => rooms(?n)[Inn]").has_error("'value' takes a node, e.g."));
    CHECK(rule_with("rooms[?t] when (value(?t) == Hall) => rooms[Inn]").has_error("takes a node first"));
    // not reserved: a grid may be called `value`
    ok("tag t { a }\nlayers { value: grid of t\n g: graph of value }\n"
       "rule r { g { ?n } when (value(?n) == a) => value(?n)[a] }\n");
}

TEST_CASE("graph rules: node extents") {
    // check 57
    CHECK(rule_with("rooms(?n)[Hall] => rooms[Inn]").has_error("appears only on the write side of a graph rule"));
    CHECK(rule_with("plan[?n] => rooms(?n)[Inn]").has_error("appears only on the write side of a graph rule"));
    CHECK(rule_with("plan { ?n } => rooms(?m)[Inn]").has_error("'?m' is not declared in the graph block"));
    CHECK(rule_with("plan { ?n } => rooms(?n)[Inn Hall]").has_error("writes a single cell over its node"));
    CHECK(rule_with("plan { ?n } => rooms(?n)[Inn, Hall]").has_error("writes a single cell over its node"));
    CHECK(rule_with("plan { ?n } => rooms[Inn]").has_error("every write of a graph rule is a node extent"));
    CHECK(rule_with("plan { ?n } => rooms(2, 2)[Inn]").has_error("every write of a graph rule is a node extent"));
    CHECK(rule_with("plan { ?n } => { any rooms(?n)[Inn] rooms[Hall] }")
              .has_error("every write of a graph rule is a node extent"));
    // the usual write-side checks still hold
    CHECK(rule_with("plan { ?n } => plan(?n)[Inn]").has_error("graph 'plan' cannot be written"));
    CHECK(rule_with("plan { ?n } => where(?n)[(x > 0)]").has_error("'where' is a match-side pseudo-layer"));
    CHECK(rule_with("plan { ?n } => nope(?n)[Inn]").has_error("undeclared grid 'nope'"));
    CHECK(rule_with("plan { ?n } => rooms(?n)[?n]").has_error("holds a node of graph 'plan' value"));
    CHECK(rule_with("plan { ?n } => rooms(?n)[!Inn]").has_error("is not allowed on the write side"));
    CHECK(rule_with("plan { ?n } => zones(?n)[Inn]").has_error("expected an integer or wildcard"));
}

TEST_CASE("graph rules: block variables") {
    // check 44: a variable is declared by its block
    CHECK(rule_with("plan { ?n } when (?m != ?n) => rooms(?n)[Inn]").has_error("'?m' is not declared in the graph block"));
    CHECK(rule_with("plan { ?n } => rooms(?n)[(if(size(?m) > 1, Inn, Hall))]")
              .has_error("'?m' is not declared in the graph block"));
    // check 56: guards stay candidate-scope
    CHECK(rule_with("plan { ?n } when (rooms == Hall) => rooms(?n)[Inn]").has_error("cannot be read here"));
    CHECK(rule_with("plan { ?n } when (x > 0) => rooms(?n)[Inn]").has_error("cannot be read here"));
    CHECK(rule_with("plan { ?n } when (size(?n)) => rooms(?n)[Inn]").has_error("a rule guard must be a boolean"));
    // variables are nodes of the block's graph
    CHECK(rule_with("plan { ?n } areas { ?z } => rooms(?n)[Inn]").has_error("does not conjoin"));
}

TEST_CASE("graph rules: block form") {
    // check 58
    CHECK(rule_with("rooms { ?n } => rooms(?n)[Inn]").has_error("'rooms' is a grid; a graph block names a graph layer"));
    CHECK(rule_with("nope { ?n } => rooms(?n)[Inn]").has_error("undeclared graph 'nope'"));
    CHECK(rule_with("plan { ?n[Hall] ?n[Inn] } => rooms(?n)[Inn]").has_error("already has a test line"));
    CHECK(rule_with("plan { ?n -- ?n } => rooms(?n)[Inn]").has_error("cannot be joined to itself"));
    CHECK(rule_with("plan { ?n[3] } => rooms(?n)[Inn]").has_error("a tag-grid source; a node test is a tag mask"));
    CHECK(rule_with("areas { ?z[Hall] } => zones(?z)[0]").has_error("a number-grid source; a node test is an integer"));
    CHECK(rule_with("areas { ?z[.] } => zones(?z)[0]").has_error("a number-grid source; a node test is an integer"));
    for (char const* t : {".", "!.", "Hall|."}) {
        INFO(t);
        CHECK(rule_with(std::string("plan { ?n[") + t + "] } => rooms(?n)[Inn]").has_error("a node is never empty"));
    }
    CHECK(rule_with("plan { ?n[Nope] } => rooms(?n)[Inn]").has_error("unknown tag value 'Nope'"));
    // a complement is a fine test: any value but the ones named
    ok(world + "rule r { plan { ?n[!Hall] } => rooms(?n)[Inn] }\n");
    // no shape, no variants
    CHECK(compile_result(world + "rule r(rotation=all) { plan { ?n } => rooms(?n)[Inn] }\n")
              .has_error("a graph rule takes no 'symmetry' or 'rotation'"));
    CHECK(compile_result(world + "rule r(symmetry=horizontal) { plan { ?n } => rooms(?n)[Inn] }\n")
              .has_error("a graph rule takes no 'symmetry' or 'rotation'"));
    CHECK(compile_result(world + "sequence s { once (rotation=90) { plan { ?n } => rooms(?n)[Inn] } }\n")
              .has_error("a graph rule takes no 'symmetry' or 'rotation'"));
    // one body, one kind of sub-rule
    CHECK(rule_with("all\n plan { ?n } => rooms(?n)[Inn]\n rooms[Hall] => rooms[Inn]")
              .has_error("cannot mix graph and grid sub-rules"));
    CHECK(rule_with("all\n rooms[Hall] => rooms[Inn]\n plan { ?n } => rooms(?n)[Inn]")
              .has_error("cannot mix graph and grid sub-rules"));
}

TEST_CASE("graph rules: same-grid simultaneous writes") {
    // check 31: one grid over one node, twice
    CHECK(rule_with("plan { ?n } => { all rooms(?n)[Inn] rooms(?n)[Hall] }")
              .has_error("is written twice over node '?n'"));
    CHECK(rule_with("plan { ?n } => { all rooms(?n)[Inn] { any rooms(?n)[Hall] decor(?n)[stall] } }")
              .has_error("is written twice over node '?n'"));
    // distinct variables are distinct nodes; distinct grids never conflict
    ok(world + "rule r { plan { ?a ?b } => { all rooms(?a)[Inn] rooms(?b)[Hall] decor(?a)[stall] } }\n");
}

TEST_CASE("graph rules: grow never warns") {
    // section 6.9: a graph rule is uncertain, so the reductivity check is silent
    compile_result r(world + "sequence main { grow { plan { ?n } => rooms(?n)[Inn] } }\n");
    INFO(r.diags.format_all());
    REQUIRE(r.ok);
    for (auto const& d : r.diags.all) CHECK(d.is_error);
}

// ── running graph rules ──────────────────────────────────────────────────────

namespace {

ls::level generate(std::string const& src, uint64_t seed = 1) {
    auto gen = ts::make(src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    return gen.generate(gen.sequence("main"), seed);
}

// Rooms R joined through halls H; marks on `rooms`, `items`, and `ids`.
const std::string dungeon = R"(
tag algo { R, H, W }
tag room { DeadEnd, Boss, Start }
tag item { chest }
layers {
    level:  grid of algo
    rooms:  grid of room
    items:  grid of item
    ids:    grid of number
    map:    graph of level(nodes=R, edges=H)
    layout: graph of level(nodes=R|H)
    areas:  graph of ids
}
)";

// Rooms A (2x2), B (2x2), C (1 cell): A-B and B-C through halls.
const std::vector<std::string> abc = {"R R H R R H R", "R R . R R . ."};
// Three one-cell rooms in a chain: nodes 0 - 1 - 2.
const std::vector<std::string> chain = {"R H R H R"};

}  // namespace

TEST_CASE("graph rules: a node extent writes the whole region") {
    auto lv = generate(ts::stamped(dungeon, "level", abc,
        "everywhere { map { ?n } when (degree(?n) == 1) => rooms(?n)[DeadEnd] }"));
    CHECK(ts::show(lv, "rooms") == "DD....D/DD.....");
    // a write cell evaluates per cell of the region, with x/y that cell
    auto ids = generate(ts::stamped(dungeon, "level", abc,
        "everywhere { map { ?n } => ids(?n)[ (x + 10 * y) ] }"));
    CHECK(ts::show(ids, "ids") == "01.34.6/1011.1314..");
}

TEST_CASE("graph rules: once chooses uniformly among nodes, whatever their size") {
    // C is one cell of nine room cells, but one node of three
    auto picks_c = [&](std::string const& rule) {
        auto gen = ts::make(ts::stamped(dungeon, "level", abc, rule));
        REQUIRE(static_cast<bool>(gen));
        int n = 0;
        for (uint64_t seed = 1; seed <= 300; ++seed)
            n += !gen.generate(gen.sequence("main"), seed)["rooms"].is_empty(6, 0);
        return n;
    };
    int by_node = picks_c("once { map { ?n } => rooms(?n)[Boss] }");
    int by_cell = picks_c("once { map[?n] => rooms[Boss] }");
    CHECK(by_node > 70);    // ~100 of 300
    CHECK(by_node < 130);
    CHECK(by_cell < 60);    // ~33 of 300
}

TEST_CASE("graph rules: assignments are collected in lexicographic node order") {
    // `?a -- ?b` on the chain: (0,1) (1,0) (1,2) (2,1) - every edge twice. A
    // step pick is the item at uniform(4), after the stamp's one uniform(1).
    std::string rule = "once { map { ?a -- ?b } => { all rooms(?a)[Start] items(?b)[chest] } }";
    int const want[4][2] = {{0, 1}, {1, 0}, {1, 2}, {2, 1}};
    auto gen = ts::make(ts::stamped(dungeon, "level", chain, rule));
    REQUIRE(static_cast<bool>(gen));
    std::set<int> seen;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        std::mt19937_64 rng(seed);
        ls::internal::uniform_draw(rng, 1);
        int k = (int)ls::internal::uniform_draw(rng, 4);
        seen.insert(k);
        auto lv = gen.generate(gen.sequence("main"), seed);
        auto lay = lv.graph("map");
        INFO("seed " << seed);
        int a = -1, b = -1;
        for (int x = 0; x < 5; ++x) {
            if (!lv["rooms"].is_empty(x, 0)) a = lay.node_at(x, 0);
            if (!lv["items"].is_empty(x, 0)) b = lay.node_at(x, 0);
        }
        CHECK(a == want[k][0]);
        CHECK(b == want[k][1]);
    }
    CHECK(seen.size() == 4);
}

TEST_CASE("graph rules: tests and edges filter assignments") {
    // a test reads the node's value in the source; an edge line needs adjacency
    auto chests = generate(ts::stamped(dungeon, "level", {"R H R H W"},
        "everywhere { layout { ?n[R] ?m[H] ?n -- ?m } => items(?n)[chest] }"));
    CHECK(ts::show(chests, "items") == "c.c..");
    // non-induced: unlisted edges are fine; `!adjacent` excludes them
    auto pairs = generate(ts::stamped(dungeon, "level", chain,
        "everywhere { map { ?a ?b } when (!adjacent(?a, ?b) && ?a != ?b) => rooms(?a)[Boss] }"));
    CHECK(ts::show(pairs, "rooms") == "B...B");
    // more variables than nodes: no candidates
    auto none = generate(ts::stamped(dungeon, "level", {"R H R"},
        "everywhere { map { ?a ?b ?c } => rooms(?a)[Boss] }"));
    CHECK(ts::show(none, "rooms") == "...");
}

TEST_CASE("graph rules: the write mask works on regions") {
    // The applications of one statement: its run's steps beyond a run without it.
    auto steps = [&](std::string const& stmt) {
        auto gen = ts::make(ts::stamped(dungeon, "level", chain, stmt));
        REQUIRE(static_cast<bool>(gen));
        auto run = gen.run(gen.sequence("main"), 7, ls::step_mode::application);
        int n = 0;
        while (run.step()) ++n;
        return n;
    };
    int base = steps("everywhere { map { ?n } when (size(?n) > 1) => rooms(?n)[Start] }");
    // as ?a, nodes 0 and 2 have one assignment each and node 1 two, which
    // conflict on rooms over node 1: three applications
    CHECK(steps("everywhere { map { ?a -- ?b } => rooms(?a)[Start] }") - base == 3);
    // with a second grid, (0,1) and (2,1) also share items over node 1:
    // whatever the shuffle, two applications
    CHECK(steps("everywhere { map { ?a -- ?b } => { all rooms(?a)[Start] items(?b)[chest] } }") - base == 2);
    // a one-variable rule never conflicts with itself
    CHECK(steps("everywhere { map { ?n } => rooms(?n)[Start] }") - base == 3);
}

TEST_CASE("graph rules: writing the source changes the next extraction") {
    // the hall becomes room: A, the hall, and B merge into one region
    auto lv = generate(ts::stamped(dungeon, "level", {"R R H R"},
        "once { layout { ?n[H] } => level(?n)[R] }"));
    CHECK(ts::show(lv, "level") == "RRRR");
    CHECK(lv.graph("layout").node_count() == 1);
    // a step mode re-extracts after every application: prune to one room
    auto pruned = generate(ts::stamped(dungeon, "level", {"R H R H R H R H R"},
        "grow { map { ?n } when (degree(?n) == 1) => level(?n)[W] }"));
    CHECK(pruned.graph("map").node_count() == 1);
    // a batch sees one snapshot: only the two ends
    auto once = generate(ts::stamped(dungeon, "level", {"R H R H R H R H R"},
        "everywhere { map { ?n } when (degree(?n) == 1) => level(?n)[W] }"));
    CHECK(ts::show(once, "level") == "WHRHRHRHW");
}

TEST_CASE("graph rules: number-grid sources and value()") {
    std::vector<std::string> zones = {"1 1 2 2 3", "1 1 2 2 3"};
    auto lv = generate(ts::stamped(dungeon, "ids", zones,
        "everywhere { areas { ?z } => ids(?z)[ (value(?z) * 10) ] }"));
    CHECK(ts::show(lv, "ids") == "1010202030/1010202030");
    auto tested = generate(ts::stamped(dungeon, "ids", zones,
        "everywhere { areas { ?z[2] ?w -- ?z } when (value(?w) > 2) => items(?w)[chest] }"));
    CHECK(ts::show(tested, "items") == "....c/....c");
    // in a grid rule too, through a graph pattern
    auto grid = generate(ts::stamped(dungeon, "ids", zones,
        "everywhere { areas[?z] when (value(?z) == 2) => rooms[Boss] }"));
    CHECK(ts::show(grid, "rooms") == "..BB./..BB.");
}

TEST_CASE("graph rules: ordered sub-rules on different graphs") {
    // step modes take the highest-priority sub-rule while it has candidates
    auto lv = generate(ts::stamped(dungeon, "level", {"R H R"},
        "grow(3) { ordered\n"
        "    map { ?n } when (size(?n) == 1) => level(?n)[W]\n"
        "    layout { ?n[H] } => rooms(?n)[Boss]\n"
        "}"));
    CHECK(ts::show(lv, "level") == "WHW");
    CHECK(ts::show(lv, "rooms") == ".B.");
}

TEST_CASE("graph rules: highlights name the assigned regions and their writes") {
    auto gen = ts::make(ts::stamped(dungeon, "level", {"R R H R"},
        "once { map { ?n } when (size(?n) == 2) => items(?n)[chest] }"));
    REQUIRE(static_cast<bool>(gen));
    auto run = gen.run(gen.sequence("main"), 1, ls::step_mode::application, ls::observe::on);
    REQUIRE(run.step());   // resize
    REQUIRE(run.step());   // the stamp
    REQUIRE(run.step());   // its statement boundary
    REQUIRE(run.step());   // the graph rule's one application
    auto hl = run.highlights();
    REQUIRE(hl.size() == 4);
    for (int i = 0; i < 2; ++i) {
        CHECK(hl[i].what == ls::cell_highlight::kind::match);
        CHECK(hl[i].layer == 0);   // level, the graph's source
        CHECK(hl[i].x == i);
        CHECK(hl[i + 2].what == ls::cell_highlight::kind::write);
        CHECK(hl[i + 2].layer == 2);   // items
        CHECK(hl[i + 2].x == i);
    }
}

// ── farthest ─────────────────────────────────────────────────────────────────

TEST_CASE("graph rules: farthest arguments") {
    // check 53: a node, then two targets
    CHECK(rule_with("plan { ?n } when (farthest(rooms[Start], rooms[Start], rooms[Hall])) => decor(?n)[stall]")
              .has_error("takes a node first"));
    CHECK(rule_with("plan { ?n } when (farthest(?n, rooms[Start])) => decor(?n)[stall]")
              .has_error("'farthest' takes a node and two targets"));
    CHECK(rule_with("plan { ?n } when (farthest(?n, rooms[Start], 3)) => decor(?n)[stall]")
              .has_error("is a node or a selector"));
    CHECK(rule_with("plan { ?n } when (farthest(?n, rooms[Start], zones[Hall])) => decor(?n)[stall]")
              .has_error("is an integer, '.', or '!.'"));
    CHECK(rule_with("plan[?n] areas[?z] when (farthest(?n, rooms[Start], ?z)) => decor[stall]")
              .has_error("a node of a different graph"));
    CHECK(rule_with("plan { ?n } when (farthest(?n, rooms[Start], rooms[Hall]) > 0) => decor(?n)[stall]")
              .has_error("relational operator"));
    for (char const* body : {
             "plan { ?n } when (farthest(?n, rooms[Start], decor[stall])) => decor(?n)[stall]",
             "plan { ?s ?n } when (farthest(?n, ?s, zones[!.])) => decor(?n)[stall]",
             "plan { ?n ?u } when (farthest(?n, rooms[Start], ?u) || farthest(?u, ?n, ?n)) => decor(?n)[stall]",
         }) {
        INFO(body);
        compile_result r = rule_with(body);
        INFO(r.diags.format_all());
        CHECK(r.ok);
    }
}

TEST_CASE("graph rules: farthest picks the nodes of u farthest from t") {
    // Nodes 0 - 1 - 2 at x 0, 2, 4, then 3 - 4 at x 6, 8, cut off from them.
    std::vector<std::string> const split = {"R H R H R . R H R"};
    auto mark = [&](std::string const& rooms, std::string const& items, std::string const& guard) {
        return ts::show(generate(ts::stamped(dungeon, "level", split,
            (rooms.empty() ? "" : ts::stamp("rooms", {rooms}) + "\n    ") +
            (items.empty() ? "" : ts::stamp("items", {items}) + "\n    ") +
            "everywhere { map { ?n } when (" + guard + ") => ids(?n)[1] }")), "ids");
    };
    std::string const start = "Start . . . . . . . .";
    std::string const far   = "farthest(?n, rooms[Start], items[chest])";
    // a unique maximum; node 1, not in u, is never farthest
    CHECK(mark(start, ". . chest . chest . . . .", far) == "....1....");
    CHECK(mark(start, ". . chest . . . . . .", far) == "..1......");
    // unreachable nodes of u never win, though their distance is the sentinel
    CHECK(mark(start, ". . chest . . . . . chest", far) == "..1......");
    CHECK(mark(start, ". . . . . . chest . chest", far) == ".........");
    // u as the source selector: every node; ties all match
    CHECK(mark(". . Start . . . . . .", "", "farthest(?n, rooms[Start], level[R])") == "1...1....");
    // t or u empty
    CHECK(mark("", ". . chest . . . . . .", far) == ".........");
    CHECK(mark(start, "", far) == ".........");
    // n in t: true only when every reachable node of u is in t
    CHECK(mark(start, "", "farthest(?n, rooms[Start], rooms[Start])") == "1........");
    CHECK(mark(start, "", "farthest(?n, rooms[Start], level[R])") == "....1....");
    CHECK(mark("", "", "farthest(?n, level[R], level[R])") == "1.1.1.1.1");
}

TEST_CASE("graph rules: farthest with node targets") {
    std::vector<std::string> const split = {"R H R H R . R H R"};
    // t a node: the room farthest from the start room
    auto lv = generate(ts::stamped(dungeon, "level", split,
        ts::stamp("rooms", {"Start . . . . . . . ."}) +
        "\n    once { map { ?s[R] ?n } when (some(?s, rooms[Start]) && farthest(?n, ?s, level[R])) "
        "=> ids(?n)[7] }"));
    CHECK(ts::show(lv, "ids") == "....7....");
    // u a node: farthest(n, t, n) is reachable(n, t)
    auto self = generate(ts::stamped(dungeon, "level", split,
        ts::stamp("rooms", {"Start . . . . . . . ."}) +
        "\n    everywhere { map[?n] when (farthest(?n, rooms[Start], ?n)) => ids[1] }"));
    CHECK(ts::show(self, "ids") == "1.1.1....");
}

TEST_CASE("graph rules: farthest ties are separate candidates under once") {
    // Start in the middle of the chain: nodes 0 and 2 tie at distance 1
    auto gen = ts::make(ts::stamped(dungeon, "level", chain,
        ts::stamp("rooms", {". . Start . ."}) +
        "\n    once { map { ?n } when (farthest(?n, rooms[Start], level[R])) => items(?n)[chest] }"));
    REQUIRE(static_cast<bool>(gen));
    int left = 0, right = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        auto lv = gen.generate(gen.sequence("main"), seed);
        left  += !lv["items"].is_empty(0, 0);
        right += !lv["items"].is_empty(4, 0);
        CHECK(lv["items"].is_empty(2, 0));
    }
    CHECK(left + right == 200);
    CHECK(left > 70);    // ~100 of 200
    CHECK(right > 70);
}

TEST_CASE("graph rules: farthest sees a selector grid written mid-sequence") {
    // one rule, so one cached maximum, which must key on u's grid version
    std::string const far = "rule far { map { ?n } when (farthest(?n, rooms[Start], items[chest])) "
                            "=> ids(?n)[ (x) ] }\n";
    auto lv = generate(ts::stamped(dungeon + far, "level", chain,
        ts::stamp("rooms", {"Start . . . ."}) + "\n    " +
        ts::stamp("items", {". . chest . ."}) + "\n    everywhere far\n    " +
        ts::stamp("items", {". . chest . chest"}) + "\n    everywhere far"));
    CHECK(ts::show(lv, "ids") == "..2.4");
}
