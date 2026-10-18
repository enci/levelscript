// The structural operations of sections 6.11-6.13: bsp, voronoi, noise -
// their guarantees as properties over many seeds, their draw contracts, and
// the hash primitive they share with path.
#include "test_support.hpp"
#include "machine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using ts::make;
using ts::show;
using ts::stamped;

namespace {

ls::level generate(std::string const& src, uint64_t seed = 1) {
    auto gen = make(src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    return gen.generate(gen.sequence("main"), seed);
}

std::vector<std::string> warnings_of(std::string const& src) {
    std::vector<std::string> got;
    auto gen = ls::generator::compile(src, "w.lvs", {},
                                      [&](std::string const& w) { got.push_back(w); });
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    gen.generate(gen.sequence("main"), 1);
    return got;
}

const std::string zone_decls = R"(
tag t { s, f, w }
layers {
    g: grid of t
    z: grid of number
    r: grid of number
    zg: graph of z
}
)";

std::string sized(int w, int h, std::string const& body) {
    return zone_decls + "\nsequence main {\n    resize(" + std::to_string(w) + ", " +
           std::to_string(h) + ")\n    " + body + "\n}\n";
}

struct box { int x0{1 << 30}, y0{1 << 30}, x1{-1}, y1{-1}, cells{0}; };

// Each id's bounding box and cell count.
std::map<long long, box> boxes(ls::level const& lv, std::string const& name) {
    std::map<long long, box> out;
    auto g = lv[name];
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x) {
            if (g.is_empty(x, y)) continue;
            auto& b = out[g.at(x, y)];
            b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
            ++b.cells;
        }
    return out;
}

// Every non-empty cell's value is the graph node covering it.
void check_ids_are_nodes(ls::level const& lv) {
    auto z = lv["z"];
    auto g = lv.graph("zg");
    std::set<long long> ids;
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x)
            if (!z.is_empty(x, y)) {
                CHECK(g.node_at(x, y) == z.at(x, y));
                ids.insert(z.at(x, y));
            }
    CHECK((int)ids.size() == g.node_count());
}

}  // namespace

// ── hash ─────────────────────────────────────────────────────────────────────

TEST_CASE("partition: hash is splitmix64's finalizer over d ^ i") {
    using ls::internal::hash;
    // splitmix64 seeded with 0: its first output is mix(0)
    CHECK(hash(0, 0) == 0xe220a8397b1dcdafull);
    CHECK(hash(5, 3) == hash(6, 0));   // only d ^ i matters
    CHECK(hash(1, 2) != hash(1, 3));
}

TEST_CASE("partition: mul_div is exact beyond 64 bits") {
    using ls::internal::mul_div;
    CHECK(mul_div(10, 7, 3) == 23);
    CHECK(mul_div(~0ull, ~0ull, ~0ull) == ~0ull);
    CHECK(mul_div(uint64_t{1} << 40, uint64_t{1} << 40, uint64_t{1} << 30) == uint64_t{1} << 50);
    CHECK(mul_div(~0ull, 3, 4) == 0xbfffffffffffffffull);
}

// ── bsp ──────────────────────────────────────────────────────────────────────

TEST_CASE("partition: bsp leaves tile the stack as rectangles within min and max") {
    struct cfg { int w, h, min, max; };
    cfg const cfgs[] = {{48, 32, 6, 14}, {20, 9, 3, 5}, {17, 17, 4, 4}, {30, 7, 2, 6}};
    for (auto const& c : cfgs)
        for (uint64_t seed = 1; seed <= 25; ++seed) {
            INFO("w=" << c.w << " h=" << c.h << " min=" << c.min << " max=" << c.max
                      << " seed=" << seed);
            auto lv = generate(sized(c.w, c.h, "bsp(into=z, min=" + std::to_string(c.min) +
                                                ", max=" + std::to_string(c.max) + ")"), seed);
            int total = 0;
            for (auto const& [id, b] : boxes(lv, "z")) {
                int bw = b.x1 - b.x0 + 1, bh = b.y1 - b.y0 + 1;
                CHECK(b.cells == bw * bh);   // a rectangle
                CHECK(bw >= c.min);
                CHECK(bh >= c.min);
                if (bw > c.max) CHECK(bw < 2 * c.min);
                if (bh > c.max) CHECK(bh < 2 * c.min);
                total += b.cells;
            }
            CHECK(total == c.w * c.h);   // every cell written
            check_ids_are_nodes(lv);
        }
}

TEST_CASE("partition: bsp with split=100 and no max splits as far as it can") {
    for (uint64_t seed = 1; seed <= 20; ++seed)
        for (auto const& [id, b] : boxes(generate(sized(40, 25, "bsp(into=z, min=3, split=100)"), seed), "z")) {
            CHECK(b.x1 - b.x0 + 1 < 6);
            CHECK(b.y1 - b.y0 + 1 < 6);
        }
}

