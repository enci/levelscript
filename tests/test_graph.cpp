// Graph layers: declarations, extraction read through level::graph, and the
// checks on graph entries.
#include "test_support.hpp"
#include "lexer.hpp"
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

using ts::compile_result;
using ts::make;

namespace {

// One inline rule the size of the whole grid, which fits only at (0, 0), so
// `once` writes exactly `rows` into `grid`. Each row is space-separated cells.
std::string stamp(std::string const& grid, std::vector<std::string> const& rows) {
    std::string any, cells;
    for (size_t r = 0; r < rows.size(); ++r) {
        std::string stars;
        for (size_t i = 0, n = 0; i <= rows[r].size(); ++i)
            if (i == rows[r].size() || rows[r][i] == ' ') {
                if (n > 0) stars += stars.empty() ? "*" : " *";
                n = 0;
            } else {
                ++n;
            }
        any   += (r ? ", " : "") + stars;
        cells += (r ? ", " : "") + rows[r];
    }
    return "once { " + grid + "[" + any + "] => " + grid + "[" + cells + "] }";
}

int width_of(std::vector<std::string> const& rows) {
    int w = 1;
    for (char c : rows[0]) w += c == ' ';
    return w;
}

// A program with `decls`, whose main resizes to fit `rows` and stamps them
// into `grid`, then runs `after`.
std::string stamped(std::string const& decls, std::string const& grid,
                    std::vector<std::string> const& rows, std::string const& after = "") {
    return decls + "\nsequence main {\n    resize(" + std::to_string(width_of(rows)) + ", " +
           std::to_string(rows.size()) + ")\n    " + stamp(grid, rows) + "\n    " + after +
           "\n}\n";
}

ls::level generate(std::string const& src) {
    auto gen = make(src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    return gen.generate(gen.sequence("main"), 1);
}

std::vector<int> ids(std::initializer_list<int> v) { return v; }

// Tag decls shared by the extraction tests.
const std::string algo = R"(
tag algo { R, H, D, W, X }
layers {
    level:  grid of algo
    map:    graph of level(nodes=R, edges=H)
    layout: graph of level(nodes=R|H)
    walls:  graph of level(nodes=R|X, edges=H|D)
    whole:  graph of level
}
)";

struct files {
    std::map<std::string, std::string> src;
    ls::generator compile(std::string const& root) const {
        return ls::generator::compile(src.at(root), root,
            [this](std::string const& path, std::string const&) -> std::optional<ls::module_source> {
                auto it = src.find(path);
                if (it == src.end()) return std::nullopt;
                return ls::module_source{path, it->second};
            });
    }
};

}  // namespace

// ── lexer and parser ─────────────────────────────────────────────────────────

TEST_CASE("graph: 'graph' is a keyword, 'graphs' an identifier") {
    ls::diagnostics d;
    auto t = ls::lex("graph graphs", "t", d);
    CHECK(t[0].is(ls::token_type::kw_graph));
    CHECK(t[1].is(ls::token_type::ident));

    CHECK(compile_result("tag t { graph }").has_error("reserved keyword"));
    CHECK(compile_result("tag t { a }\nlayers { graph: grid of t }").has_error("reserved keyword"));
    CHECK(compile_result("params { graph: number = 1 }").has_error("reserved keyword"));
    CHECK(compile_result("tag t { graphs }\nlayers { graphs2: grid of t }").ok);
}

TEST_CASE("graph: the three entry forms parse and compile") {
    compile_result r(R"(
tag algo { R, H, W }
layers {
    level: grid of algo
    ids:   grid of number
    a: graph of level
    b: graph of level(nodes=R)
    c: graph of level(nodes=R, edges=H)
    d: graph of level(nodes=R|H, edges=W)
    e: graph of level(nodes=!W)
    f: graph of level(nodes=!.)
    g: graph of ids
}
)");
    INFO(r.diags.format_all());
    REQUIRE(r.ok);
    REQUIRE(r.prog.graphs.size() == 7);
    CHECK(r.prog.layers.size() == 2);   // graphs are not layers of the stack
    int64_t R = ls::tag_bit(0), H = ls::tag_bit(1), W = ls::tag_bit(2);
    CHECK(r.prog.graphs[0].nodes == (R | H | W));
    CHECK(r.prog.graphs[1].nodes == R);
    CHECK(r.prog.graphs[2].nodes == R);
    CHECK(r.prog.graphs[2].edges == H);
    CHECK(r.prog.graphs[3].nodes == (R | H));
    CHECK(r.prog.graphs[4].nodes == (R | H));
    CHECK(r.prog.graphs[5].nodes == (R | H | W));
    CHECK(r.prog.graphs[6].source == 1);
}

