#include "directives.hpp"
#include "inspect.hpp"
#include "lexer.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace ls;

// Tool directives (Appendix B): `// @key=value` comments read by the tools.

static directive_set read(std::string const& src) { return read_directives(src, "test.lvs"); }

static bool warns(directive_set const& d, std::string const& needle) {
    for (auto const& w : d.warnings.all)
        if (w.message.find(needle) != std::string::npos) return true;
    return false;
}

TEST_CASE("directives: the lexer keeps directive comments only") {
    diagnostics diags;
    std::vector<directive_comment> dc;
    auto toks = lex("// plain\n// @seed=4\na // @play=1\n\n// see @seed\nb", "t", diags, &dc);
    REQUIRE(dc.size() == 2);
    CHECK(dc[0].line == 2);
    CHECK(dc[0].leading);
    CHECK(!dc[0].trailing);
    CHECK(dc[0].code_line == 3);
    CHECK(dc[1].trailing);
    CHECK(dc[1].code_line == 3);
    CHECK(dc[1].text == " @play=1");
    CHECK(toks.size() == 8);   // \n \n a \n \n \n b <end> - comments stay out of the stream
}

TEST_CASE("directives: the spec's examples attach") {
    auto d = read(R"(// @seed=42
// Fill-and-carve dungeon.
tag algo {
    F   // @name=floor
    W   // @name=wall
}
layers {
    level:   grid of algo
    tiles:   grid of number
}
sequence main {
    resize(60, 40)
    everywhere { level[.] => level[W] }   // @play=0
    // @play=100

    // a note
    everywhere { level[W] => level[F] }
}
)");
    CHECK(d.warnings.all.empty());
    CHECK(d.seed() == 42u);
    REQUIRE(d.find("name", directive_target::tag_value, "algo", "F"));
    CHECK(d.find("name", directive_target::tag_value, "algo", "F")->value == "floor");
    CHECK(d.find("name", directive_target::tag_value, "algo", "W")->value == "wall");
    CHECK(d.find("play", directive_target::statement, "main", "", 1)->number == 0);
    CHECK(d.find("play", directive_target::statement, "main", "", 2)->number == 100);   // stacked past a comment
}

TEST_CASE("directives: malformed comments, unknown keys and bad values warn") {
    auto d = read("// @seed 42\n// @seed=4 see notes\n// @frob=1 @seed=x\ntag t { a }\n");
    CHECK(d.all.empty());
    CHECK(warns(d, "malformed directive comment"));
    CHECK(warns(d, "unknown directive '@frob'"));
    CHECK(warns(d, "'@seed' takes a number, got 'x'"));
    CHECK(read("// @seed=18446744073709551615\ntag t { a }\n").seed() == UINT64_MAX);
    CHECK(warns(read("// @seed=18446744073709551616\ntag t { a }\n"), "out of range"));
    CHECK(warns(read("tag t {\n a // @name=1\n}\n"), "takes a name"));
}

TEST_CASE("directives: @seed only before the first declaration") {
    auto d = read("tag t { a }\n// @seed=4\n");
    CHECK(!d.seed());
    CHECK(warns(d, "before the first declaration"));
    CHECK(warns(read("tag t { a } // @seed=4\n"), "before the first declaration"));
}

TEST_CASE("directives: no target, or an ambiguous one") {
    CHECK(warns(read("tag t { a, b } // @name=x\n"), "ambiguous: 2 tag values"));
    CHECK(warns(read("// @name=x\ntag t {\n a\n}\n"), "none starts on line 2"));
    CHECK(warns(read("tag t {\n a\n}\n// @name=x\n"), "none starts after it"));
    CHECK(warns(read("tag t { a }\nlayers {\n g: grid of t // @play=1\n}\n"), "needs a statement"));
    // tiles are the tools' settings, not the script's
    CHECK(warns(read("tag t { a }\nlayers {\n g: grid of t // @tileset=s\n}\n"),
                "unknown directive '@tileset'"));
    CHECK(warns(read("tag t {\n a // @tile=3\n}\n"), "unknown directive '@tile'"));
}

