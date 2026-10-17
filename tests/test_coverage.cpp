// Coverage of the execution model's details: draw primitives, path at run
// time, expression totality, geometric operations, the write mask, and the
// generation API.
#include "test_support.hpp"
#include "machine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

using ts::make;
using ts::show;
using ts::stamp;
using ts::stamped;

namespace {

ls::level generate(std::string const& src, uint64_t seed = 1,
                   std::vector<std::pair<std::string, int>> const& params = {}) {
    auto gen = make(src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    return gen.generate(gen.sequence("main"), seed, params);
}

// Row `y` of number grid `name`; empty cells as `none`.
constexpr long long none = INT64_MIN;
std::vector<long long> row(ls::level const& lv, std::string const& name, int y = 0) {
    auto g = lv[name];
    std::vector<long long> out;
    for (int x = 0; x < lv.width(); ++x) out.push_back(g.is_empty(x, y) ? none : g.at(x, y));
    return out;
}

int count(std::string const& cells, char c) {
    int n = 0;
    for (char k : cells) n += k == c;
    return n;
}

const std::string path_decls = R"(
tag t { a, p }
tag terrain { f }
layers {
    g: grid of t
    m: grid of terrain
    n: grid of number
}
)";

}  // namespace

// ── draw primitives ──────────────────────────────────────────────────────────

TEST_CASE("coverage: uniform(n) rejects raw draws below 2^64 mod n") {
    auto stream = [](std::vector<uint64_t> raw, int& used) {
        return [raw, &used]() mutable { return raw[(size_t)used++]; };
    };
    int used = 0;
    // 2^64 mod 3 == 1: the raw 0 is rejected
    CHECK(ls::internal::uniform_draw(stream({0, 7}, used), 3) == 1);
    CHECK(used == 2);
    used = 0;
    CHECK(ls::internal::uniform_draw(stream({1}, used), 3) == 1);   // 1 is the threshold itself
    CHECK(used == 1);
    used = 0;
    CHECK(ls::internal::uniform_draw(stream({0}, used), 1) == 0);   // n = 1 never rejects
    CHECK(used == 1);
    // n = 2^63 + 1: the threshold is 2^63 - 1, so half the stream is rejected
    uint64_t n = (uint64_t{1} << 63) + 1;
    used = 0;
    CHECK(ls::internal::uniform_draw(stream({5, uint64_t{1} << 63}, used), n) == uint64_t{1} << 63);
    CHECK(used == 2);
}

TEST_CASE("coverage: a supplied input skips its default's draw") {
    // tests/golden/draws/params.lvs: k = random(1, 100), j = random(1, 100) + k
    std::string src = R"(
layers { n: grid of number }
params {
    k: number = random(1, 100)
    j = random(1, 100) + k
}
sequence main {
    resize(2, 1)
    once { where[ (x == 0) ] => n[ (k) ] }
    once { where[ (x == 1) ] => n[ (j) ] }
}
)";
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        auto drawn = row(generate(src, seed), "n");
        auto given = row(generate(src, seed, {{"k", 5}}), "n");
        CHECK(given[0] == 5);
        // with k supplied, j's draw is the run's first: the one k's default took
        CHECK(given[1] - 5 == drawn[0]);
    }
}

// ── path at run time ─────────────────────────────────────────────────────────

TEST_CASE("coverage: path connectivity 8 walks diagonals") {
    auto run = [&](int conn) {
        return show(generate(stamped(path_decls, "g", {"a . . .", ". . . .", ". . . .", ". . . p"},
            "path(from=a, to=p, into=g, write=a, passable=(x >= 0), connectivity=" +
            std::to_string(conn) + ")")), "g");
    };
    CHECK(run(8) == "a.../.a../..a./...a");
    CHECK(count(run(4), 'a') == 7);
}

TEST_CASE("coverage: path's default passable is any non-empty layer") {
    // only m's cells (and the endpoints) are traversable
    auto lv = generate(stamped(path_decls, "g", {"a . . . .", ". . . . .", ". . . . p"},
        stamp("m", {". f f f f", ". f . . .", "f f f f ."}) +
        "\n    path(from=a, to=p, into=n, write=1)"));
    CHECK(show(lv, "n") == "11.../.1.../.1111");   // through f cells only
}

TEST_CASE("coverage: path from several sources takes the nearest") {
    auto lv = generate(stamped(path_decls, "g", {"a . . . a . p"},
        "path(from=a, to=p, into=n, write=1, passable=(x >= 0))"));
    CHECK(show(lv, "n") == "....111");
}

