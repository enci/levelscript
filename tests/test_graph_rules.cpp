// Graph rules (section 5.13): a graph block of node variables, edges, and
// value tests matches a subgraph; node extents write whole regions. Also
// `value(n)`, the graph function graph rules rely on.
#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>
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