TEST_CASE("graph: a graph may name a grid declared after it") {
    CHECK(compile_result("tag t { a }\nlayers {\n    m: graph of g\n    g: grid of t\n}").ok);
}

TEST_CASE("graph: 'nodes' and 'edges' stay usable as names") {
    compile_result r(R"(
tag nodes { edges, nodes }
layers {
    edges: grid of nodes
    g:     graph of edges(nodes=nodes, edges=edges)
}
params { e: number = 1 }
rule nodes_rule { edges[nodes] => edges[edges] }
)");
    INFO(r.diags.format_all());
    CHECK(r.ok);
}

TEST_CASE("graph: malformed entries are parse errors") {
    CHECK(compile_result("tag t { a }\nlayers { g: grid of t\n m: graph of }")
              .has_error("expected a grid name"));
    CHECK(compile_result("tag t { a }\nlayers { g: grid of t\n m: graph g }")
              .has_error("expected 'of'"));
    CHECK(compile_result("tag t { a }\nlayers { g: grid of t\n m: graph of g(nodes a) }")
              .has_error("expected '='"));
    CHECK(compile_result("tag t { a, b }\nlayers { g: grid of t\n m: graph of g(nodes=a | b) }")
              .has_error("expected ')'"));
    CHECK(compile_result("tag t { a }\nlayers { m: table of t }")
              .has_error("expected 'grid' or 'graph'"));
}

TEST_CASE("graph: the selector grammar") {
    // a selector parses only as a call argument, with exactly one cell
    auto parse_errors = [](std::string const& cell) {
        ls::diagnostics d;
        ls::parse("rule r { map[ (" + cell + " > 0) ] => g[a] }", "t", d);
        return d.has_errors();
    };
    CHECK_FALSE(parse_errors("distance(rooms[Start])"));
    CHECK_FALSE(parse_errors("neighbors(ids[3])"));
    CHECK_FALSE(parse_errors("neighbors(ids[!.])"));
    CHECK_FALSE(parse_errors("neighbors(rooms[A|!B])"));
    CHECK(parse_errors("distance(rooms[Start Boss])"));
    CHECK(parse_errors("distance(rooms[(x > 1)])"));
    CHECK(parse_errors("rooms[Start]"));
}

// ── checks on graph entries ──────────────────────────────────────────────────

TEST_CASE("graph: the source must be a grid") {
    // check 48
    std::string grids = "tag t { a }\nlayers {\n    g: grid of t\n";
    CHECK(compile_result(grids + "    m: graph of t\n}").has_error("built from tagset 't'"));
    CHECK(compile_result(grids + "    m: graph of g\n    n: graph of m\n}")
              .has_error("built from graph 'm'"));
    CHECK(compile_result(grids + "    m: graph of nope\n}").has_error("undeclared grid 'nope'"));
    CHECK(compile_result(grids + "    m: graph of g\n}").ok);
}

TEST_CASE("graph: the form of a graph entry") {
    // check 49
    std::string grids = "tag t { a, b }\nlayers {\n    g: grid of t\n    n: grid of number\n";
    CHECK(compile_result(grids + "    m: graph of g(node=a)\n}").has_error("unknown graph argument 'node'"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=a, nodes=b)\n}").has_error("'nodes' given twice"));
    CHECK(compile_result(grids + "    m: graph of g(edges=a)\n}").has_error("'edges' without 'nodes'"));
    CHECK(compile_result(grids + "    m: graph of n(nodes=.)\n}").has_error("takes no 'nodes' or 'edges'"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=a, edges=b)\n}").ok);
    CHECK(compile_result(grids + "    m: graph of n\n}").ok);
}

TEST_CASE("graph: the masks of a graph entry") {
    // check 50
    std::string grids = "tag t { a, b, c, ab = a | b }\ntag u { z }\nlayers {\n    g: grid of t\n";
    CHECK(compile_result(grids + "    m: graph of g(nodes=z)\n}").has_error("unknown tag value 'z'"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=.)\n}").has_error("'nodes' mask of graph 'm' selects no value"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=a, edges=.)\n}").has_error("'edges' mask of graph 'm' selects no value"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=a, edges=a|c)\n}").has_error("share 'a'"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=ab, edges=b)\n}").has_error("share 'b'"));
    CHECK(compile_result(grids + "    m: graph of g(nodes=!c, edges=c)\n}").ok);
}

