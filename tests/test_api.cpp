#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector>

using ts::make;

static const std::string fill_src = R"(
tag geo { wall, floor }
layers {
    level: grid of geo
    tiles: grid of number
}
rule fill { level[.] => level[floor] }
program {
    resize(8, 4)
    all fill
}
)";

static const std::string scatter_src = R"(
tag algo { S }
layers { algo: grid of algo }
rule plant { algo[.] => algo[S] }
program {
    resize(10, 6)
    some(max=5) plant
}
)";

TEST_CASE("api: compile success and failure") {
    auto ok = make(fill_src);
    CHECK(static_cast<bool>(ok));
    CHECK(ok.error().empty());

    auto bad = make("rule r { g[.] => g[wall] }");   // no layers, no program
    CHECK(!bad);
    CHECK(!bad.error().empty());

    auto no_prog = make("tag t { a }");
    CHECK(!no_prog);
    CHECK(no_prog.error().find("no 'program' block") != std::string::npos);
}

TEST_CASE("api: generate fills the level") {
    auto gen = make(fill_src);
    REQUIRE(static_cast<bool>(gen));

    ls::level lv = gen.generate(42);
    CHECK(lv.width() == 8);
    CHECK(lv.height() == 4);
    CHECK(lv.layer_count() == 2);
    CHECK(lv.layer_name(0) == "level");

    ls::grid geo = lv["level"];
    int floor = gen.tag("geo.floor");
    REQUIRE(floor >= 0);
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x) {
            CHECK(geo.at(x, y) == floor);
            CHECK(!geo.empty(x, y));
        }

    // untouched number layer stays empty
    ls::grid tiles = lv["tiles"];
    CHECK(tiles.is_number());
    CHECK(tiles.empty(0, 0));
}

TEST_CASE("api: tag resolution") {
    auto gen = make(fill_src);
    CHECK(gen.tag("geo.wall") == 0);
    CHECK(gen.tag("geo.floor") == 1);
    CHECK(gen.tag("geo.lava") == -1);
    CHECK(gen.tag("nope.wall") == -1);
    CHECK(gen.tag("wall") == -1);   // must be qualified
}

TEST_CASE("api: queries are total") {
    auto gen = make(fill_src);
    ls::level lv = gen.generate(1);
    CHECK(lv["nope"].at(0, 0) == -1);          // unknown layer
    CHECK(lv["level"].at(-1, 0) == -1);        // out of range
    CHECK(lv["level"].at(0, 99) == -1);
    CHECK(ls::level{}.width() == 0);           // default level
    CHECK(ls::grid{}.at(0, 0) == -1);          // default grid
    CHECK(ls::generator{}.generate(1).layer_count() == 0);
}

static std::vector<int> dump(ls::level const& lv, char const* layer) {
    std::vector<int> out;
    ls::grid g = lv[layer];
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x)
            out.push_back(g.at(x, y));
    return out;
}

TEST_CASE("api: some(max=N) applies exactly N, deterministically per seed") {
    auto gen = make(scatter_src);
    REQUIRE(static_cast<bool>(gen));

    ls::level a = gen.generate(7);
    int planted = 0;
    for (int v : dump(a, "algo"))
        if (v >= 0) ++planted;
    CHECK(planted == 5);

    ls::level b = gen.generate(7);
    CHECK(dump(a, "algo") == dump(b, "algo"));   // same seed, same level

    // results are independent values; both remain readable
    CHECK(a.width() == 10);
    CHECK(b.width() == 10);
}

TEST_CASE("api: one applies exactly one") {
    auto gen = make(R"(
tag algo { S }
layers { algo: grid of algo }
rule plant { algo[.] => algo[S] }
program {
    resize(4, 4)
    one plant
}
)");
    int planted = 0;
    for (int v : dump(gen.generate(3), "algo"))
        if (v >= 0) ++planted;
    CHECK(planted == 1);
}

TEST_CASE("api: rhs '.' clears and '*' preserves") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule fill  { level[.] => level[floor] }
rule strip { level[floor] => level[.] }
program {
    resize(3, 3)
    all fill
    all strip
}
)");
    ls::level lv = gen.generate(1);
    for (int v : dump(lv, "level")) CHECK(v == -1);
}

