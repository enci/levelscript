#include "inspect.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

// The inspect JSON is the contract between the compiler and the editor
// tooling (lsc --inspect and the WASM module share this one function).
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
program {
    resize(4, 4)
    all fill
}
)";

TEST_CASE("inspect: healthy file emits ok, symbols, and no diagnostics") {
    std::string j = ls::inspect_json(good_src, "test.ls");
    CHECK(has(j, "\"ok\":true"));
    CHECK(has(j, "\"diagnostics\":[]"));
    CHECK(has(j, "\"name\":\"geo\""));
    CHECK(has(j, "\"values\":[\"wall\",\"floor\"]"));
    CHECK(has(j, "\"unions\":[\"blocker\"]"));
    CHECK(has(j, "{\"name\":\"level\",\"type\":\"geo\"}"));
    CHECK(has(j, "{\"name\":\"tiles\",\"type\":\"number\"}"));
    CHECK(has(j, "{\"name\":\"difficulty\",\"derived\":false}"));
    CHECK(has(j, "{\"name\":\"budget\",\"derived\":true}"));
    CHECK(has(j, "\"rules\":[\"fill\"]"));
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
program { all good }
)";
    std::string j = ls::inspect_json(src, "test.ls");
    CHECK(has(j, "\"ok\":false"));
    CHECK(has(j, "unknown tag value 'lava'"));
    CHECK(has(j, "\"name\":\"geo\""));                  // symbols survive
    CHECK(has(j, "\"rules\":[\"bad\",\"good\"]"));
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
program { all(policy=incremental) mark }
)";
    std::string j = ls::inspect_json(wsrc, "test.ls");
    CHECK(has(j, "\"ok\":true"));
    CHECK(has(j, "\"severity\":\"warning\""));
    CHECK(has(j, "may never terminate"));
    (void)src;
}

TEST_CASE("inspect: message strings are JSON-escaped") {
    // messages contain single quotes routinely; ensure double quotes inside
    // identifiers don't break the JSON (lexer rejects them, message quotes them)
    std::string j = ls::inspect_json("tag \"x\" { a }", "test.ls");
    CHECK(has(j, "\"ok\":false"));
    CHECK(has(j, "\\\""));   // escaped quote inside a message
}