TEST_CASE("graph: graphs share the layer namespace") {
    // checks 42 and 12
    std::string grids = "tag t { a }\nlayers {\n    g: grid of t\n";
    CHECK(compile_result(grids + "    g: graph of g\n}").has_error("duplicate layer 'g'"));
    CHECK(compile_result(grids + "    m: graph of g\n    m: graph of g\n}").has_error("duplicate layer 'm'"));
    CHECK(compile_result(grids + "    x: graph of g\n}").has_error("collides with a reserved"));
    CHECK(compile_result(grids + "    m: graph of g\n}\nparams { m: number = 1 }")
              .has_error("collides with a graph"));

    files f;
    f.src["a.lvs"] = "tag t { a }\nlayers { g: grid of t\n m: grid of t }\n";
    f.src["b.lvs"] = "use \"a.lvs\"\nlayers { m: graph of g }\n";
    CHECK(f.compile("b.lvs").error().find("duplicate layer 'm'") != std::string::npos);
}

TEST_CASE("graph: the source must be visible") {
    // check 8
    files f;
    f.src["a.lvs"] = "tag t { a }\nlayers { g: grid of t }\n";
    f.src["b.lvs"] = "use \"a.lvs\"\n";
    f.src["c.lvs"] = "use \"b.lvs\"\nlayers { m: graph of g }\n";
    CHECK(f.compile("c.lvs").error().find("grid 'g' is not visible here") != std::string::npos);
}

// ── extraction ───────────────────────────────────────────────────────────────

TEST_CASE("graph: value regions on a number grid") {
    std::string decls = "layers {\n    ids: grid of number\n    g: graph of ids\n}";
    auto lv = generate(stamped(decls, "ids", {"7 7 . 7"}));
    auto g = lv.graph("g");
    REQUIRE(g.node_count() == 2);   // one value, two patches
    CHECK(g.node_at(0, 0) == 0);
    CHECK(g.node_at(1, 0) == 0);
    CHECK(g.node_at(2, 0) == -1);
    CHECK(g.node_at(3, 0) == 1);
    CHECK(g.node_size(0) == 2);
    CHECK(g.neighbors(0).empty());

    auto g2 = generate(stamped(decls, "ids", {"7 8"})).graph("g");
    REQUIRE(g2.node_count() == 2);
    CHECK(g2.neighbors(0) == ids({1}));
    CHECK(g2.neighbors(1) == ids({0}));

    auto g3 = generate(stamped(decls, "ids", {"0 0", "0 1"})).graph("g");
    CHECK(g3.node_count() == 2);   // 0 is a value, not empty
    CHECK(g3.node_size(0) == 3);
}

TEST_CASE("graph: empty cells separate") {
    std::string decls = "layers {\n    ids: grid of number\n    g: graph of ids\n}";
    auto g = generate(stamped(decls, "ids", {"1 . 2"})).graph("g");
    REQUIRE(g.node_count() == 2);
    CHECK(g.neighbors(0).empty());
    CHECK(g.neighbors(1).empty());
}

TEST_CASE("graph: tag values split by exact equality") {
    // a union cell holds a different value from either member
    auto g = generate(stamped(algo, "level", {"R|W R"})).graph("whole");
    REQUIRE(g.node_count() == 2);
    CHECK(g.neighbors(0) == ids({1}));
}

TEST_CASE("graph: nodes=M excludes and splits") {
    auto g = generate(stamped(algo, "level", {"R R H R W R"})).graph("layout");
    REQUIRE(g.node_count() == 4);   // R, H, R, R
    CHECK(g.node_size(0) == 2);
    CHECK(g.node_at(4, 0) == -1);   // W is outside the mask, and separates
    CHECK(g.neighbors(0) == ids({1}));
    CHECK(g.neighbors(1) == ids({0, 2}));
    CHECK(g.neighbors(2) == ids({1}));
    CHECK(g.neighbors(3).empty());
}

TEST_CASE("graph: a connector joins the nodes it touches") {
    auto lv = generate(stamped(algo, "level", {"R H R"}));
    auto g = lv.graph("map");
    REQUIRE(g.node_count() == 2);
    CHECK(g.node_at(1, 0) == -1);   // a connector cell is no node
    CHECK(g.neighbors(0) == ids({1}));

    // a connector region touching three rooms forms a triangle
    auto t = generate(stamped(algo, "level", {"R H R", ". H .", ". R ."})).graph("map");
    REQUIRE(t.node_count() == 3);
    CHECK(t.neighbors(0) == ids({1, 2}));
    CHECK(t.neighbors(1) == ids({0, 2}));
    CHECK(t.neighbors(2) == ids({0, 1}));
}

TEST_CASE("graph: connectors don't split by value") {
    auto g = generate(stamped(algo, "level", {"R H D H R"})).graph("walls");
    REQUIRE(g.node_count() == 2);
    CHECK(g.neighbors(0) == ids({1}));
}