TEST_CASE("partition: bsp edge cases") {
    // split=0, no max: the root never splits
    CHECK(show(generate(sized(5, 2, "bsp(into=z, min=1, split=0)")), "z") == "00000/00000");
    // a stack shorter than min is one leaf
    CHECK(show(generate(sized(3, 2, "bsp(into=z, min=5, max=1)")), "z") == "000/000");
    // max below min forces every node that can split: 6 wide, min 2 -> 2 + 2 + 2 or 2 + 4 ...
    for (uint64_t seed = 1; seed <= 10; ++seed)
        for (auto const& [id, b] : boxes(generate(sized(6, 1, "bsp(into=z, min=2, max=1)"), seed), "z"))
            CHECK(b.x1 - b.x0 + 1 < 4);
}

TEST_CASE("partition: bsp draws depend only on the stack size and arguments") {
    // different content before the split, same draws: the same leaves
    auto lv_a = generate(sized(30, 20, ts::stamp("g", {"s f"}) + "\n    bsp(into=z, min=4)"), 9);
    auto lv_b = generate(sized(30, 20, ts::stamp("g", {"w w"}) + "\n    bsp(into=z, min=4)"), 9);
    CHECK(show(lv_a, "z") == show(lv_b, "z"));
    // split=0 takes no draw at all: a following draw is unchanged
    std::string after = "\n    once { where[ (x == 0 && y == 0) ] => r[ (random(0, 1000000)) ] }";
    auto with    = generate(sized(4, 4, "bsp(into=z, min=1, split=0)" + after), 3);
    auto without = generate(sized(4, 4, after), 3);
    CHECK(with["r"].at(0, 0) == without["r"].at(0, 0));
}

// ── voronoi ──────────────────────────────────────────────────────────────────

TEST_CASE("partition: voronoi zones are connected, one per site, in graph node order") {
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        INFO("seed=" << seed);
        auto lv = generate(sized(24, 16,
            "everywhere { where[ (x == 12 && y != 8) ] => g[w] }\n"
            "    everywhere { g[.] => g[f] }\n"
            "    scatter(7) { g[f] => g[s] }\n"
            "    voronoi(sites=s, within=(g != w), into=z)"), seed);
        // adjacent placements merge into one site, so up to 7 zones; each holds a site
        auto bs = boxes(lv, "z");
        CHECK((bs.size() >= 1 && bs.size() <= 7));
        check_ids_are_nodes(lv);   // graph regions are 4-connected: one node per zone
        auto z = lv["z"];
        auto g = lv["g"];
        std::set<long long> seeded;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 24; ++x)
                if (g.valueName(g.at(x, y)) == "s") seeded.insert(z.at(x, y));
        CHECK(seeded.size() == bs.size());
        CHECK(z.is_empty(12, 0));  // a wall cell is outside within
    }
}

TEST_CASE("partition: voronoi distance is geodesic through within") {
    // the left site reaches the right half only through the gap at the bottom
    auto lv = generate(stamped(zone_decls, "g", {"f s w f f", "f f w f f", "f f f f s"},
                               "voronoi(sites=s, within=(g != w), into=z)"));
    auto z = lv["z"];
    // (3,0) is 2 steps from (1,0) across the wall but 6 around it; 3 from (4,2)
    CHECK(z.at(3, 0) == z.at(4, 2));
    CHECK(z.at(0, 0) == z.at(1, 0));
    CHECK(z.at(1, 0) != z.at(4, 2));
}

TEST_CASE("partition: voronoi ties are keyed per site, so borders are coherent") {
    // the middle column is equidistant from both sites everywhere
    std::set<std::string> outcomes;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        auto lv = generate(stamped(zone_decls, "g", {". . .", ". . .", "s . s", ". . .", ". . ."},
                                   "voronoi(sites=s, into=z)"), seed);
        auto z = lv["z"];
        for (int y = 1; y < 5; ++y) CHECK(z.at(1, y) == z.at(1, 0));
        outcomes.insert(show(lv, "z"));
    }
    CHECK(outcomes.size() == 2);
}

TEST_CASE("partition: voronoi site blobs, unreachable cells, and connectivity 8") {
    // a 4-connected blob is one site; the enclosed cell is unreachable
    auto blob = generate(stamped(zone_decls, "g", {"s s . w f", ". . . w w"},
                                 "voronoi(sites=s, within=(g != w), into=z)"));
    CHECK(show(blob, "z") == "000../000..");
    // diagonal neighbours: under 8 the far corner is one step closer
    auto eight = generate(stamped(zone_decls, "g", {"s . .", ". . .", ". . s"},
                                  "voronoi(sites=s, into=z, connectivity=8)"));
    auto z = eight["z"];
    CHECK(z.at(0, 0) == 0);
    CHECK(z.at(2, 2) == 1);
}

// ── noise ────────────────────────────────────────────────────────────────────

TEST_CASE("partition: noise rank gives every value the same share of cells") {
    for (int scale : {1, 3, 8, 32})
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            INFO("scale=" << scale << " seed=" << seed);
            auto lv = generate(sized(32, 24, "noise(into=z, lo=0, hi=99, scale=" +
                                                 std::to_string(scale) + ", octaves=4)"), seed);
            auto z = lv["z"];
            std::vector<int> per(100);
            int below30 = 0;
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 32; ++x) {
                    long long v = z.at(x, y);
                    REQUIRE((v >= 0 && v <= 99));
                    ++per[(size_t)v];
                    below30 += v < 30;
                }
            // 768 cells: 0.3 * 768 = 230.4
            CHECK((below30 == 230 || below30 == 231));
            auto [mn, mx] = std::minmax_element(per.begin(), per.end());
            CHECK(*mx - *mn <= 1);
        }
}

