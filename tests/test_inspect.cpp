#include "inspect.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

// The inspect JSON is the contract between the compiler and the editor
// tooling (levelscript --inspect and the WASM module share this one function).
// Substring assertions keep the tests honest without a JSON parser.

static bool has(std::string const& json, std::string const& needle) {
    return json.find(needle) != std::string::npos;
}

static const std::string good_src = R"(
tag geo { wall, floor, blocker = wall | floor }
layers {
    level: grid of geo
    tiles: grid of number
}
params {
    difficulty: number = 3
    budget = difficulty * 2
}
rule fill { level[.] => level[floor] }
sequence main {
    resize(4, 4)
    everywhere fill
}
)";

TEST_CASE("inspect: healthy file emits ok, symbols, and no diagnostics") {
    std::string j = ls::inspect_json(good_src, "test.lvs");
    CHECK(has(j, "\"ok\":true"));
    CHECK(has(j, "\"diagnostics\":[]"));
    CHECK(has(j, "\"name\":\"geo\""));
    CHECK(has(j, "\"values\":[{\"name\":\"wall\",\"loc\":"));
    CHECK(has(j, "\"unions\":[{\"name\":\"blocker\",\"loc\":{\"line\":2,\"col\":24,\"len\":7,"
                 "\"module\":\"test.lvs\"}}]"));
    CHECK(has(j, "{\"name\":\"level\",\"type\":\"geo\",\"loc\":"));
    CHECK(has(j, "{\"name\":\"tiles\",\"type\":\"number\",\"loc\":"));
    CHECK(has(j, "{\"name\":\"difficulty\",\"derived\":false,\"loc\":"));
    CHECK(has(j, "{\"name\":\"budget\",\"derived\":true,\"loc\":"));
    CHECK(has(j, "\"rules\":[{\"name\":\"fill\",\"loc\":"));
    CHECK(has(j, "\"ops\":[\"resize\""));
    CHECK(has(j, "\"builtins\":[\"if\""));
}

TEST_CASE("inspect: token classification of pattern tag values") {
    // rule fill's write cell 'floor' (value id 1 of tagset 0) — line 11:
    // `rule fill { level[.] => level[floor] }` — col of 'floor' is 31.
    std::string j = ls::inspect_json(good_src, "test.lvs");
    CHECK(has(j, "{\"line\":11,\"col\":31,\"len\":5,\"tag\":0,\"value\":1}"));
}

TEST_CASE("inspect: broken file still yields symbols (best effort)") {
    // 'lava' is unknown -> a diagnostic; the tag/layer/rule tables and the
    // OTHER pattern tokens must still be present for last-good-free editing.
    std::string src = R"(
tag geo { wall, floor }
layers { level: grid of geo }
rule bad  { level[lava] => level[wall] }
rule good { level[.] => level[floor] }
sequence main { everywhere good }
)";
    std::string j = ls::inspect_json(src, "test.lvs");
    CHECK(has(j, "\"ok\":false"));
    CHECK(has(j, "unknown tag value 'lava'"));
    CHECK(has(j, "\"name\":\"geo\""));                  // symbols survive
    CHECK(has(j, "\"rules\":[{\"name\":\"bad\",\"loc\":"));
    CHECK(has(j, "\"tag\":0,\"value\":1"));             // 'floor' token survives
}

TEST_CASE("inspect: parse-broken file still reports and stays valid JSON-ish") {
    std::string j = ls::inspect_json("rule r { level[.] =>", "test.lvs");
    CHECK(has(j, "\"ok\":false"));
    CHECK(has(j, "\"severity\":\"error\""));
    CHECK(j.back() == '}');
}

TEST_CASE("inspect: warnings carry their severity") {
    std::string src = R"(
tag geo { floor }
layers { level: grid of geo }
layers2: grid of geo
)";
    // simpler: use the reductivity warning
    std::string wsrc = R"(
tag geo { floor }
layers { level: grid of geo  tiles: grid of number }
rule mark { level[floor] => tiles[1] }
sequence main { grow mark }
)";
    std::string j = ls::inspect_json(wsrc, "test.lvs");
    CHECK(has(j, "\"ok\":true"));
    CHECK(has(j, "\"severity\":\"warning\""));
    CHECK(has(j, "may never terminate"));
    (void)src;
}

TEST_CASE("inspect: message strings are JSON-escaped") {
    // messages contain single quotes routinely; an unresolved `use` quotes
    // its path in double quotes, which must not break the JSON
    std::string j = ls::inspect_json("use \"nope.lvs\"\n", "test.lvs");
    CHECK(has(j, "\"ok\":false"));
    CHECK(has(j, "\\\""));   // escaped quote inside a message
}