TEST_CASE("graph: dangling and parallel connectors") {
    auto dangling = generate(stamped(algo, "level", {"R H H", ". . .", "R . ."})).graph("map");
    REQUIRE(dangling.node_count() == 2);
    CHECK(dangling.neighbors(0).empty());

    // two corridors between the same rooms are one edge
    auto parallel = generate(stamped(algo, "level", {"R H R", "R . R", "R H R"})).graph("map");
    REQUIRE(parallel.node_count() == 2);
    CHECK(parallel.neighbors(0) == ids({1}));
}

TEST_CASE("graph: direct contact joins nodes in the connector form") {
    auto g = generate(stamped(algo, "level", {"R X . R"})).graph("walls");
    REQUIRE(g.node_count() == 3);
    CHECK(g.neighbors(0) == ids({1}));
    CHECK(g.neighbors(2).empty());
}

TEST_CASE("graph: regions are 4-connected") {
    auto g = generate(stamped(algo, "level", {"R .", ". R"})).graph("whole");
    REQUIRE(g.node_count() == 2);   // diagonal cells are two nodes
    CHECK(g.neighbors(0).empty());  // and diagonal contact joins nothing
}

TEST_CASE("graph: node ids follow the row-major order of first cells") {
    // row 0 is scanned first: the right R (2,0) comes before the H region (1,1)
    auto g = generate(stamped(algo, "level", {"R . R", "R H H"})).graph("layout");
    REQUIRE(g.node_count() == 3);
    CHECK(g.node_at(0, 0) == 0);
    CHECK(g.node_at(2, 0) == 1);
    CHECK(g.node_at(1, 1) == 2);
    CHECK(g.node_at(0, 1) == 0);
    // a region whose first cell lies right of another's on the same row
    auto u = generate(stamped(algo, "level", {". R", "R R"})).graph("layout");
    REQUIRE(u.node_count() == 1);
    auto v = generate(stamped(algo, "level", {". H", "R H"})).graph("layout");
    REQUIRE(v.node_count() == 2);
    CHECK(v.node_at(1, 0) == 0);
    CHECK(v.node_at(0, 1) == 1);
}

TEST_CASE("graph: the API is total") {
    auto lv = generate(stamped(algo, "level", {"R H R"}));
    auto g = lv.graph("map");
    CHECK(g.node_at(-1, 0) == -1);
    CHECK(g.node_at(3, 0) == -1);
    CHECK(g.node_at(0, 1) == -1);
    CHECK(g.node_size(-1) == 0);
    CHECK(g.node_size(2) == 0);
    CHECK(g.neighbors(5).empty());

    auto bad = lv.graph("nope");
    CHECK(bad.node_count() == 0);
    CHECK(bad.node_at(0, 0) == -1);
    CHECK(bad.node_size(0) == 0);
    CHECK(bad.neighbors(0).empty());
    CHECK(lv.graph("level").node_count() == 0);   // a grid is no graph
    CHECK(ls::level{}.graph("map").node_count() == 0);

    // a graph outlives its level
    ls::graph kept;
    {
        auto tmp = generate(stamped(algo, "level", {"R H R"}));
        kept = tmp.graph("map");
    }
    CHECK(kept.node_count() == 2);
}

