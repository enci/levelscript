// Modules and entries (spec 0.7: section 2.6 modules, section 6 entries, Appendix A).
// Every test resolves through an in-memory file map; a module's canonical
// name is its path as written.
#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

namespace {

struct files {
    std::map<std::string, std::string> src;
    mutable std::map<std::string, int> calls;   // resolve() count per path

    ls::resolver resolver() const {
        return [this](std::string const& path, std::string const&)
                   -> std::optional<ls::module_source> {
            ++calls[path];
            auto it = src.find(path);
            if (it == src.end()) return std::nullopt;
            return ls::module_source{path, it->second};
        };
    }
    ls::generator compile(std::string const& root) const {
        return ls::generator::compile(src.at(root), root, resolver());
    }
};

bool has(std::string const& text, std::string const& needle) {
    return text.find(needle) != std::string::npos;
}

// The section 2.6 example: dungeon.lvs -> carve.lvs -> schema.lvs.
files spec_example() {
    files f;
    f.src["schema.lvs"] = "tag algo { R, W }\nlayers { level: grid of algo }\n";
    f.src["carve.lvs"] =
        "use \"schema.lvs\"\n"
        "rule seed_room { level[.] => level[R] }\n"
        "sequence carve {\n    resize(16, 7)\n    once seed_room\n}\n";
    f.src["dungeon.lvs"] = "use \"carve.lvs\"\nsequence main {\n    once carve\n}\n";
    return f;
}

}  // namespace

// ── section 2.6: the closure, canonical order, visibility ────────────────────────────

TEST_CASE("modules: the spec example compiles and runs") {
    auto f = spec_example();
    auto gen = f.compile("dungeon.lvs");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    CHECK(gen.modules() == std::vector<std::string>{"schema.lvs", "carve.lvs", "dungeon.lvs"});
    auto lv = gen.generate(gen.sequence("main"), 1);
    CHECK(lv.width() == 16);
    CHECK(lv.height() == 7);
    int r = 0;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 16; ++x)
            if (lv["level"].at(x, y) == gen.tag("algo.R")) ++r;
    CHECK(r == 1);
}

TEST_CASE("modules: a use is not re-exported (D3)") {
    auto f = spec_example();
    // dungeon.lvs writes `level`, declared in schema.lvs, which it does not use
    f.src["dungeon.lvs"] =
        "use \"carve.lvs\"\nrule mark { level[R] => level[W] }\n"
        "sequence main {\n    once carve\n    everywhere mark\n}\n";
    auto gen = f.compile("dungeon.lvs");
    CHECK(!gen);
    CHECK(has(gen.error(), "dungeon.lvs:2:"));
    CHECK(has(gen.error(), "grid 'level' is not visible here: it is declared in module "
                           "'schema.lvs', which this module does not use"));
    // using schema.lvs directly fixes it
    f.src["dungeon.lvs"] = "use \"schema.lvs\"\n" + f.src["dungeon.lvs"];
    CHECK(static_cast<bool>(f.compile("dungeon.lvs")));
}

TEST_CASE("modules: every kind of name goes through visibility (check 8)") {
    files f;
    f.src["lib.lvs"] =
        "tag t { a }\nlayers { g: grid of t }\nparams { k: number = 2 }\n"
        "rule fill { g[.] => g[a] }\nsequence s { everywhere fill }\n";
    f.src["mid.lvs"] = "use \"lib.lvs\"\n";
    auto check = [&](std::string body, char const* kind, char const* name) {
        f.src["root.lvs"] = "use \"mid.lvs\"\n" + body;
        auto gen = f.compile("root.lvs");
        INFO(body);
        CHECK(has(gen.error(), std::string(kind) + " '" + name + "' is not visible here"));
    };
    check("layers { h: grid of t }\n", "tagset", "t");
    check("rule r { g[.] => g[a] }\n", "grid", "g");
    check("sequence main { resize(1, 1)  once fill }\n", "rule", "fill");
    check("sequence main { once s }\n", "sequence", "s");
    check("params { q = k + 1 }\n", "param", "k");
    check("sequence main { resize(1, 1)  path(from=a, to=a, into=g, write=a) }\n", "grid", "g");
}

TEST_CASE("modules: tag values need no visibility of their own") {
    files f;
    f.src["schema.lvs"] = "tag t { a, b }\nlayers { g: grid of t }\n";
    f.src["root.lvs"] =
        "use \"schema.lvs\"\n"   // sees g; `a`/`b` resolve through g's tagset
        "rule r { g[.] => g[ (if(x == 0, a, b)) ] }\nsequence main { resize(2, 1)  everywhere r }\n";
    INFO(f.compile("root.lvs").error());
    CHECK(static_cast<bool>(f.compile("root.lvs")));
}

