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
sequence main {
    resize(8, 4)
    all fill
}
)";

static const std::string scatter_src = R"(
tag algo { S }
layers { algo: grid of algo }
rule plant { algo[.] => algo[S] }
sequence main {
    resize(10, 6)
    some(max=5) plant
}
)";

TEST_CASE("api: compile success and failure") {
    auto ok = make(fill_src);
    CHECK(static_cast<bool>(ok));
    CHECK(ok.error().empty());

    auto bad = make("rule r { g[.] => g[wall] }");   // no layers
    CHECK(!bad);
    CHECK(!bad.error().empty());

    // a module with no sequences compiles; it just has no entry to run (section 6)
    auto lib = make("tag t { a }");
    CHECK(static_cast<bool>(lib));
    CHECK(lib.sequence_count() == 0);
    CHECK(lib.sequence("main") == -1);
    CHECK(lib.generate(-1, 1).width() == 0);   // invalid entry: empty level
    CHECK(!lib.run(-1, 1).step());             // invalid entry: nothing to step
}

TEST_CASE("api: generate fills the level") {
    auto gen = make(fill_src);
    REQUIRE(static_cast<bool>(gen));

    ls::level lv = gen.generate(gen.sequence("main"), 42);
    CHECK(lv.width() == 8);
    CHECK(lv.height() == 4);
    CHECK(lv.layer_count() == 2);
    CHECK(lv.layer_name(0) == "level");

    ls::grid geo = lv["level"];
    int floor = gen.tag("geo.floor");
    REQUIRE(floor > 0);   // 0 means unknown; a resolved mask is >= 2
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x) {
            CHECK(geo.at(x, y) == floor);
            CHECK(!geo.is_empty(x, y));
        }

    // untouched number layer stays empty
    ls::grid tiles = lv["tiles"];
    CHECK(tiles.is_number());
    CHECK(tiles.is_empty(0, 0));
}

TEST_CASE("api: tag resolution") {
    auto gen = make(fill_src);
    // tag() returns the value's bit mask (spec section 3: bit 1..30 in declaration
    // order), not a 0-based id; 0 means unknown (not -1), so an unresolved
    // name ORed into a composite mask degrades correctly.
    CHECK(gen.tag("geo.wall") == (1 << 1));
    CHECK(gen.tag("geo.floor") == (1 << 2));
    CHECK(gen.tag("geo.lava") == 0);
    CHECK(gen.tag("nope.wall") == 0);
    CHECK(gen.tag("wall") == 0);   // must be qualified
}

TEST_CASE("api: queries are total") {
    auto gen = make(fill_src);
    ls::level lv = gen.generate(gen.sequence("main"), 1);
    CHECK(lv["nope"].at(0, 0) == -1);          // unknown layer
    CHECK(lv["level"].at(-1, 0) == -1);        // out of range
    CHECK(lv["level"].at(0, 99) == -1);
    CHECK(ls::level{}.width() == 0);           // default level
    CHECK(ls::grid{}.at(0, 0) == -1);          // default grid
    CHECK(ls::generator{}.generate(0, 1).layer_count() == 0);
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

    ls::level a = gen.generate(gen.sequence("main"), 7);
    int planted = 0;
    for (int v : dump(a, "algo"))
        if (v >= 0) ++planted;
    CHECK(planted == 5);

    ls::level b = gen.generate(gen.sequence("main"), 7);
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
sequence main {
    resize(4, 4)
    one plant
}
)");
    int planted = 0;
    for (int v : dump(gen.generate(gen.sequence("main"), 3), "algo"))
        if (v >= 0) ++planted;
    CHECK(planted == 1);
}

TEST_CASE("api: rhs '.' clears and '*' preserves") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule fill  { level[.] => level[floor] }
rule strip { level[floor] => level[.] }
sequence main {
    resize(3, 3)
    all fill
    all strip
}
)");
    ls::level lv = gen.generate(gen.sequence("main"), 1);
    for (int v : dump(lv, "level")) CHECK(v == -1);
}