TEST_CASE("graph: graphs are not layers") {
    auto lv = generate(stamped(R"(
tag algo { R }
layers {
    a: grid of algo
    m: graph of a
    b: grid of number
}
)", "a", {"R"}));
    CHECK(lv.layer_count() == 2);
    CHECK(lv.layer_name(0) == "a");
    CHECK(lv.layer_name(1) == "b");
    CHECK(lv["m"].name().empty());
}

TEST_CASE("graph: geometric operations need no special case") {
    auto up = generate(stamped(algo, "level", {"R H R"}, "upscale(2, 2)")).graph("map");
    REQUIRE(up.node_count() == 2);
    CHECK(up.node_size(0) == 4);
    CHECK(up.node_size(1) == 4);
    CHECK(up.neighbors(0) == ids({1}));

    // mirror folds the left half onto the right: R H . . -> R H H R
    auto mir = generate(stamped(algo, "level", {"R H . ."}, "mirror(horizontal)")).graph("map");
    REQUIRE(mir.node_count() == 2);
    CHECK(mir.neighbors(0) == ids({1}));

    auto trimmed = generate(stamped(algo, "level", {". . .", ". R H", ". . R"}, "trim()"));
    CHECK(trimmed.width() == 2);
    auto tr = trimmed.graph("map");
    REQUIRE(tr.node_count() == 2);
    CHECK(tr.node_at(0, 0) == 0);
    CHECK(tr.node_at(1, 1) == 1);
    CHECK(tr.neighbors(0) == ids({1}));
}

TEST_CASE("graph: an empty stack has no nodes") {
    auto gen = make(algo + "sequence main { }\nsequence blank { resize(4, 4) }\n");
    REQUIRE(static_cast<bool>(gen));
    CHECK(gen.generate(gen.sequence("main"), 1).graph("map").node_count() == 0);
    auto blank = gen.generate(gen.sequence("blank"), 1).graph("whole");
    CHECK(blank.node_count() == 0);
    CHECK(blank.node_at(0, 0) == -1);
}

// ── graph patterns ───────────────────────────────────────────────────────────

namespace {

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

// One character per cell of `grid`, rows joined by '/': '.' for an empty
// cell, else the first letter of its value name (number grids: the number).
std::string show(ls::level const& lv, std::string const& name) {
    auto g = lv[name];
    std::string out;
    for (int y = 0; y < lv.height(); ++y) {
        if (y) out += '/';
        for (int x = 0; x < lv.width(); ++x) {
            if (g.is_empty(x, y)) out += '.';
            else if (g.is_number()) out += std::to_string(g.at(x, y));
            else out += g.valueName(g.at(x, y))[0];
        }
    }
    return out;
}

}  // namespace

TEST_CASE("graph: a graph pattern paints whole regions") {
    auto lv = generate(stamped(dungeon, "level", abc,
                               "everywhere { map[ (degree() == 1) ] => rooms[DeadEnd] }"));
    CHECK(show(lv, "rooms") == "DD....D/DD.....");
}

TEST_CASE("graph: off-node positions fail, '*' matches everywhere") {
    auto lv = generate(stamped(dungeon, "level", abc,
                               "everywhere { map[ (degree() >= 0) ] => items[chest] }"));
    CHECK(show(lv, "items") == "cc.cc.c/cc.cc..");
    auto all = generate(stamped(dungeon, "level", abc, "everywhere { map[*] => items[chest] }"));
    CHECK(show(all, "items") == "ccccccc/ccccccc");
}

TEST_CASE("graph: size()") {
    auto lv = generate(stamped(dungeon, "ids",
                               {"1 1 2 2 2 3 3 3 3", "1 1 2 2 2 3 3 3 3", ". . 2 2 2 3 3 3 3"},
                               "everywhere { areas[ (size() > 8) ] => items[chest] }"));
    CHECK(show(lv, "items") == "..ccccccc/..ccccccc/..ccccccc");
}

TEST_CASE("graph: a selector selects a node through any of its cells") {
    // one Boss cell in room B
    auto lv = generate(stamped(dungeon, "level", abc,
        stamp("rooms", {". . . . Boss . .", ". . . . . . ."}) +
        "\n    everywhere { map[ (neighbors(rooms[Boss]) > 0) ] => items[chest] }"));
    CHECK(show(lv, "items") == "cc....c/cc.....");
}

TEST_CASE("graph: selectors on number grids") {
    std::string ids = stamp("ids", {". . . 3 . . .", ". . . . . . ."});
    auto run = [&](std::string const& cond) {
        return show(generate(stamped(dungeon, "level", abc, ids +
            "\n    everywhere { map[ (" + cond + ") ] => items[chest] }")), "items");
    };
    CHECK(run("neighbors(ids[3]) > 0")  == "cc....c/cc.....");
    CHECK(run("neighbors(ids[!.]) > 0") == "cc....c/cc.....");
    CHECK(run("neighbors(ids[.]) == 2") == "...cc../...cc..");   // every room has an empty cell
    CHECK(run("neighbors(ids[4]) > 0")  == "......./.......");
}

TEST_CASE("graph: distance()") {
    // a chain of three rooms and an island; Start in the first room
    std::vector<std::string> chain = {"R H R H R . R"};
    auto run = [&](std::string const& start, std::string const& cond) {
        return show(generate(stamped(dungeon, "level", chain, stamp("rooms", {start}) +
            "\n    everywhere { map[ (" + cond + ") ] => items[chest] }")), "items");
    };
    std::string s = "Start . . . . . .";
    CHECK(run(s, "distance(rooms[Start]) == 0") == "c.....c");   // the island reads as 0
    CHECK(run(s, "distance(rooms[Start]) == 1") == "..c....");
    CHECK(run(s, "distance(rooms[Start]) == 2") == "....c..");
    CHECK(run(s, "distance(rooms[Start]) != .") == "c.c.c..");
    CHECK(run(s, "distance(rooms[Start]) == .") == "......c");
    CHECK(run(s, ". != distance(rooms[Start])") == "c.c.c..");
    CHECK(run(s, "distance(rooms[Start]) + 1 == 1") == "c.....c");
    // with no Start, every node reads 0, and the guarded form matches none
    std::string none = ". . . . . . .";
    CHECK(run(none, "distance(rooms[Start]) == 0") == "c.c.c.c");
    CHECK(run(none, "distance(rooms[Start]) != . && distance(rooms[Start]) == 0") == ".......");
}

TEST_CASE("graph: multi-cell graph patterns and variants") {
    std::vector<std::string> fix = {"R R R H R", "R R R . ."};
    auto lv = generate(stamped(dungeon, "level", fix,
        "everywhere { layout[ (size() > 4) (size() == 1) ] => items[chest *] }"));
    CHECK(show(lv, "items") == "..c../.....");

    std::vector<std::string> up = {". H .", "R R R", "R R R"};
    std::string rule = " { layout[ (size() > 4) (size() == 1) ] => items[chest *] }";
    CHECK(show(generate(stamped(dungeon, "level", up, "everywhere" + rule)), "items") ==
          ".../.../...");
    CHECK(show(generate(stamped(dungeon, "level", up, "everywhere (rotation=all)" + rule)),
               "items") == ".../.c./...");
}

TEST_CASE("graph: graph patterns conjoin with grid patterns") {
    // one chest already sits in a dead end; scatter adds two, never on it
    auto lv = generate(stamped(dungeon, "level", abc,
        stamp("items", {"chest . . . . . .", ". . . . . . ."}) +
        "\n    scatter(2) { map[ (degree() == 1) ] items[.] => items[chest] }"));
    std::string items = show(lv, "items");
    CHECK(items[0] == 'c');
    int chests = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] != 'c') continue;
        ++chests;
        CHECK(std::string("cc....c/cc.....")[i] == 'c');   // dead-end cells only
    }
    CHECK(chests == 3);
}