TEST_CASE("modules: canonical order is a post-order walk; a shared module loads once") {
    files f;   // a diamond: root -> left, right -> base
    f.src["base.lvs"]  = "tag t { a }\nlayers { g0: grid of t }\nsequence s0 { }\n";
    f.src["left.lvs"]  = "use \"base.lvs\"\nlayers { g1: grid of t }\nsequence s1 { }\n";
    f.src["right.lvs"] = "use \"base.lvs\"\nlayers { g2: grid of t }\nsequence s2 { }\n";
    f.src["root.lvs"]  = "use \"left.lvs\"\nuse \"right.lvs\"\nlayers { g3: grid of number }\n"
                        "sequence main { resize(1, 1) }\n";
    auto gen = f.compile("root.lvs");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    CHECK(gen.modules() == std::vector<std::string>{"base.lvs", "left.lvs", "right.lvs", "root.lvs"});
    CHECK(f.calls["base.lvs"] == 2);   // resolved from both sides...
    // ...but loaded once: layer order and sequence ids follow canonical order
    auto lv = gen.generate(gen.sequence("main"), 1);
    REQUIRE(lv.layer_count() == 4);
    CHECK(lv.layer_name(0) == "g0");
    CHECK(lv.layer_name(1) == "g1");
    CHECK(lv.layer_name(2) == "g2");
    CHECK(lv.layer_name(3) == "g3");
    REQUIRE(gen.sequence_count() == 4);
    CHECK(gen.sequence_name(0) == "s0");
    CHECK(gen.sequence_name(3) == "main");
    CHECK(gen.sequence_name(4).empty());
}

TEST_CASE("modules: params evaluate in canonical order across modules") {
    files f;
    f.src["base.lvs"] = "params { r1: number = random(0, 1000000) }\n";
    f.src["root.lvs"] = "use \"base.lvs\"\nparams { r2: number = random(0, 1000000)\n"
                       "both = r1 * 0 + r2 }\n"
                       "layers { n: grid of number }\nrule w { n[.] => n[ (both) ] }\n"
                       "sequence main { resize(1, 1)  everywhere w }\n";
    // r1's draw comes first: base.lvs precedes root.lvs in canonical order. The
    // same declarations in one file, in that order, give the same output.
    files one;
    one.src["one.lvs"] = "params { r1: number = random(0, 1000000)\n"
                        "r2: number = random(0, 1000000)\nboth = r1 * 0 + r2 }\n"
                        "layers { n: grid of number }\nrule w { n[.] => n[ (both) ] }\n"
                        "sequence main { resize(1, 1)  everywhere w }\n";
    auto a = f.compile("root.lvs"), b = one.compile("one.lvs");
    INFO(a.error() << b.error());
    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    for (uint64_t seed = 1; seed <= 10; ++seed)
        CHECK(a.generate(a.sequence("main"), seed)["n"].at(0, 0) ==
              b.generate(b.sequence("main"), seed)["n"].at(0, 0));
}

TEST_CASE("modules: a path predicate considers only the grids its module sees (6.6)") {
    files f;
    f.src["a.lvs"] = "tag ta { v }\nlayers { ga: grid of ta }\n";
    f.src["b.lvs"] = "tag tb { v }\nlayers { gb: grid of tb }\n";
    f.src["hidden.lvs"] = "use \"b.lvs\"\n";
    f.src["root.lvs"] = "use \"a.lvs\"\nuse \"hidden.lvs\"\nsequence main {\n    resize(3, 1)\n"
                       "    path(from=v, to=v, into=ga, write=v)\n}\n";
    // gb could hold `v` too, but root.lvs does not see b.lvs: not ambiguous
    INFO(f.compile("root.lvs").error());
    CHECK(static_cast<bool>(f.compile("root.lvs")));
    f.src["root.lvs"] = "use \"a.lvs\"\nuse \"b.lvs\"\nsequence main {\n    resize(3, 1)\n"
                       "    path(from=v, to=v, into=ga, write=v)\n}\n";
    CHECK(has(f.compile("root.lvs").error(), "is ambiguous"));
}

// ── section 7.3, checks 9, 39-43 ──────────────────────────────────────────────────────