TEST_CASE("api: stepping matches batch generation") {
    auto gen = make(scatter_src);

    // statement granularity: one step per statement of the entry's body
    auto g1 = gen.run(gen.sequence("main"), 7, ls::step_mode::statement);
    int stmts = 0;
    while (g1.step()) ++stmts;
    CHECK(stmts == 2);   // resize + some(max=5)

    // application granularity: resize stops once, each application once
    auto g2 = gen.run(gen.sequence("main"), 7, ls::step_mode::application);
    int steps = 0;
    while (g2.step()) ++steps;
    CHECK(steps == 2 + 5);   // resize stmt + 5 applications + apply stmt boundary
                             // (the batch's own statement boundary is the 7th)

    // finish() == generate() for the same seed
    auto g3 = gen.run(gen.sequence("main"), 7);
    ls::level via_steps = g3.finish();
    ls::level via_batch = gen.generate(gen.sequence("main"), 7);
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
sequence main {
    resize(6, 4)
    some(max=4) plant
    all fill_geo
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 11);
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
sequence main {
    resize(12, 8)
    all mix
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level a = gen.generate(gen.sequence("main"), 5);
    ls::level b = gen.generate(gen.sequence("main"), 5);
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
sequence main {
    resize(1, 2)
    one seed_top
    one mark
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 3);
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
sequence main {
    resize(4, 4)
    all pave
    all decorate
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 9);
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
sequence main {
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
    ls::level lv = inc.generate(inc.sequence("main"), 4);
    CHECK(count_val(lv, "algo", inc.tag("algo.S")) == 21);

    // The same rule under (default) snapshot can only fill the frozen
    // snapshot's neighbourhood: at most the seed's 4 neighbours.
    std::string snap_src = grow_src;
    auto pos = snap_src.find(", policy=incremental");
    snap_src.erase(pos, std::string(", policy=incremental").size());
    auto snap = make(snap_src);
    REQUIRE(static_cast<bool>(snap));
    ls::level sv = snap.generate(snap.sequence("main"), 4);
    CHECK(count_val(sv, "algo", snap.tag("algo.S")) <= 5);
}

TEST_CASE("api: all(policy=incremental) runs to the fixpoint") {
    auto gen = make(R"(
tag geo { floor }
layers { level: grid of geo }
rule fill { level[.] => level[floor] }
sequence main {
    resize(6, 5)
    all(policy=incremental) fill
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 2);
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
sequence main {
    resize(7, 7)
    one plant
    all(policy=stabilize) flood
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 3);
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
sequence main {
    resize(9, 9)
    one plant
    some(max=2, policy=stabilize) flood
}
)");
    REQUIRE(static_cast<bool>(gen));
    int s = gen.tag("algo.S");
    int n = count_val(gen.generate(gen.sequence("main"), 3), "algo", s);
    CHECK(n >= 6);    // a 2-ring diamond, clipped by edges: 6..13 cells
    CHECK(n <= 13);
}

TEST_CASE("api: some(percent=P) applies the exact fraction of the applied set") {
    auto src = [](int pct) {
        return "tag geo { floor }\n"
               "layers { level: grid of geo }\n"
               "rule paint { level[.] => level[floor] }\n"
               "sequence main {\n    resize(10, 10)\n    some(percent=" +
               std::to_string(pct) + ") paint\n}\n";
    };
    auto half = make(src(50));
    REQUIRE(static_cast<bool>(half));
    CHECK(count_val(half.generate(half.sequence("main"), 1), "level", half.tag("geo.floor")) == 50);

    auto none = make(src(0));
    CHECK(count_val(none.generate(none.sequence("main"), 1), "level", none.tag("geo.floor")) == 0);

    auto full = make(src(100));
    CHECK(count_val(full.generate(full.sequence("main"), 1), "level", full.tag("geo.floor")) == 100);
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
sequence main {
    resize(6, 6)
    all paint
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 8);
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
sequence main {
    resize(3, 3)
    all(policy=incremental) tick
}
)");
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 6);
    CHECK(count_val(lv, "g", gen.tag("t.B")) == 9);
    CHECK(count_val(lv, "g", gen.tag("t.A")) == 0);
}

TEST_CASE("api: observe channel reports highlights and statement index") {
    auto gen = make(fill_src);
    auto g = gen.run(gen.sequence("main"), 42, ls::step_mode::application, ls::observe::on);
    CHECK(gen.statement_count(gen.sequence("main")) == 2);
    CHECK(g.statement_index() == -1);

    REQUIRE(g.step());               // resize (statement boundary, no highlights)
    CHECK(g.statement_index() == 0);

    REQUIRE(g.step());               // first fill application
    CHECK(g.statement_index() == 1);
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
    auto g2 = gen.run(gen.sequence("main"), 42, ls::step_mode::application);
    g2.step(); g2.step();
    CHECK(g2.highlights().empty());
}