TEST_CASE("graph: pattern variables in graph cells") {
    auto run = [&](std::string const& ids) {
        return show(generate(stamped(dungeon, "level", abc, stamp("ids", {ids, ". . . . . . ."}) +
            "\n    everywhere { ids[?n] map[ (size() == ?n) ] => items[chest] }")), "items");
    };
    CHECK(run("4 . . 4 . . 4") == "c..c.../.......");   // C has size 1, not 4
    CHECK(run(". . . . . . 1") == "......c/.......");
}

TEST_CASE("graph: batch, step, and settle see the graph of their snapshot") {
    // five one-cell rooms in a chain; prune walls a dead end
    std::vector<std::string> chain = {"R H R H R H R H R"};
    std::string prune = "rule prune { map[ (degree() == 1) ] => level[W] }\n";
    auto rooms = [&](std::string const& stmt) {
        auto lv = generate(stamped(dungeon + prune, "level", chain, stmt));
        return show(lv, "level");
    };
    // one pass: only the two ends are dead ends in the snapshot
    CHECK(rooms("everywhere prune") == "WHRHRHRHW");
    // each sweep sees the previous one: ends, then the next pair, then stable
    CHECK(rooms("settle prune") == "WHWHRHWHW");
    // steps see every prior application: pruning runs on to a single room
    auto lv = generate(stamped(dungeon + prune, "level", chain, "grow prune"));
    CHECK(lv.graph("map").node_count() == 1);

    // a pass's other sub-rules don't change the degrees it sees; the next
    // statement sees the new graph
    auto both = generate(stamped(dungeon, "level", {"R H R H R"},
        "everywhere { all map[ (degree() == 2) ] => items[chest]\n"
        "                 level[H] => level[W] }\n"
        "    everywhere { map[ (degree() == 0) ] => rooms[Boss] }"));
    CHECK(show(both, "items") == "..c..");
    CHECK(show(both, "rooms") == "B.B.B");
}