TEST_CASE("directives: the same key twice - the last one applies") {
    auto d = read("tag t {\n // @name=x\n a // @name=y\n}\n");
    CHECK(warns(d, "duplicate '@name'"));
    CHECK(d.find("name", directive_target::tag_value, "t", "a")->value == "y");
    CHECK(d.all.size() == 1);
}

TEST_CASE("directives: equal display names within one tagset warn") {
    CHECK(warns(read("tag t {\n a // @name=x\n b // @name=x\n}\n"), "already the display name"));
    CHECK(read("tag t {\n a // @name=x\n}\ntag u {\n a // @name=x\n}\n").warnings.all.empty());
}

TEST_CASE("directives: @seed in a used module is ignored") {
    resolver r = [](std::string const& path, std::string const&) -> std::optional<module_source> {
        if (path != "lib.lvs") return std::nullopt;
        return module_source{"lib.lvs", "// @seed=9\ntag t {\n a // @name=alpha\n}\n"};
    };
    auto d = read_directives("use \"lib.lvs\"\n", "root.lvs", r);
    CHECK(!d.seed());
    CHECK(warns(d, "root file only"));
    CHECK(d.find("name", directive_target::tag_value, "t", "a"));   // other keys work in any module
}

TEST_CASE("directives: inspect reports directives and their warnings apart") {
    std::string j = inspect_json("// @seed=7\ntag t {\n a // @name=alpha\n}\n// @bogus=1\n", "test.lvs");
    CHECK(j.find("\"directives\":[{\"key\":\"seed\",\"value\":\"7\"") != std::string::npos);
    CHECK(j.find("\"target\":{\"kind\":\"tag_value\",\"range\":{\"line\":3,\"col\":2,\"end_line\":3,"
                 "\"end_col\":3},\"owner\":\"t\",\"name\":\"a\"}") != std::string::npos);
    CHECK(j.find("\"directive_warnings\":[{\"line\":5") != std::string::npos);
    CHECK(j.find("\"diagnostics\":[]") != std::string::npos);
    CHECK(j.find("\"ok\":true") != std::string::npos);
}

TEST_CASE("directives: @expect and @param take text") {
    auto d = read("// @expect run-ok\n"
                  "// @expect grid g count(wall) >= 3   // a note\n"
                  "// @expect stderr-contains no route\n"
                  "// @param difficulty=5\n"
                  "// @param bias=-2\n"
                  "tag t { a }\n");
    CHECK(d.warnings.all.empty());
    CHECK(d.expects() == std::vector<std::string>{"run-ok", "grid g count(wall) >= 3",
                                                  "stderr-contains no route"});
    CHECK(d.params() == std::vector<std::pair<std::string, int>>{{"difficulty", 5}, {"bias", -2}});
}

TEST_CASE("directives: malformed test directives warn") {
    CHECK(warns(read("// @seed 42\ntag t { a }\n"), "'@seed' takes '=': write '@seed=42'"));
    CHECK(warns(read("// @expect=run_ok\ntag t { a }\n"), "takes text"));
    CHECK(warns(read("// @expect sunny\ntag t { a }\n"), "expects 'error', 'run-ok'"));
    CHECK(warns(read("// @expect grid g count(a) = 3\ntag t { a }\n"), "expects 'grid <grid>"));
    CHECK(warns(read("// @param difficulty 5\ntag t { a }\n"), "expects '<param>=<integer>'"));
    CHECK(warns(read("// @seed=4 @expect run-ok\ntag t { a }\n"), "malformed"));
    CHECK(warns(read("tag t { a }\n// @expect run-ok\n"), "before the first declaration"));
    auto twice = read("// @param p=1\n// @param p=2\ntag t { a }\n");
    CHECK(warns(twice, "duplicate '@param'"));
    CHECK(twice.params() == std::vector<std::pair<std::string, int>>{{"p", 2}});
}

TEST_CASE("directives: test keys in a used module are ignored quietly") {
    resolver r = [](std::string const& path, std::string const&) -> std::optional<module_source> {
        if (path != "lib.lvs") return std::nullopt;
        return module_source{"lib.lvs", "// @expect run-ok\n// @param p=1\ntag t { a }\n"};
    };
    auto d = read_directives("use \"lib.lvs\"\n", "root.lvs", r);
    CHECK(d.warnings.all.empty());
    CHECK(d.expects().empty());
    CHECK(d.params().empty());
}