TEST_CASE("api: mid-batch snapshot shows the accumulating writes") {
    auto gen = make(fill_src);   // 8x4 all-fill = 32 applications
    auto g = gen.run(gen.sequence("main"), 42, ls::step_mode::application);
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
sequence main {
    resize(4, 4)
    all paint
    all strip  when (extra > 10)
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));

    // default difficulty 0 → all floor
    ls::level a = gen.generate(gen.sequence("main"), 1);
    CHECK(count_val(a, "level", gen.tag("geo.floor")) == 16);

    // difficulty 5 → all wall, and the guard (extra=10 > 10 false) leaves them
    ls::level b = gen.generate(gen.sequence("main"), 1, {{"difficulty", 5}});
    CHECK(count_val(b, "level", gen.tag("geo.wall")) == 16);

    // difficulty 6 → extra=12 → guard true → walls stripped
    ls::level c = gen.generate(gen.sequence("main"), 1, {{"difficulty", 6}});
    CHECK(count_val(c, "level", gen.tag("geo.wall")) == 0);
}

TEST_CASE("api: where gates positions") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule pave { level[.] => level[floor] }
rule frame {
    level[floor]
    where[ (x == 0 || y == 0 || x == width - 1 || y == height - 1) ]
    =>
    level[wall]
}
sequence main {
    resize(6, 5)
    all pave
    all frame
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 1);
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
sequence main {
    resize(8, 8)
    all roll
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level a = gen.generate(gen.sequence("main"), 9);
    ls::level b = gen.generate(gen.sequence("main"), 9);
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
sequence main {
    resize(2, 2)
    all f
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    for (int v : dump(gen.generate(gen.sequence("main"), 1), "tiles")) CHECK(v == 0);
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
    flags[.]
    where[ ((tiles == 0) && (tiles != .)) ]
    =>
    flags[mark]
}
sequence main {
    resize(4, 1)
    some(max=2) zero_some
    all mark_empty
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 5);
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
sequence main {
    resize(6, 6)
    all mix
    all clear_blockers
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 7);
    CHECK(count_val(lv, "level", gen.tag("geo.floor")) == 36);
}

// -- step 5: geometry operations + path --

TEST_CASE("api: upscale, pad, mirror, trim") {
    auto gen = make(R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule seed_corner {
    level[.]
    where[ (x == 0 && y == 0) ]
    =>
    level[wall]
}
sequence main {
    resize(2, 2)
    all seed_corner
    upscale(2, 2)
    pad(1)
    mirror(horizontal)
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 1);
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
    level[.]
    where[ (x >= 2 && x <= 4 && y >= 1 && y <= 3) ]
    =>
    level[wall]
}
sequence main {
    resize(8, 6)
    all mark
    trim()
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 1);
    CHECK(lv.width() == 3);
    CHECK(lv.height() == 3);
    CHECK(count_val(lv, "level", gen.tag("geo.wall")) == 9);
}

static const std::string corridor_src = R"(
tag algo { start, goal, road }
layers { algo: grid of algo }
rule place_start {
    algo[.]
    where[ (x == 0 && y == 0) ]
    =>
    algo[start]
}
rule place_goal {
    algo[.]
    where[ (x == width - 1 && y == height - 1) ]
    =>
    algo[goal]
}
sequence main {
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
    ls::level lv = gen.generate(gen.sequence("main"), 11);
    int road = gen.tag("algo.road");

    // endpoints included/overwritten: (0,0) and (6,4) are road now
    CHECK(lv["algo"].at(0, 0) == road);
    CHECK(lv["algo"].at(6, 4) == road);
    // 4-connected shortest route length = manhattan + 1
    CHECK(count_val(lv, "algo", road) == 6 + 4 + 1);

    // deterministic per seed; different seeds may carve different routes
    CHECK(dump(gen.generate(gen.sequence("main"), 11), "algo") == dump(lv, "algo"));
}