TEST_CASE("coverage: path where from and to overlap is one cell") {
    auto lv = generate(stamped(path_decls, "g", {". a|p . a"},
        "path(from=a, to=p, into=n, write=1, passable=(x >= 0))"));
    CHECK(show(lv, "n") == ".1..");
}

TEST_CASE("coverage: path ties follow the seed, and only the seed") {
    std::string src = stamped(path_decls, "g", {"a . . .", ". . . .", ". . . .", ". . . p"},
                              "path(from=a, to=p, into=n, write=1, passable=(x >= 0))");
    std::set<std::string> routes;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        std::string r = show(generate(src, seed), "n");
        CHECK(r == show(generate(src, seed), "n"));
        CHECK(count(r, '1') == 7);
        routes.insert(r);
    }
    CHECK(routes.size() > 1);
}

TEST_CASE("coverage: path writes once per route cell, start to goal") {
    // one route only; reversing it reverses the order of the written draws
    auto run = [&](std::string const& line) {
        return row(generate(stamped(path_decls, "g", {line},
            "path(from=a, to=p, into=n, write=(random(0, 999)), passable=(x >= 0))"), 7), "n");
    };
    auto forward  = run("a . . . . p");
    auto backward = run("p . . . . a");
    std::vector<long long> reversed(backward.rbegin(), backward.rend());
    CHECK(forward == reversed);
    for (long long v : forward) CHECK((v >= 0 && v <= 999));
}

// ── expression totality ──────────────────────────────────────────────────────

TEST_CASE("coverage: arithmetic wraps and truncates at run time") {
    std::string cells[] = {
        "2147483647 + 1",            // overflow wraps
        "abs(-2147483647 - 1)",      // abs wraps at INT_MIN
        "-7 / 2", "7 / -2", "-7 / -2",   // truncation toward zero
        "min(3, -4)", "max(3, -4)",
        "clamp(12, 0, 9)", "clamp(-5, 0, 9)", "clamp(4, 0, 9)",
        "2147483647 * 2",
    };
    std::string body;
    int i = 0;
    for (auto const& c : cells)
        body += "\n    once { where[ (x == " + std::to_string(i++) + ") ] => n[ (" + c + ") ] }";
    auto lv = generate("layers { n: grid of number }\nsequence main {\n    resize(" +
                       std::to_string(i) + ", 1)" + body + "\n}\n");
    CHECK(row(lv, "n") == std::vector<long long>{-2147483648LL, -2147483648LL, -3, -3, 3, -4, 3, 9, 0, 4, -2});
}

// ── geometric operations ─────────────────────────────────────────────────────

namespace {
const std::string two = R"(
tag t { a, b, c, d }
layers {
    g: grid of t
    h: grid of t
}
)";
}  // namespace

TEST_CASE("coverage: resize keeps content at the top left") {
    CHECK(show(generate(stamped(two, "g", {"a b c", "d a b"}, "resize(5, 3)")), "g") ==
          "abc../dab../.....");
    CHECK(show(generate(stamped(two, "g", {"a b c", "d a b"}, "resize(2, 1)")), "g") == "ab");
    // shrink, then grow: the cropped cells are gone
    CHECK(show(generate(stamped(two, "g", {"a b c", "d a b"}, "resize(2, 1)\n    resize(3, 2)")),
               "g") == "ab./...");
}

TEST_CASE("coverage: mirror on odd sizes keeps the middle") {
    CHECK(show(generate(stamped(two, "g", {"a b c d b"}, "mirror(horizontal)")), "g") == "abcba");
    CHECK(show(generate(stamped(two, "g", {"a", "b", "c", "d", "c"}, "mirror(vertical)")), "g") ==
          "a/b/c/b/a");
    // an even size has no middle
    CHECK(show(generate(stamped(two, "g", {"a b c d"}, "mirror(horizontal)")), "g") == "abba");
}

TEST_CASE("coverage: upscale duplicates every layer") {
    auto lv = generate(stamped(two, "g", {"a b"}, stamp("h", {"c ."}) + "\n    upscale(2, 3)"));
    CHECK(show(lv, "g") == "aabb/aabb/aabb");
    CHECK(show(lv, "h") == "cc../cc../cc..");
}

TEST_CASE("coverage: pad writes empty around every layer") {
    auto lv = generate(stamped(two, "g", {"a b"}, stamp("h", {"c d"}) + "\n    pad(1)"));
    CHECK(show(lv, "g") == "..../.ab./....");
    CHECK(show(lv, "h") == "..../.cd./....");
}