TEST_CASE("inspect: unions, number literals, and wildcards are colorable") {
    std::string j = ls::inspect_json(R"(tag t { F, W, D = F | W }
layers {
    g: grid of t
    n: grid of number
}
rule r { g[* D] n[* 12] => { all g[F D]  n[. 3] } }
sequence main { }
)", "test.lvs");
    CHECK(has(j, "\"diagnostics\":[]"));
    // union D takes the slot after its tag's values (F=0, W=1 -> D=2)
    CHECK(has(j, "{\"line\":6,\"col\":14,\"len\":1,\"tag\":0,\"value\":2}"));
    CHECK(has(j, "{\"line\":6,\"col\":38,\"len\":1,\"tag\":0,\"value\":2}"));
    // '*' in a tag grid and in a number grid
    CHECK(has(j, "{\"line\":6,\"col\":12,\"len\":1,\"tag\":-1,\"value\":-1}"));
    CHECK(has(j, "{\"line\":6,\"col\":19,\"len\":1,\"tag\":-1,\"value\":-1}"));
    // number-grid literals carry their value; '.' is the empty token
    CHECK(has(j, "{\"line\":6,\"col\":21,\"len\":2,\"tag\":-3,\"value\":12}"));
    CHECK(has(j, "{\"line\":6,\"col\":44,\"len\":1,\"tag\":-2,\"value\":-2}"));
    CHECK(has(j, "{\"line\":6,\"col\":46,\"len\":1,\"tag\":-3,\"value\":3}"));
}

TEST_CASE("inspect: inline rules are colored and their grids referenced") {
    std::string j = ls::inspect_json(R"(tag t { F, W }
layers { g: grid of t }
sequence main { everywhere { g[F] => g[W] } }
)", "test.lvs");
    CHECK(has(j, "\"diagnostics\":[]"));
    CHECK(has(j, "{\"line\":3,\"col\":32,\"len\":1,\"tag\":0,\"value\":0}"));   // F
    CHECK(has(j, "{\"line\":3,\"col\":30,\"len\":1,\"kind\":\"layer\",\"target\":\"g\"}"));
    CHECK(!has(j, "\"kind\":\"rule\""));   // no name reference for an inline target
}

TEST_CASE("inspect: an outline of the declarations, and every node's range") {
    std::string j = ls::inspect_json(good_src, "test.lvs");
    auto range = [](int l, int c, int el, int ec) {
        return "{\"line\":" + std::to_string(l) + ",\"col\":" + std::to_string(c) +
               ",\"end_line\":" + std::to_string(el) + ",\"end_col\":" + std::to_string(ec) + "}";
    };
    // the whole declaration, then its name to select
    CHECK(has(j, "{\"kind\":\"rule\",\"name\":\"fill\",\"range\":" + range(11, 1, 11, 39) +
                 ",\"select\":" + range(11, 6, 11, 10) + "}"));
    CHECK(has(j, "{\"kind\":\"sequence\",\"name\":\"main\",\"range\":" + range(12, 1, 15, 2)));
    // blocks select their keyword; a param spans its name through its value
    CHECK(has(j, "{\"kind\":\"layers\",\"name\":\"layers\",\"range\":" + range(3, 1, 6, 2) +
                 ",\"select\":" + range(3, 1, 3, 7) + ",\"children\":[{\"kind\":\"grid\""));
    CHECK(has(j, "{\"kind\":\"params\",\"name\":\"params\",\"range\":" + range(7, 1, 10, 2)));
    CHECK(has(j, "{\"kind\":\"param\",\"name\":\"budget\",\"range\":" + range(9, 5, 9, 28)));
    CHECK(has(j, "{\"kind\":\"union\",\"name\":\"blocker\""));
    // node ranges: the sub-rule, a statement, and the multi-line blocks
    CHECK(has(j, range(11, 13, 11, 37)));    // level[.] => level[floor]
    CHECK(has(j, range(13, 5, 13, 17)));     // resize(4, 4)
    CHECK(has(j, "\"spans\":[" + range(3, 1, 6, 2) + "," + range(7, 1, 10, 2)));
}

TEST_CASE("inspect: diagnostics carry the range of what is wrong") {
    std::string j = ls::inspect_json(R"(tag geo { wall }
layers { level: grid of geo }
rule r { level[(1 + wall)] => lvl[wall] }
sequence main { everywhere nope }
)", "test.lvs");
    auto at = [](int l, int c, int el, int ec) {
        return "{\"line\":" + std::to_string(l) + ",\"col\":" + std::to_string(c) +
               ",\"end_line\":" + std::to_string(el) + ",\"end_col\":" + std::to_string(ec) + ",";
    };
    CHECK(has(j, at(3, 17, 3, 25)));   // the operation: 1 + wall
    CHECK(has(j, at(3, 16, 3, 26)));   // the cell it makes the wrong type: (1 + wall)
    CHECK(has(j, at(3, 31, 3, 34)));   // the undeclared grid's name: lvl
    CHECK(has(j, at(4, 28, 4, 32)));   // the undeclared rule's name: nope
    // a parse error underlines the offending token
    std::string p = ls::inspect_json("rule r { g[.] => g[a] }\nsequence main { frob }\n", "test.lvs");
    CHECK(has(p, at(2, 17, 2, 21)));   // frob
}
