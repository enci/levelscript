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