TEST_CASE("coverage: trim takes the union of every layer's content") {
    auto lv = generate(stamped(two, "g", {". . . . .", ". a . . .", ". . . . .", ". . . . ."},
        stamp("h", {". . . . .", ". . . . .", ". . . b .", ". . . . ."}) + "\n    trim()"));
    CHECK(lv.width() == 3);
    CHECK(lv.height() == 2);
    CHECK(show(lv, "g") == "a../...");
    CHECK(show(lv, "h") == ".../..b");
}

// ── the write mask ───────────────────────────────────────────────────────────

TEST_CASE("coverage: candidates overlapping only on '*' cells both apply") {
    // anchors 0 and 1 write cells {0, 2} and {1, 3}; each one's '*' is the other's write
    auto lv = generate("tag t { a }\nlayers { g: grid of t }\n"
                       "sequence main {\n    resize(4, 1)\n"
                       "    everywhere { g[. * .] => g[a * a] }\n}\n");
    CHECK(show(lv, "g") == "aaaa");
}

TEST_CASE("coverage: writes to one cell of different grids both apply") {
    auto lv = generate(two + "sequence main {\n    resize(3, 1)\n"
                       "    everywhere { all\n        g[.] => g[a]\n        h[.] => h[b]\n    }\n}\n");
    CHECK(show(lv, "g") == "aaa");
    CHECK(show(lv, "h") == "bbb");
}

TEST_CASE("coverage: skipped candidates don't spend scatter's budget") {
    // two candidates per cell, writing the same cell: one of each pair is skipped
    std::string src = two + "sequence main {\n    resize(5, 1)\n"
                      "    scatter(3) { all\n        g[.] => g[a]\n        g[.] => g[b]\n    }\n}\n";
    for (uint64_t seed = 1; seed <= 20; ++seed)
        CHECK(count(show(generate(src, seed), "g"), '.') == 2);
}

// ── the generation API ───────────────────────────────────────────────────────

namespace {
const std::string staged = R"(
tag t { a, b }
layers { g: grid of t }
sequence main {
    resize(6, 4)
    scatter(5) { g[.] => g[a] }
    everywhere { g[a .] => g[a b] }
    scatter(50%) { g[.] => g[b] }
}
)";
}  // namespace

TEST_CASE("coverage: finish() after partial stepping equals generate") {
    auto gen = make(staged);
    REQUIRE(static_cast<bool>(gen));
    int main = gen.sequence("main");
    for (int steps = 0; steps <= 6; ++steps) {
        auto r = gen.run(main, 9, ls::step_mode::application);
        for (int i = 0; i < steps; ++i) r.step();
        CHECK(show(r.finish(), "g") == show(gen.generate(main, 9), "g"));
    }
}

TEST_CASE("coverage: a snapshot at a statement boundary is that prefix's level") {
    auto gen = make(staged);
    REQUIRE(static_cast<bool>(gen));
    auto r = gen.run(gen.sequence("main"), 9, ls::step_mode::statement);
    REQUIRE(r.step());   // resize
    REQUIRE(r.step());   // scatter(5)
    REQUIRE(r.at_statement_boundary());
    std::string prefix = R"(
tag t { a, b }
layers { g: grid of t }
sequence main {
    resize(6, 4)
    scatter(5) { g[.] => g[a] }
}
)";
    CHECK(show(r.snapshot(), "g") == show(generate(prefix, 9), "g"));
}

TEST_CASE("coverage: a run dropped mid-way leaves its generator usable") {
    auto gen = make(staged);
    REQUIRE(static_cast<bool>(gen));
    int main = gen.sequence("main");
    {
        auto r = gen.run(main, 9, ls::step_mode::application);
        r.step();
        r.step();
        auto kept = r.snapshot();   // a snapshot outlives its run
        (void)kept;
    }
    ls::level kept;
    {
        auto r = gen.run(main, 9, ls::step_mode::application);
        for (int i = 0; i < 3; ++i) r.step();
        kept = r.snapshot();
    }
    CHECK(kept.width() == 6);
    CHECK(show(gen.generate(main, 9), "g") == show(gen.run(main, 9).finish(), "g"));
}

TEST_CASE("coverage: invalid entries are inert") {
    auto gen = make(staged);
    REQUIRE(static_cast<bool>(gen));
    for (int bad : {-1, 1, 99}) {
        CHECK(gen.generate(bad, 1).width() == 0);
        CHECK(gen.generate(bad, 1).layer_count() == 0);
        auto r = gen.run(bad, 1);
        CHECK_FALSE(r.step());
        CHECK(r.finish().width() == 0);
        CHECK(gen.statement_count(bad) == 0);
    }
}