TEST_CASE("graph: selections follow writes to the selector's grid") {
    std::vector<std::string> chain = {"R H R H R"};
    // between statements: Start moves from the first room to the last, and
    // one rule reads it before and after
    std::string far = "rule far { map[ (distance(rooms[Start]) == 2) ] items[.] => items[chest] }\n";
    auto lv = generate(stamped(dungeon + far, "level", chain, stamp("rooms", {"Start . . . ."}) + R"(
    everywhere far
    once { rooms[Start] => rooms[.] }
    once { where[ (x == 4) ] => rooms[Start] }
    everywhere far)"));
    CHECK(show(lv, "items") == "c...c");

    // between steps: Start spreads one room per step
    auto grown = generate(stamped(dungeon, "level", chain, stamp("rooms", {"Start . . . ."}) +
        "\n    grow { map[ (distance(rooms[Start]) == 1) ] rooms[.] => rooms[Start] }"));
    CHECK(show(grown, "rooms") == "S.S.S");
}

TEST_CASE("graph: off-node graph cells draw nothing") {
    std::string after = "\n    scatter(3) { level[R] items[.] => items[chest] }";
    auto items = [&](std::string const& first) {
        return show(generate(stamped(dungeon, "level", abc, first + after)), "items");
    };
    // the cell sees no node on H and empty cells: no draws, so the scatter is unchanged
    CHECK(items("everywhere { level[!R] map[ (random(0, 9) > 100) ] => items[.] }") == items(""));
}

TEST_CASE("graph: graph cells evaluate after bare cells") {
    std::string after = "\n    scatter(3) { level[R] items[.] => items[chest] }";
    auto items = [&](std::string const& first) {
        return show(generate(stamped(dungeon, "level", abc, first + after)), "items");
    };
    std::string base = items("");
    // rejected by its bare cell everywhere: the graph cell never draws
    CHECK(items("everywhere { items[chest] map[ (random(0, 9) > 100) ] => items[.] }") == base);
    // reached: it draws, and the scatter's draws move
    std::string drawn = items("everywhere { items[.] map[ (random(0, 9) > 100) ] => items[.] }");
    CHECK(drawn != base);
}

TEST_CASE("graph: a graph cell highlights its source layer") {
    auto gen = make(stamped(dungeon, "level", {"R H R"},
                            "once { map[ (degree() == 1) ] => items[chest] }"));
    REQUIRE(static_cast<bool>(gen));
    auto run = gen.run(gen.sequence("main"), 1, ls::step_mode::application, ls::observe::on);
    REQUIRE(run.step());   // resize
    REQUIRE(run.step());   // the stamp
    REQUIRE(run.step());   // the stamp's statement boundary
    REQUIRE(run.step());   // the graph rule's one application
    auto hl = run.highlights();
    REQUIRE(hl.size() == 2);
    CHECK(hl[0].what == ls::cell_highlight::kind::match);
    CHECK(hl[0].layer == 0);   // level, the graph's source
    CHECK(hl[1].what == ls::cell_highlight::kind::write);
    CHECK(hl[1].layer == 2);   // items
    CHECK(hl[0].x == hl[1].x);
    CHECK(hl[0].y == hl[1].y);
}

TEST_CASE("graph: graphs across modules") {
    files f;
    f.src["schema.lvs"] = "tag algo { R, H }\nlayers { level: grid of algo }\n";
    f.src["graphs.lvs"] = "use \"schema.lvs\"\nlayers { map: graph of level(nodes=R, edges=H) }\n"
                          "sequence carve {\n    resize(3, 1)\n    " + stamp("level", {"R H R"}) +
                          "\n}\n";
    // main sees the graph, but not its source
    f.src["main.lvs"] = "use \"graphs.lvs\"\nlayers { marks: grid of number }\n"
                        "sequence main {\n    once carve\n"
                        "    everywhere { map[ (degree() == 1) ] => marks[1] }\n}\n";
    auto gen = f.compile("main.lvs");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    auto lv = gen.generate(gen.sequence("main"), 1);
    CHECK(show(lv, "marks") == "1.1");
    CHECK(lv.graph("map").node_count() == 2);
}

// ── checks on graph patterns ─────────────────────────────────────────────────

namespace {

// `body` as the one sub-rule of a rule over the dungeon decls.
compile_result rule_with(std::string const& body) {
    return compile_result(dungeon + "rule r { " + body + " }\n");
}

}  // namespace

TEST_CASE("graph: misusing a graph name") {
    // check 51
    CHECK(rule_with("level[R] where[ (map == 1) ] => level[H]").has_error("cannot be read in an expression"));
    CHECK(rule_with("map[ (neighbors(map[R]) > 0) ] => level[H]").has_error("cannot be a selector's grid"));
    CHECK(rule_with("level[R] => map[ (degree() == 0) ]").has_error("graph 'map' cannot be written"));
    CHECK(compile_result(dungeon + "sequence s { path(from=R, to=R, into=map, write=R) }")
              .has_error("'map' is a graph layer"));
    CHECK(compile_result(dungeon + "sequence s { path(from=R, to=R, into=level, write=R, over=map) }")
              .has_error("'map' is a graph layer"));
}

TEST_CASE("graph: graph-pattern cells") {
    // checks 52 and 46
    CHECK(rule_with("map[R] => level[H]").has_error("graph-pattern cells must be '*'"));
    CHECK(rule_with("map[3] => level[H]").has_error("graph-pattern cells must be '*'"));
    CHECK(rule_with("ids[?v] map[?v] => level[H]").has_error("cannot be a bare pattern variable"));
    CHECK(rule_with("map[ (level == R) ] => level[H]").has_error("reads no grid"));
    CHECK(rule_with("map[ (level != .) ] => level[H]").has_error("reads no grid"));
    CHECK(rule_with("map[ (degree()) ] => level[H]").has_error("must evaluate to a boolean"));
    CHECK(rule_with("map[ (degree() == 1 && x > 0 && width > ?v) ] ids[?v] => level[H]").ok);
}

TEST_CASE("graph: graph functions only in graph-pattern cells") {
    // check 53
    CHECK(rule_with("level[R] where[ (degree() == 1) ] => level[H]").has_error("is a graph function"));
    CHECK(rule_with("level[R] => ids[ (size()) ]").has_error("is a graph function"));
    CHECK(rule_with("level[ (if(degree() > 0, R, H)) ] => level[H]").has_error("is a graph function"));
    CHECK(compile_result(dungeon + "sequence s { once { level[R] => level[H] } when (degree() > 0) }")
              .has_error("is a graph function"));
    CHECK(compile_result(dungeon + "sequence s { scatter(size()) { level[R] => level[H] } }")
              .has_error("is a graph function"));
    CHECK(compile_result(dungeon + "params { p = degree() }").has_error("is a graph function"));
    CHECK(rule_with("map[ (min(rooms[Boss], 1) > 0) ] => level[H]").has_error("a selector is valid only"));
    CHECK(rule_with("map[ (distance(3) > 0) ] => level[H]").has_error("takes a selector"));
    CHECK(rule_with("map[ (neighbors(ids[R]) > 0) ] => level[H]").has_error("is an integer, '.', or '!.'"));
    CHECK(rule_with("map[ (neighbors(rooms[3]) > 0) ] => level[H]").has_error("needs a number grid"));
    CHECK(rule_with("map[ (neighbors(rooms[Nope]) > 0) ] => level[H]").has_error("unknown tag value 'Nope'"));
    CHECK(rule_with("map[ (neighbors(nope[R]) > 0) ] => level[H]").has_error("undeclared grid 'nope'"));
}

TEST_CASE("graph: graph function arity") {
    // check 19
    CHECK(rule_with("map[ (degree(1) > 0) ] => level[H]").has_error("takes 0 argument(s), got 1"));
    CHECK(rule_with("map[ (distance() > 0) ] => level[H]").has_error("takes 1 argument(s), got 0"));
    CHECK(rule_with("map[ (neighbors(rooms[Boss], ids[1]) > 0) ] => level[H]")
              .has_error("takes 1 argument(s), got 2"));
}

TEST_CASE("graph: graph names and visibility in rules") {
    // check 8
    files f;
    f.src["a.lvs"] = "tag t { a }\nlayers { g: grid of t\n m: graph of g }\n";
    f.src["b.lvs"] = "use \"a.lvs\"\n";
    f.src["c.lvs"] = "use \"b.lvs\"\nlayers { n: grid of number }\n"
                     "rule r { m[ (degree() > 0) ] => n[1] }\n";
    CHECK(f.compile("c.lvs").error().find("graph 'm' is not visible here") != std::string::npos);

    f.src["d.lvs"] = "use \"a.lvs\"\nlayers { n: grid of number }\n"
                     "rule r { m[ (degree() > 0) ] => n[1] }\n";
    CHECK(static_cast<bool>(f.compile("d.lvs")));
    // a selector grid the module does not see
    f.src["x.lvs"] = "layers { n: grid of number }\n";
    f.src["y.lvs"] = "use \"x.lvs\"\n";
    f.src["f.lvs"] = "use \"a.lvs\"\nuse \"y.lvs\"\nlayers { h: grid of number }\n"
                     "rule r { m[ (neighbors(n[1]) > 0) ] => h[1] }\n";
    CHECK(f.compile("f.lvs").error().find("grid 'n' is not visible here") != std::string::npos);
}

TEST_CASE("graph: graph function names are not reserved") {
    compile_result r(R"(
tag t { degree, size }
layers {
    distance:  grid of t
    neighbors: grid of number
    g:         graph of distance(nodes=!.)
}
params { size2: number = 1 }
rule r { distance[size] g[ (size() > size2 && neighbors(neighbors[!.]) >= 0) ] => neighbors[1] }
)");
    INFO(r.diags.format_all());
    CHECK(r.ok);
}