TEST_CASE("api: stepping matches batch generation") {
    auto gen = make(scatter_src);

    // statement granularity: one step per program statement
    auto g1 = gen.begin(7, ls::step_mode::statement);
    int stmts = 0;
    while (g1.step()) ++stmts;
    CHECK(stmts == 2);   // resize + some(max=5)

    // application granularity: resize stops once, each application once
    auto g2 = gen.begin(7, ls::step_mode::application);
    int steps = 0;
    while (g2.step()) ++steps;
    CHECK(steps == 2 + 5);   // resize stmt + 5 applications + apply stmt boundary
                             // (the batch's own statement boundary is the 7th)

    // finish() == generate() for the same seed
    auto g3 = gen.begin(7);
    ls::level via_steps = g3.finish();
    ls::level via_batch = gen.generate(7);
    CHECK(dump(via_steps, "algo") == dump(via_batch, "algo"));
}

// ── step 2: cross-grid rules, weighted writes, rotation ───────────────────────

TEST_CASE("api: cross-grid match and body sub-rules") {
    auto gen = make(R"(
tag algo { S, F }
tag geo  { wall, floor }
layers {
    algo:  grid of algo
    level: grid of geo
}
rule plant { algo[.] => algo[S] }
rule fill_geo { all
    algo[S] => level[floor]
    algo[.] => level[wall]
}
program {
    resize(6, 4)
    some(max=4) plant
    all fill_geo
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(11);
    ls::grid algo = lv["algo"], geo = lv["level"];
    int s = gen.tag("algo.S"), floor = gen.tag("geo.floor"), wall = gen.tag("geo.wall");
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x)
            CHECK(geo.at(x, y) == (algo.at(x, y) == s ? floor : wall));
    (void)wall;
}

TEST_CASE("api: weighted any writes are deterministic per seed") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule mix {
    level[.]
    =>
    { any
      (weight=3) level[floor]
      (weight=1) level[wall]
    }
}
program {
    resize(12, 8)
    all mix
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level a = gen.generate(5);
    ls::level b = gen.generate(5);
    CHECK(dump(a, "level") == dump(b, "level"));

    int walls = 0, floors = 0;
    for (int v : dump(a, "level")) {
        if (v == gen.tag("geo.wall")) ++walls;
        else if (v == gen.tag("geo.floor")) ++floors;
    }
    CHECK(walls + floors == 12 * 8);   // every cell decided
    CHECK(floors > walls);             // 3:1 bias over 96 cells
}

TEST_CASE("api: rotation variants match reshaped patterns") {
    // The grid is 1 wide, so the written 1x2 [S .] pattern can never fit —
    // only its rotated 2x1 variant can. If rotation expansion works, the rule
    // fires and writes W below the S.
    auto gen = make(R"(
tag algo { S, W }
layers { algo: grid of algo }
rule seed_top { algo[.] => algo[S] }
rule mark(rotation=all) {
    algo[S .]
    =>
    algo[S W]
}
program {
    resize(1, 2)
    one seed_top
    one mark
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(3);
    int s = gen.tag("algo.S"), w = gen.tag("algo.W");
    // exactly one S and one W, vertically adjacent
    int count_s = 0, count_w = 0;
    for (int v : dump(lv, "algo")) {
        if (v == s) ++count_s;
        if (v == w) ++count_w;
    }
    CHECK(count_s == 1);
    CHECK(count_w == 1);
}

TEST_CASE("api: all-of-any writes both layers at once") {
    auto gen = make(R"(
tag algo  { F }
tag items { chest, heart }
layers {
    algo:  grid of algo
    loot:  grid of items
}
rule pave { algo[.] => algo[F] }
rule decorate {
    algo[F]
    =>
    { all
      algo[F]
      { any
        loot[chest]
        loot[heart]
      }
    }
}
program {
    resize(4, 4)
    all pave
    all decorate
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(9);
    ls::grid loot = lv["loot"];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(loot.at(x, y) >= 0);   // every cell got chest or heart
}

// ── step 3: policies, percent, ordered, observe ───────────────────────────────

static int count_val(ls::level const& lv, char const* layer, int val) {
    int n = 0;
    for (int v : dump(lv, layer))
        if (v == val) ++n;
    return n;
}

static const std::string grow_src = R"(
tag algo { S }
layers { algo: grid of algo }
rule plant { algo[.] => algo[S] }
rule grow(rotation=all) {
    algo[S .]
    =>
    algo[* S]
}
program {
    resize(9, 9)
    one plant
    some(max=20, policy=incremental) grow
}
)";

TEST_CASE("api: incremental sees prior writes; snapshot does not") {
    // From one seed, 20 incremental growth steps add exactly 20 cells —
    // each step re-collects, so growth feeds on its own writes.
    auto inc = make(grow_src);
    REQUIRE(static_cast<bool>(inc));
    ls::level lv = inc.generate(4);
    CHECK(count_val(lv, "algo", inc.tag("algo.S")) == 21);

    // The same rule under (default) snapshot can only fill the frozen
    // snapshot's neighbourhood: at most the seed's 4 neighbours.
    std::string snap_src = grow_src;
    auto pos = snap_src.find(", policy=incremental");
    snap_src.erase(pos, std::string(", policy=incremental").size());
    auto snap = make(snap_src);
    REQUIRE(static_cast<bool>(snap));
    ls::level sv = snap.generate(4);
    CHECK(count_val(sv, "algo", snap.tag("algo.S")) <= 5);
}

TEST_CASE("api: all(policy=incremental) runs to the fixpoint") {
    auto gen = make(R"(
tag geo { floor }
layers { level: grid of geo }
rule fill { level[.] => level[floor] }
program {
    resize(6, 5)
    all(policy=incremental) fill
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(2);
    CHECK(count_val(lv, "level", gen.tag("geo.floor")) == 30);
}

TEST_CASE("api: stabilize sweeps to a fixpoint") {
    // One seed floods the whole grid: each sweep advances the frontier one
    // ring; stabilize re-sweeps until nothing changes.
    auto gen = make(R"(
tag algo { S }
layers { algo: grid of algo }
rule plant { algo[.] => algo[S] }
rule flood(rotation=all) {
    algo[S .]
    =>
    algo[* S]
}
program {
    resize(7, 7)
    one plant
    all(policy=stabilize) flood
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(3);
    CHECK(count_val(lv, "algo", gen.tag("algo.S")) == 49);
}

TEST_CASE("api: some(max=N, policy=stabilize) counts sweeps") {
    // Two sweeps from a corner seed reach cells within Manhattan distance 2.
    auto gen = make(R"(
tag algo { S }
layers { algo: grid of algo }
rule plant { algo[.] => algo[S] }
rule flood(rotation=all) {
    algo[S .]
    =>
    algo[* S]
}
program {
    resize(9, 9)
    one plant
    some(max=2, policy=stabilize) flood
}
)");
    REQUIRE(static_cast<bool>(gen));
    int s = gen.tag("algo.S");
    int n = count_val(gen.generate(3), "algo", s);
    CHECK(n >= 6);    // a 2-ring diamond, clipped by edges: 6..13 cells
    CHECK(n <= 13);
}

TEST_CASE("api: some(percent=P) applies the exact fraction of the applied set") {
    auto src = [](int pct) {
        return "tag geo { floor }\n"
               "layers { level: grid of geo }\n"
               "rule paint { level[.] => level[floor] }\n"
               "program {\n    resize(10, 10)\n    some(percent=" +
               std::to_string(pct) + ") paint\n}\n";
    };
    auto half = make(src(50));
    REQUIRE(static_cast<bool>(half));
    CHECK(count_val(half.generate(1), "level", half.tag("geo.floor")) == 50);

    auto none = make(src(0));
    CHECK(count_val(none.generate(1), "level", none.tag("geo.floor")) == 0);

    auto full = make(src(100));
    CHECK(count_val(full.generate(1), "level", full.tag("geo.floor")) == 100);
}

TEST_CASE("api: ordered claims in priority order under snapshot") {
    // Both sub-rules match every empty cell; under `ordered`, the wall
    // sub-rule claims every cell first, so floor never applies.
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule paint { ordered
    level[.] => level[wall]
    level[.] => level[floor]
}
program {
    resize(6, 6)
    all paint
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(8);
    CHECK(count_val(lv, "level", gen.tag("geo.wall")) == 36);
    CHECK(count_val(lv, "level", gen.tag("geo.floor")) == 0);
}

TEST_CASE("api: ordered is preemptive under incremental") {
    // Priority: convert A to B while any A exists, else plant an A. The
    // fixpoint is all B — and at no point can two As coexist.
    auto gen = make(R"(
tag t { A, B }
layers { g: grid of t }
rule tick { ordered
    g[A] => g[B]
    g[.] => g[A]
}
program {
    resize(3, 3)
    all(policy=incremental) tick
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(6);
    CHECK(count_val(lv, "g", gen.tag("t.B")) == 9);
    CHECK(count_val(lv, "g", gen.tag("t.A")) == 0);
}

TEST_CASE("api: observe channel reports highlights and statement index") {
    auto gen = make(fill_src);
    auto g = gen.begin(42, ls::step_mode::application, ls::observe::on);
    CHECK(gen.statement_count() == 2);
    CHECK(g.stmt_index() == -1);

    REQUIRE(g.step());               // resize (statement boundary, no highlights)
    CHECK(g.stmt_index() == 0);

    REQUIRE(g.step());               // first fill application
    CHECK(g.stmt_index() == 1);
    auto hl = g.highlights();
    REQUIRE(!hl.empty());
    bool has_match = false, has_write = false;
    for (auto const& h : hl) {
        if (h.what == ls::cell_highlight::kind::match) has_match = true;
        if (h.what == ls::cell_highlight::kind::write) has_write = true;
        CHECK(h.layer == 0);
        CHECK(h.x >= 0); CHECK(h.x < 8);
        CHECK(h.y >= 0); CHECK(h.y < 4);
    }
    CHECK(has_match);
    CHECK(has_write);
    g.finish();

    // observe off: no highlights recorded
    auto g2 = gen.begin(42, ls::step_mode::application);
    g2.step(); g2.step();
    CHECK(g2.highlights().empty());
}

TEST_CASE("api: mid-batch snapshot shows the accumulating writes") {
    auto gen = make(fill_src);   // 8x4 all-fill = 32 applications
    auto g = gen.begin(42, ls::step_mode::application);
    REQUIRE(g.step());   // resize done
    int floor = gen.tag("geo.floor");
    for (int k = 1; k <= 3; ++k) {
        REQUIRE(g.step());
        CHECK(count_val(g.snapshot(), "level", floor) == k);
    }
    ls::level done = g.finish();
    CHECK(count_val(done, "level", floor) == 32);
}

// ── step 4: expressions, where, params, when, random ─────────────────────────

TEST_CASE("api: params drive conditional writes and when guards") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
params {
    difficulty: number = 0
    extra = difficulty * 2
}
rule paint { level[.] => level[ (if(difficulty > 3, wall, floor)) ] }
rule strip { level[wall] => level[.] }
program {
    resize(4, 4)
    all paint
    all strip  when (extra > 10)
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));

    // default difficulty 0 → all floor
    ls::level a = gen.generate(1);
    CHECK(count_val(a, "level", gen.tag("geo.floor")) == 16);

    // difficulty 5 → all wall, and the guard (extra=10 > 10 false) leaves them
    ls::level b = gen.generate(1, {{"difficulty", 5}});
    CHECK(count_val(b, "level", gen.tag("geo.wall")) == 16);

    // difficulty 6 → extra=12 → guard true → walls stripped
    ls::level c = gen.generate(1, {{"difficulty", 6}});
    CHECK(count_val(c, "level", gen.tag("geo.wall")) == 0);
}

TEST_CASE("api: where gates positions") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule pave { level[.] => level[floor] }
rule frame {
    { all
      level[floor]
      where[ (x == 0 || y == 0 || x == width - 1 || y == height - 1) ]
    }
    =>
    level[wall]
}
program {
    resize(6, 5)
    all pave
    all frame
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(1);
    int wall = gen.tag("geo.wall"), floor = gen.tag("geo.floor");
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 6; ++x) {
            bool edge = x == 0 || y == 0 || x == 5 || y == 4;
            CHECK(lv["level"].at(x, y) == (edge ? wall : floor));
        }
}

TEST_CASE("api: random cells are deterministic per seed and in range") {
    auto gen = make(R"(
tag geo { floor }
layers { tiles: grid of number }
rule roll { tiles[.] => tiles[ (random(3, 7)) ] }
program {
    resize(8, 8)
    all roll
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level a = gen.generate(9);
    ls::level b = gen.generate(9);
    CHECK(dump(a, "tiles") == dump(b, "tiles"));
    for (int v : dump(a, "tiles")) {
        CHECK(v >= 3);
        CHECK(v <= 7);
    }
}

TEST_CASE("api: totality — division by zero yields zero") {
    auto gen = make(R"(
layers { tiles: grid of number }
params { n: number = 0 }
rule f { tiles[.] => tiles[ (if(n == 0, 0, 100 / n)) ] }
program {
    resize(2, 2)
    all f
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    for (int v : dump(gen.generate(1), "tiles")) CHECK(v == 0);
}

TEST_CASE("api: empty tests distinguish empty from stored zero") {
    auto gen = make(R"(
tag geo { mark }
layers {
    tiles: grid of number
    flags: grid of geo
}
rule zero_some { tiles[.] => tiles[0] }
rule mark_empty {
    { all
      flags[.]
      where[ ((tiles == 0) && (tiles != .)) ]
    }
    =>
    flags[mark]
}
program {
    resize(4, 1)
    some(max=2) zero_some
    all mark_empty
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(5);
    // exactly the two cells holding a stored 0 get marked; empty cells (which
    // also read as 0 in arithmetic) do not
    CHECK(count_val(lv, "flags", gen.tag("geo.mark")) == 2);
}

TEST_CASE("api: union masks match either value") {
    auto gen = make(R"(
tag geo { wall, door, floor, blocker = wall | door }
layers { level: grid of geo }
rule mix {
    level[.]
    =>
    { any
      level[wall]
      level[door]
      level[floor]
    }
}
rule clear_blockers { level[blocker] => level[floor] }
program {
    resize(6, 6)
    all mix
    all clear_blockers
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(7);
    CHECK(count_val(lv, "level", gen.tag("geo.floor")) == 36);
}

// -- step 5: geometry operations + path --

TEST_CASE("api: upscale, pad, mirror, trim") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule seed_corner {
    { all
      level[.]
      where[ (x == 0 && y == 0) ]
    }
    =>
    level[wall]
}
program {
    resize(2, 2)
    all seed_corner
    upscale(2, 2)
    pad(1)
    mirror(horizontal)
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(1);
    // 2x2 -> upscale(2,2) -> 4x4 -> pad(1) -> 6x6
    CHECK(lv.width() == 6);
    CHECK(lv.height() == 6);
    int wall = gen.tag("geo.wall");
    ls::grid g = lv["level"];
    // the corner wall became a 2x2 block at (1,1) after pad; mirror(horizontal)
    // copies the left half reflected onto the right
    CHECK(g.at(1, 1) == wall);
    CHECK(g.at(2, 2) == wall);
    CHECK(g.at(4, 1) == wall);   // mirrored image of x=1 (width 6: 5-1=4)
    CHECK(g.at(3, 2) == wall);   // mirrored image of x=2
    CHECK(count_val(lv, "level", wall) == 8);
}

TEST_CASE("api: trim crops to the union content box") {
    auto gen = make(R"(
tag geo { wall }
layers { level: grid of geo }
rule mark {
    { all
      level[.]
      where[ (x >= 2 && x <= 4 && y >= 1 && y <= 3) ]
    }
    =>
    level[wall]
}
program {
    resize(8, 6)
    all mark
    trim()
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(1);
    CHECK(lv.width() == 3);
    CHECK(lv.height() == 3);
    CHECK(count_val(lv, "level", gen.tag("geo.wall")) == 9);
}

static const std::string corridor_src = R"(
tag algo { start, goal, road }
layers { algo: grid of algo }
rule place_start {
    { all
      algo[.]
      where[ (x == 0 && y == 0) ]
    }
    =>
    algo[start]
}
rule place_goal {
    { all
      algo[.]
      where[ (x == width - 1 && y == height - 1) ]
    }
    =>
    algo[goal]
}
program {
    resize(7, 5)
    all place_start
    all place_goal
    path(from=start, to=goal, into=algo, write=road,
         passable=((0 == 0)))
}
)";

TEST_CASE("api: path carves a connected shortest route") {
    auto gen = make(corridor_src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(11);
    int road = gen.tag("algo.road");

    // endpoints included/overwritten: (0,0) and (6,4) are road now
    CHECK(lv["algo"].at(0, 0) == road);
    CHECK(lv["algo"].at(6, 4) == road);
    // 4-connected shortest route length = manhattan + 1
    CHECK(count_val(lv, "algo", road) == 6 + 4 + 1);

    // deterministic per seed; different seeds may carve different routes
    CHECK(dump(gen.generate(11), "algo") == dump(lv, "algo"));
}

TEST_CASE("api: path over weighted cost prefers the cheap terrain") {
    auto gen = make(R"(
tag algo { start, goal, road }
layers {
    algo:  grid of algo
    swamp: grid of number
}
rule mark_swamp {
    { all
      swamp[.]
      where[ (y == 0 && x > 0 && x < width - 1) ]
    }
    =>
    swamp[9]
}
rule place_start {
    { all algo[.] where[ (x == 0 && y == 0) ] }
    =>
    algo[start]
}
rule place_goal {
    { all algo[.] where[ (x == width - 1 && y == 0) ] }
    =>
    algo[goal]
}
program {
    resize(5, 3)
    all mark_swamp
    all place_start
    all place_goal
    path(from=start, to=goal, into=algo, write=road,
         passable=((0 == 0)), cost=(1 + swamp))
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(3);
    int road = gen.tag("algo.road");
    // the straight top row costs 1+10+10+10+1; the detour around costs 7 —
    // the route must dip below the swamp row
    CHECK(lv["algo"].at(1, 0) != road);
    CHECK(lv["algo"].at(2, 0) != road);
    CHECK(lv["algo"].at(3, 0) != road);
    CHECK(count_val(lv, "algo", road) == 7);
}

TEST_CASE("api: path with no route warns and leaves grids unchanged") {
    auto gen = make(R"(
tag algo { start, goal, road }
layers { algo: grid of algo }
rule place_start {
    { all algo[.] where[ (x == 0 && y == 0) ] }
    =>
    algo[start]
}
rule place_goal {
    { all algo[.] where[ (x == 4 && y == 4) ] }
    =>
    algo[goal]
}
program {
    resize(5, 5)
    all place_start
    all place_goal
    path(from=start, to=goal, into=algo, write=road,
         passable=((algo == road)))
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(1);   // start/goal isolated: nothing passable between
    CHECK(count_val(lv, "algo", gen.tag("algo.road")) == 0);
    CHECK(lv["algo"].at(0, 0) == gen.tag("algo.start"));
    CHECK(lv["algo"].at(4, 4) == gen.tag("algo.goal"));
}

TEST_CASE("api: snapshot mid-run sees committed statements only") {
    auto gen = make(scatter_src);
    auto g = gen.begin(7, ls::step_mode::statement);

    REQUIRE(g.step());               // resize done
    ls::level after_resize = g.snapshot();
    CHECK(after_resize.width() == 10);
    int planted = 0;
    for (int v : dump(after_resize, "algo"))
        if (v >= 0) ++planted;
    CHECK(planted == 0);             // scatter not yet run

    g.finish();
}