TEST_CASE("modules: an unresolved use is check 9") {
    files f;
    f.src["root.lvs"] = "use \"missing.lvs\"\nsequence main { }\n";
    auto gen = f.compile("root.lvs");
    CHECK(has(gen.error(), "root.lvs:1:1: error: cannot resolve module \"missing.lvs\""));
    // with no resolver at all, every use is unresolved
    auto bare = ls::generator::compile("use \"x.lvs\"\n", "root.lvs");
    CHECK(has(bare.error(), "cannot resolve module \"x.lvs\""));
    CHECK(bare.modules() == std::vector<std::string>{"root.lvs"});   // resolved so far
}

TEST_CASE("modules: use cycles are check 40") {
    files f;
    f.src["self.lvs"] = "use \"self.lvs\"\n";
    CHECK(has(f.compile("self.lvs").error(), "module cycle: 'self.lvs' -> 'self.lvs'"));
    f.src["a.lvs"] = "use \"b.lvs\"\n";
    f.src["b.lvs"] = "use \"c.lvs\"\n";
    f.src["c.lvs"] = "use \"a.lvs\"\n";
    auto gen = f.compile("a.lvs");
    CHECK(has(gen.error(), "c.lvs:1:1: error: module cycle: 'a.lvs' -> 'b.lvs' -> 'c.lvs' -> 'a.lvs'"));
    // a failed compile still lists what it resolved, for hot reload
    CHECK(gen.modules() == std::vector<std::string>{"c.lvs", "b.lvs", "a.lvs"});
}

TEST_CASE("modules: using one module twice is check 41") {
    files f;
    f.src["lib.lvs"] = "tag t { a }\n";
    f.src["root.lvs"] = "use \"lib.lvs\"\nuse \"lib.lvs\"\n";
    CHECK(has(f.compile("root.lvs").error(), "root.lvs:2:1: error: module 'lib.lvs' is already used"));
    // two spellings of one module: the resolver's canonical name decides
    files g;
    g.src["root.lvs"] = "use \"lib.lvs\"\nuse \"./lib.lvs\"\n";
    auto canon = [&](std::string const& p, std::string const&) -> std::optional<ls::module_source> {
        std::string name = p.rfind("./", 0) == 0 ? p.substr(2) : p;
        if (name != "lib.lvs") return std::nullopt;
        return ls::module_source{name, "tag t { a }\n"};
    };
    auto gen = ls::generator::compile(g.src["root.lvs"], "root.lvs", canon);
    CHECK(has(gen.error(), "module 'lib.lvs' is already used"));
}

TEST_CASE("modules: names are unique across the closure (checks 39, 42)") {
    files f;
    f.src["lib.lvs"] = "tag t { a }\nlayers { g: grid of t }\nparams { k: number = 1 }\n"
                      "rule r { g[.] => g[a] }\n";
    auto dup = [&](std::string body) {
        f.src["root.lvs"] = "use \"lib.lvs\"\n" + body;
        return f.compile("root.lvs").error();
    };
    CHECK(has(dup("tag t { b }\n"), "root.lvs:2:1: error: duplicate tag 't'"));
    CHECK(has(dup("layers { g: grid of t }\n"), "duplicate grid 'g'"));
    CHECK(has(dup("params { k: number = 2 }\n"), "duplicate param 'k'"));
    CHECK(has(dup("rule r { g[.] => g[a] }\n"), "duplicate rule 'r'"));
    CHECK(has(dup("sequence r { }\n"), "already declared as a rule"));
}

TEST_CASE("modules: one layers block per module (check 43)") {
    CHECK(ts::compile_result("layers { }\nlayers { }\n").has_error("only one 'layers' block per module"));
}

TEST_CASE("modules: diagnostics are labelled with the declaring module") {
    files f;
    f.src["lib.lvs"] = "rule broken { g[.] => g[a] }\n";   // lib has no `g`
    f.src["root.lvs"] = "use \"lib.lvs\"\n";
    auto gen = f.compile("root.lvs");
    CHECK(has(gen.error(), "lib.lvs:1:"));
    CHECK(!has(gen.error(), "root.lvs:1:"));
}

TEST_CASE("modules: a module compiles identically on its own (2.6)") {
    auto f = spec_example();
    auto alone = f.compile("carve.lvs");   // a dependency, compiled as the root
    INFO(alone.error());
    REQUIRE(static_cast<bool>(alone));
    CHECK(alone.sequence("main") == -1);
    CHECK(alone.generate(alone.sequence("carve"), 1).width() == 16);
}

// ── section 6 entries ────────────────────────────────────────────────────────────────

static char const* two_entries = R"(
layers { tiles: grid of number }
rule one_up { tiles[*] => tiles[ (tiles + 1) ] }
sequence small {
    resize(1, 1)
}
sequence main {
    resize(2, 2)
    everywhere one_up
    once small
}
)";

