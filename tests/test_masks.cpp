// Tags revisit (0.16.0): uniform complement (`!m` includes empty), named
// masks as mask expressions, and the `is` match operator.
#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

using ts::compile_result;
using ts::make;
using ts::show;
using ts::stamped;

namespace {

ls::level run(std::string const& src) {
    auto gen = make(src);
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    return gen.generate(gen.sequence("main"), 1, {});
}

// S = a|b is the "solid" pair; free and ground are its two complements.
const std::string decls = R"(
tag t {
    a, b, c, d
    S      = a | b
    free   = !S          // c, d, or empty
    ground = !(S | .)    // c or d: neither solid nor empty
    none   = .
}
layers {
    g: grid of t
    h: grid of t
    n: grid of number
}
)";

void ok(std::string const& src) {
    compile_result r(src);
    INFO(src);
    INFO(r.diags.format_all());
    CHECK(r.ok);
}

}  // namespace

TEST_CASE("masks: named masks resolve to their expression") {
    compile_result r(decls + "sequence main { }\n");
    INFO(r.diags.format_all());
    REQUIRE(r.ok);
    int64_t a = ls::tag_bit(0), b = ls::tag_bit(1), c = ls::tag_bit(2), d = ls::tag_bit(3);
    CHECK(r.prog.mask_of(0, "S") == (a | b));
    CHECK(r.prog.mask_of(0, "free") == (ls::tag_empty | c | d));
    CHECK(r.prog.mask_of(0, "ground") == (c | d));
    CHECK(r.prog.mask_of(0, "none") == ls::tag_empty);
    CHECK(r.prog.named(0, "S")->writable());
    CHECK(r.prog.named(0, "none")->writable());
    CHECK_FALSE(r.prog.named(0, "free")->writable());
    CHECK_FALSE(r.prog.named(0, "ground")->writable());
}

TEST_CASE("masks: complement includes empty") {
    // `!a` matches an empty cell, so `!a|.` is the same mask
    CHECK(show(run(stamped(decls, "g", {". a b"}, "everywhere { g[!a] => g[c] }")), "g") == "cac");
    CHECK(show(run(stamped(decls, "g", {". a b"}, "everywhere { g[!a|.] => g[c] }")), "g") == "cac");
    // `!.` still means non-empty
    CHECK(show(run(stamped(decls, "g", {". a b"}, "everywhere { g[!.] => g[c] }")), "g") == ".cc");
}

TEST_CASE("masks: named masks in cells") {
    CHECK(show(run(stamped(decls, "g", {"a . c b"}, "everywhere { g[free] => g[d] }")), "g") == "addb");
    CHECK(show(run(stamped(decls, "g", {"a . c b"}, "everywhere { g[ground] => g[a] }")), "g") == "a.ab");
    CHECK(show(run(stamped(decls, "g", {"a . c b"}, "everywhere { g[S] => g[none] }")), "g") == "..c.");
    // a complement of a named mask
    CHECK(show(run(stamped(decls, "g", {"a . c b"}, "everywhere { g[!free] => g[d] }")), "g") == "d.cd");
}

TEST_CASE("masks: is matches like a cell") {
    // in a where cell, against a value, a union, a named mask, and '.'
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (g is S) ] => h[c] }")), "h") == "c..c");
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (g is c|.) ] => h[c] }")), "h") == ".cc.");
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (g is free && x > 1) ] => h[c] }")), "h") == "..c.");
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (g is .) ] => h[c] }")), "h") == ".c..");
    // `!(g is a)` negates the match; `!g is a` complements the read
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (!(g is a)) ] => h[c] }")), "h") == ".ccc");
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (!g is a) ] => h[c] }")), "h") == ".ccc");
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { where[ (!g is .) ] => h[c] }")), "h") == "c.cc");
    // in a computed write cell, `is` may test a complement
    CHECK(show(run(stamped(decls, "g", {"a . c b"},
        "everywhere { g[*] => h[ (if(g is !S, c, d)) ] }")), "h") == "dccd");
}