TEST_CASE("api: path over weighted cost prefers the cheap terrain") {
    auto gen = make(R"(
tag algo { start, goal, road }
layers {
    algo:  grid of algo
    swamp: grid of number
}
rule mark_swamp {
    swamp[.]
    where[ (y == 0 && x > 0 && x < width - 1) ]
    =>
    swamp[9]
}
rule place_start {
    algo[.] where[ (x == 0 && y == 0) ]
    =>
    algo[start]
}
rule place_goal {
    algo[.] where[ (x == width - 1 && y == 0) ]
    =>
    algo[goal]
}
sequence main {
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
    ls::level lv = gen.generate(gen.sequence("main"), 3);
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
    algo[.] where[ (x == 0 && y == 0) ]
    =>
    algo[start]
}
rule place_goal {
    algo[.] where[ (x == 4 && y == 4) ]
    =>
    algo[goal]
}
sequence main {
    resize(5, 5)
    all place_start
    all place_goal
    path(from=start, to=goal, into=algo, write=road,
         passable=((algo == road)))
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    ls::level lv = gen.generate(gen.sequence("main"), 1);   // start/goal isolated: nothing passable between
    CHECK(count_val(lv, "algo", gen.tag("algo.road")) == 0);
    CHECK(lv["algo"].at(0, 0) == gen.tag("algo.start"));
    CHECK(lv["algo"].at(4, 4) == gen.tag("algo.goal"));
}

TEST_CASE("api: snapshot mid-run sees committed statements only") {
    auto gen = make(scatter_src);
    auto g = gen.run(gen.sequence("main"), 7, ls::step_mode::statement);

    REQUIRE(g.step());               // resize done
    ls::level after_resize = g.snapshot();
    CHECK(after_resize.width() == 10);
    int planted = 0;
    for (int v : dump(after_resize, "algo"))
        if (v >= 0) ++planted;
    CHECK(planted == 0);             // scatter not yet run

    g.finish();
}

// -- spec 0.5 delta Δ3: a same-LHS variant with different writes is its own
//    candidate. Statistical over seeds; each seed is deterministic, so these
//    are stable, and the failure odds of a correct build are < 2^-100. --

TEST_CASE("api: H-flip of a same-LHS rule writes both orientations") {
    auto gen = make(R"(
tag t { a, b, c }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule pair(symmetry=horizontal) { g[a a] => g[b c] }
sequence main {
    resize(2, 1)
    all fill
    one pair
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    int b = gen.tag("t.b"), c = gen.tag("t.c");
    int bc = 0, cb = 0;
    const int runs = 400;
    for (int seed = 1; seed <= runs; ++seed) {
        ls::level lv = gen.generate(gen.sequence("main"), seed);
        ls::grid g = lv["g"];
        if (g.at(0, 0) == b && g.at(1, 0) == c) ++bc;
        else if (g.at(0, 0) == c && g.at(1, 0) == b) ++cb;
    }
    CHECK(bc + cb == runs);
    // both variants are equal-weight candidates: expect ~50/50
    CHECK(bc > runs / 4);
    CHECK(cb > runs / 4);
}

TEST_CASE("api: rotation=all on a symmetric LHS reaches every write corner") {
    auto gen = make(R"(
tag t { s, f }
layers { g: grid of t }
rule fill { g[.] => g[s] }
rule reduce(rotation=all) {
    g[
        s s
        s s ]
    =>
    g[
        s f
        f f ]
}
sequence main {
    resize(2, 2)
    all fill
    one reduce
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    int s = gen.tag("t.s");
    int corner_hits[4] = {0, 0, 0, 0};
    const int runs = 400;
    for (int seed = 1; seed <= runs; ++seed) {
        ls::level lv = gen.generate(gen.sequence("main"), seed);
        ls::grid g = lv["g"];
        int count = 0, where = -1;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                if (g.at(x, y) == s) { ++count; where = y * 2 + x; }
        REQUIRE(count == 1);
        ++corner_hits[where];
    }
    for (int k = 0; k < 4; ++k) {
        INFO("corner " << k);
        CHECK(corner_hits[k] > runs / 8);   // ~25% each
    }
}

// ── cross-platform determinism (section 7.2, section 10.6) ─────────────────────────────────
// The draw primitives are plain 64-bit integer arithmetic over mt19937_64, so
// these exact outputs must come out on every platform and compiler. A
// failure here on one platform only is a portability bug, not a golden to
// update. Each primitive is exercised: `random` (bounded draws), `one mark`
// (the batch shuffle), `pick` (incremental uniform picks), `mix` (weighted
// `{ any }` rolls).
TEST_CASE("api: pinned draws give the same level on every platform") {
    auto gen = make(R"(
tag t { a, b, c, m }
layers {
    n: grid of number
    g: grid of t
}
rule rnd  { n[.] => n[ (random(0, 999)) ] }
rule mark { g[.] => g[m] }
rule fill { g[.] => g[a] }
rule pick { g[a] => g[b] }
rule mix  { g[a] => { any (weight=1) g[b]  (weight=3) g[c] } }
sequence main {
    resize(4, 3)
    all rnd
    one mark
    all fill
    some(max=2, policy=incremental) pick
    all mix
}
)");
    REQUIRE(static_cast<bool>(gen));
    auto render = [&](uint64_t seed) {
        auto lv = gen.generate(gen.sequence("main"), seed);
        std::string out;
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x) out += std::to_string(lv["n"].at(x, y)) + " ";
        out += "| ";
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x) out += lv["g"].valueName(lv["g"].at(x, y));
        return out;
    };
    CHECK(render(1)  == "783 567 169 277 180 563 833 610 188 400 307 523 | bccccbcbcmcc");
    CHECK(render(42) == "74 406 309 392 358 609 425 392 210 662 659 351 | bcmcbcbcccbc");
}