TEST_CASE("entries: any sequence can be the entry") {
    auto gen = ts::make(two_entries);
    REQUIRE(static_cast<bool>(gen));
    auto m = gen.generate(gen.sequence("main"), 1);
    CHECK(m.width() == 1);   // main ends by applying `small`
    CHECK(m["tiles"].at(0, 0) == 1);
    auto s = gen.generate(gen.sequence("small"), 1);
    CHECK(s.width() == 1);
    CHECK(s["tiles"].is_empty(0, 0));
    CHECK(gen.statement_count(gen.sequence("main")) == 3);
    CHECK(gen.statement_count(gen.sequence("small")) == 1);
    CHECK(gen.statement_count(-1) == 0);
    CHECK(gen.statement_count(99) == 0);
}

TEST_CASE("entries: the statement stack counts from the entry's body") {
    auto gen = ts::make(two_entries);
    auto r = gen.run(gen.sequence("main"), 1, ls::step_mode::statement, ls::observe::on);
    std::vector<std::vector<ls::stmt_frame>> stacks;
    while (r.step()) stacks.push_back(r.stmt_stack());
    REQUIRE(stacks.size() == 3);   // resize, all one_up, small's resize
    CHECK(stacks[0].size() == 1);
    CHECK(stacks[0][0].index == 0);
    CHECK(stacks[1][0].index == 1);
    REQUIRE(stacks[2].size() == 2);   // inside `small`
    CHECK(stacks[2][0].index == 2);
    CHECK(stacks[2][1].index == 0);
}

// ── run::stop_at_begin: stop *before* each statement (debuggers) ─────────────

TEST_CASE("run: stop_at_begin also stops before every statement") {
    auto gen = ts::make(R"(
layers { tiles: grid of number }
params { off: number = 0 }
rule one_up { tiles[*] => tiles[ (tiles + 1) ] }
sequence inner {
    once one_up
}
sequence main {
    resize(1, 1)
    settle(2) inner
    pad(1)  when (off == 1)
}
)");
    REQUIRE(static_cast<bool>(gen));
    auto r = gen.run(gen.sequence("main"), 1, ls::step_mode::application, ls::observe::on);
    r.stop_at_begin(true);
    std::vector<std::string> events;
    while (r.step()) {
        std::string p;
        for (auto const& f : r.stmt_stack())
            p += (p.empty() ? "" : ".") + std::to_string(f.index) + "@" + std::to_string(f.iteration);
        char k = r.at_statement_begin() ? 'B' : r.at_statement_boundary() ? 'E' : 'A';
        if (k == 'B') CHECK(r.highlights().empty());   // nothing matched yet
        events.push_back(std::string(1, k) + " " + p);
    }
    CHECK(events == std::vector<std::string>{
        "B 0@0", "E 0@0",                      // resize: begin, done
        "B 1@0",                               // the sequence application begins
        "B 1@0.0@0", "A 1@0.0@0", "E 1@0.0@0", // inner, iteration 0
        "B 1@0.0@1", "A 1@0.0@1", "E 1@0.0@1", // inner, iteration 1
        "E 2@0",                               // pad's guard is false: no begin
    });
}

TEST_CASE("run: begin events are off by default") {
    auto gen = ts::make("layers { }\nsequence main { resize(1, 1)  resize(2, 2) }\n");
    auto r = gen.run(gen.sequence("main"), 1, ls::step_mode::application, ls::observe::on);
    int steps = 0;
    while (r.step()) { CHECK(!r.at_statement_begin()); ++steps; }
    CHECK(steps == 2);
}

TEST_CASE("modules: an inline rule resolves names in its sequence's module (0.8, section 6)") {
    files f;
    f.src["schema.lvs"] = "tag t { a }\nlayers { g: grid of t }\n";
    f.src["walls.lvs"] = "use \"schema.lvs\"\nsequence fill { everywhere { g[.] => g[a] } }\n";
    f.src["main.lvs"] = "use \"walls.lvs\"\nsequence main { resize(2, 2) once fill }\n";
    auto gen = f.compile("main.lvs");
    INFO(gen.error());
    CHECK(static_cast<bool>(gen));
    // main.lvs does not use schema.lvs, so it cannot see grid g
    f.src["main.lvs"] = "use \"walls.lvs\"\nsequence main { resize(2, 2) everywhere { g[.] => g[a] } }\n";
    CHECK(has(f.compile("main.lvs").error(), "not visible"));
}