TEST_CASE("masks: is binds between && and |") {
    ok(decls + "rule r { where[ (g is a|b && x > 0) ] => h[c] }\n");
    compile_result chained(decls + "rule r { where[ (g is a is b) ] => h[c] }\n");
    CHECK(chained.has_error("'is' does not chain"));
}

TEST_CASE("masks: is is reserved") {
    CHECK(compile_result("tag t { a, is }\nsequence main { }\n").has_error("reserved keyword"));
    CHECK(compile_result("tag t { a, m = is }\nsequence main { }\n").has_error("reserved keyword"));
}

TEST_CASE("masks: is takes tag operands") {
    // check 14
    CHECK(compile_result(decls + "rule r { where[ (n is 3) ] => h[c] }\n")
          .has_error("'is' takes tag operands"));
    CHECK(compile_result("tag t { a }\ntag u { p }\nlayers { g: grid of t\n k: grid of u }\n"
                         "rule r { where[ (g is k) ] => g[a] }\n")
          .has_error("within one tagset"));
}

TEST_CASE("masks: a complement is never written") {
    // check 15
    CHECK(compile_result(decls + "rule r { g[a] => g[free] }\n").has_error("match-only"));
    CHECK(compile_result(decls + "rule r { g[a] => g[(free)] }\n").has_error("match-only"));
    CHECK(compile_result(decls + "rule r { g[a] => g[(!a)] }\n")
          .has_error("not allowed on the write side"));
    CHECK(compile_result(decls + "rule r { g[a] => g[(if(x > 0, !a, a))] }\n")
          .has_error("not allowed on the write side"));
    CHECK(compile_result(decls + "rule r { g[a] => g[ground] }\n").has_error("match-only"));
    // a writable named mask writes, and an `is` operand is a test
    ok(decls + "rule r { g[a] => g[S] }\n");
    ok(decls + "rule r { g[a] => g[none] }\n");
    ok(decls + "rule r { g[a] => g[(if(g is free, c, d))] }\n");
    // match side: complements and match-only names are fine
    ok(decls + "rule r { g[free] => g[c] }\n");
    ok(decls + "rule r { g[(!a)] => g[c] }\n");
}

TEST_CASE("masks: '.' joins no write-side union through a name") {
    // check 47
    CHECK(compile_result(decls + "rule r { g[a] => g[none|a] }\n").has_error("'.' cannot be written"));
    CHECK(compile_result("tag t { a, e = a | . }\nlayers { g: grid of t }\n"
                         "rule r { g[a] => g[e] }\n")
          .has_error("'.' cannot be written"));
}

TEST_CASE("masks: named mask members") {
    // check 25
    CHECK(compile_result("tag t { a, m = !(a | nope) }\nsequence main { }\n")
          .has_error("not a value or earlier named mask"));
    CHECK(compile_result("tag t { a, m = !k, k = a }\nsequence main { }\n")
          .has_error("not a value or earlier named mask"));
    CHECK(compile_result("tag t { a, m = (a | }\nsequence main { }\n")
          .has_error("in a named mask"));
}

TEST_CASE("masks: a node test has no '.' through a name") {
    // check 58
    std::string world = R"(
tag room { Hall, Inn, open = !Hall, dot = Hall | . }
layers {
    rooms: grid of room
    plan:  graph of rooms
}
)";
    CHECK(compile_result(world + "rule r { plan { ?n[dot] } => rooms(?n)[Inn] }\n")
          .has_error("a node is never empty"));
    // `!Hall` covers empty too, but has no '.' atom
    ok(world + "rule r { plan { ?n[open] } => rooms(?n)[Inn] }\n");
}

TEST_CASE("masks: a mask that selects nothing") {
    // check 59
    CHECK(compile_result("tag t { F, W, m = !(F | W | .) }\nsequence main { }\n")
          .has_error("selects nothing"));
    CHECK(compile_result("tag t { F, W, full = F | W | . }\nlayers { g: grid of t }\n"
                         "rule r { g[!full] => g[F] }\n")
          .has_error("selects nothing"));
}
