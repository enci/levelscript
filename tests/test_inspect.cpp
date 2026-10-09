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
    std::string j = ls::inspect_json(good_src, "test.ls");
    CHECK(has(j, "\"ok\":true"));
    CHECK(has(j, "\"diagnostics\":[]"));
    CHECK(has(j, "\"name\":\"geo\""));
    CHECK(has(j, "\"values\":[{\"name\":\"wall\",\"loc\":"));
    CHECK(has(j, "\"unions\":[{\"name\":\"blocker\",\"loc\":{\"line\":2,\"col\":24,\"len\":7,"
                 "\"module\":\"test.ls\"}}]"));
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
    std::string j = ls::inspect_json(good_src, "test.ls");
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
    std::string j = ls::inspect_json(src, "test.ls");
    CHECK(has(j, "\"ok\":false"));
    CHECK(has(j, "unknown tag value 'lava'"));
    CHECK(has(j, "\"name\":\"geo\""));                  // symbols survive
    CHECK(has(j, "\"rules\":[{\"name\":\"bad\",\"loc\":"));
    CHECK(has(j, "\"tag\":0,\"value\":1"));             // 'floor' token survives
}

TEST_CASE("inspect: parse-broken file still reports and stays valid JSON-ish") {
    std::string j = ls::inspect_json("rule r { level[.] =>", "test.ls");
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
    std::string j = ls::inspect_json(wsrc, "test.ls");
    CHECK(has(j, "\"ok\":true"));
    CHECK(has(j, "\"severity\":\"warning\""));
    CHECK(has(j, "may never terminate"));
    (void)src;
}

TEST_CASE("inspect: message strings are JSON-escaped") {
    // messages contain single quotes routinely; an unresolved `use` quotes
    // its path in double quotes, which must not break the JSON
    std::string j = ls::inspect_json("use \"nope.ls\"\n", "test.ls");
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
)", "test.ls");
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