TEST_CASE("partition: noise range maps the extremes onto lo and hi") {
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        auto lv = generate(sized(20, 20, "noise(into=z, lo=-5, hi=5, scale=6, normalize=range)"), seed);
        auto b = boxes(lv, "z");
        CHECK(b.begin()->first == -5);
        CHECK(b.rbegin()->first == 5);
    }
}

TEST_CASE("partition: noise follows the seed, and is smooth at large scale") {
    std::string src = sized(16, 16, "noise(into=z, lo=0, hi=255, scale=16)");
    CHECK(show(generate(src, 4), "z") == show(generate(src, 4), "z"));
    CHECK(show(generate(src, 4), "z") != show(generate(src, 5), "z"));
    // one octave at spacing 16 over 16 cells: neighbours differ by little
    auto z = generate(src, 4)["z"];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x + 1 < 16; ++x) CHECK(std::abs(z.at(x, y) - z.at(x + 1, y)) <= 40);
}

TEST_CASE("partition: noise and voronoi each take exactly one raw draw") {
    std::string after = "\n    once { where[ (x == 0 && y == 0) ] => r[ (random(0, 1000000)) ] }";
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        auto n = generate(sized(6, 6, "noise(into=z, lo=0, hi=9, scale=2, octaves=2)" + after), seed);
        auto v = generate(sized(6, 6, "voronoi(sites=(x == 0), into=z)" + after), seed);
        CHECK(n["r"].at(0, 0) == v["r"].at(0, 0));
    }
}

// ── compile time and warnings ────────────────────────────────────────────────

TEST_CASE("partition: into must be a number grid") {
    // check 35
    for (char const* op : {"bsp(into=g, min=2)", "voronoi(sites=s, into=g)",
                           "noise(into=g, lo=0, hi=1, scale=2)"})
        CHECK(ts::compile_result(sized(4, 4, op)).has_error("'g' is a tag grid; it takes a number grid"));
    CHECK(ts::compile_result(sized(4, 4, "noise(into=z, lo=0, hi=1, scale=2, normalize=sorted)"))
              .has_error("invalid noise normalize 'sorted'"));
    CHECK(ts::compile_result(sized(4, 4, "noise(into=z, lo=0, scale=2)"))
              .has_error("requires argument 'hi='"));
    CHECK(ts::compile_result(sized(4, 4, "bsp(z, 2)")).has_error("named-only"));
}

TEST_CASE("partition: every runtime warning reaches the handler") {
    struct row { char const* stmt; char const* message; };
    row const rows[] = {
        {"bsp(into=z, min=0)",              "bsp() min 0 is below 1; treating it as 1"},
        {"bsp(into=z, min=1, split=150)",   "bsp() split 150 is outside 0-100; clamping it"},
        {"voronoi(sites=s, into=z)",        "voronoi() found no site cell; writing every cell empty"},
        {"noise(into=z, lo=0, hi=9, scale=0)",   "noise() scale 0 is below 1; treating it as 1"},
        {"noise(into=z, lo=0, hi=9, scale=4, octaves=0)", "noise() octaves 0 is below 1"},
        {"noise(into=z, lo=0, hi=9, scale=4, octaves=5)", "noise() octaves 5 exceeds the 3 that scale 4 allows"},
        {"noise(into=z, lo=0, hi=9, scale=4, persistence=-1)", "noise() persistence -1 is outside 0-100"},
        {"noise(into=z, lo=9, hi=0, scale=4)", "noise() hi 0 is below lo 9; writing lo everywhere"},
    };
    std::string head = zone_decls + "\nsequence main {\n    resize(3, 3)\n    ";
    for (auto const& r : rows) {
        INFO(r.stmt);
        auto got = warnings_of(head + r.stmt + "\n}\n");
        REQUIRE(got.size() >= 1);
        CHECK(got[0].rfind("w.lvs:12:5: warning: " + std::string(r.message), 0) == 0);
    }
    auto conn = warnings_of(head + "voronoi(sites=(x == 0), into=z, connectivity=6)\n}\n");
    REQUIRE(conn.size() == 1);
    CHECK(conn[0].find("voronoi() connectivity 6 is not 4 or 8; treating it as 4") != std::string::npos);
    for (char const* op : {"bsp(into=z, min=1)", "voronoi(sites=s, into=z)",
                           "noise(into=z, lo=0, hi=1, scale=1)"}) {
        auto early = warnings_of(zone_decls + "\nsequence main {\n    " + op + "\n}\n");
        REQUIRE(early.size() == 1);
        CHECK(early[0].find("before any resize; leaving grids unchanged") != std::string::npos);
    }
    auto hilo = generate(sized(2, 1, "noise(into=z, lo=9, hi=0, scale=4)"));
    CHECK(show(hilo, "z") == "99");
}
