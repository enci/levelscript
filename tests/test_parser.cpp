#include "test_support.hpp"
#include "ast_dump.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ls;

static ast_file parse_ok(std::string const& src) {
    diagnostics diags;
    auto ast = parse(src, "test", diags);
    INFO(diags.format_all());
    REQUIRE(ast.has_value());
    REQUIRE(!diags.has_errors());
    return std::move(*ast);
}

static bool parse_fails(std::string const& src) {
    diagnostics diags;
    parse(src, "test", diags);
    return diags.has_errors();
}

TEST_CASE("parser: missing arrow is an error") {
    CHECK(parse_fails("rule r { g[.] g[floor] }"));
}

TEST_CASE("parser: misplaced and malformed use declarations") {
    CHECK(parse_fails("tag t { a }\nuse \"schema.lvs\"\n"));   // after a declaration
    CHECK(parse_fails("use schema\n"));                          // a path is a string
    CHECK(parse_fails("use \"schema.lvs\n"));                     // unterminated
}

// ── step 2: match blocks, write trees, attributes, body combinators ──────────

TEST_CASE("parser: weight inside { all } is an error") {
    // check 5
    CHECK(parse_fails(R"(
rule r {
    g[.]
    =>
    { all
      (weight=2) g[a]
      h[b]
    }
}
)"));
}

TEST_CASE("parser: the match side takes no combinator or commas") {
    CHECK(parse_fails("rule r { { any g[a] h[b] } => g[c] }"));
    CHECK(parse_fails("rule r { g[a], h[b] => g[c] }"));
    CHECK(parse_fails("rule r { { g[a], h[b] } => g[c] }"));
    CHECK(parse_fails("rule r { { all g[a] h[b] } => g[c] }"));
}

TEST_CASE("parser: match-side braces hold patterns and do not nest") {
    CHECK(parse_fails("rule r { { } => g[c] }"));
    CHECK(parse_fails("rule r { { { g[a] } } => g[c] }"));
}

TEST_CASE("parser: list commas are optional, newlines are whitespace") {
    // comma-only, on one line
    auto a = parse_ok("tag t { a, b, c }\nlayers { g: grid of t, h: grid of number }\n"
                      "params { n: number = 2, m = n * 2 }\n"
                      "rule r { all g[a] => g[b], g[b] => g[c] }");
    CHECK(a.tags[0].values.size() == 3);
    CHECK(a.layers.layers.size() == 2);
    CHECK(a.params.size() == 2);
    CHECK(a.rules[0].pairs.size() == 2);
    // no separators at all, items sharing a line
    auto b = parse_ok("tag t { a b c }\nlayers { g: grid of t h: grid of number }\n"
                      "params { n: number = 2 m = n * 2 }\n"
                      "rule r { all g[a] => g[b] g[b] => g[c] }");
    CHECK(b.tags[0].values.size() == 3);
    CHECK(b.layers.layers.size() == 2);
    CHECK(b.params.size() == 2);
    CHECK(b.rules[0].pairs.size() == 2);
    // newline-only, with trailing commas and line breaks inside items
    auto c = parse_ok("tag t {\n a\n b,\n}\nparams {\n n: number =\n   2\n}\n"
                      "rule r {\n g[a]\n =>\n { any\n g[a],\n g[b],\n }\n}");
    CHECK(c.tags[0].values.size() == 2);
    REQUIRE(c.params.size() == 1);
    CHECK(c.rules[0].pairs[0].rhs.items.size() == 2);
}

TEST_CASE("parser: a bad statement is reported once per line") {
    diagnostics diags;
    parse("sequence main {\n  frob frob frob\n  resize(4, 4)\n}", "test", diags);
    CHECK(diags.all.size() == 1);
}

// ── step 3: modes, counts, ordered ────────────────────────────────────────────

TEST_CASE("parser: '%' binds to an atom") {
    CHECK(parse_fails("sequence main { scatter(n * 2%) a }"));
    CHECK(parse_fails("sequence main { scatter(-5%) a }"));
    CHECK(parse_fails("sequence main { scatter(f(1)%) a }"));
}

TEST_CASE("parser: count forms each mode rejects") {
    CHECK(parse_fails("sequence main { scatter r }"));       // scatter needs a count
    CHECK(parse_fails("sequence main { once(2) r }"));       // once takes none
    CHECK(parse_fails("sequence main { everywhere(2) r }"));
    CHECK(parse_fails("sequence main { grow(50%) r }"));     // only scatter takes %
    CHECK(parse_fails("sequence main { settle(10%) r }"));
}

TEST_CASE("parser: the 0.7 count and policy forms are not statements") {
    CHECK(parse_fails("sequence main { one r }"));
    CHECK(parse_fails("sequence main { some(max=5) r }"));
    CHECK(parse_fails("sequence main { all r }"));
    CHECK(parse_fails("sequence main { all(policy=stabilize) r }"));
}

TEST_CASE("parser: an inline rule is always braced") {
    CHECK(parse_fails("sequence main { everywhere g[.] => g[a] }"));
    CHECK(parse_fails("sequence main { once (symmetry=all) fill }"));
    CHECK(parse_fails("sequence main { everywhere { } }"));
}

TEST_CASE("parser: ordered on a match or write side is an error") {
    // check 29
    CHECK(parse_fails("rule r { { ordered g[a] h[b] } => g[c] }"));
    CHECK(parse_fails("rule r { g[.] => { ordered g[a] g[b] } }"));
}

// ── step 4: expressions, mask cells, where, params, when ─────────────────────

TEST_CASE("parser: a spaced union is not one bare cell") {
    CHECK(parse_fails("rule r { g[wall | door] => g[floor] }"));
}

TEST_CASE("parser: a variable is not a bare mask atom") {
    CHECK(parse_fails("rule r { g[?a|wall] => g[*] }"));
    CHECK(parse_fails("rule r { g[wall|?a] => g[*] }"));
    CHECK(parse_fails("rule r { g[!?a] => g[*] }"));
    CHECK(parse_fails("rule r { g[? a] => g[*] }"));
}

TEST_CASE("parser: '!' and '|' touch the atoms of a bare mask") {
    CHECK(parse_fails("rule r { g[! .] => g[*] }"));
    CHECK(parse_fails("rule r { g[. | door] => g[*] }"));
}

TEST_CASE("parser: empty pattern rows around commas are errors") {
    // check 2
    CHECK(parse_fails("rule r { g[a,, b] => g[a, b] }"));
    CHECK(parse_fails("rule r { g[, a] => g[a] }"));
    CHECK(parse_fails("rule r { g[a, b,] => g[a, b] }"));
    CHECK(parse_fails("rule r { g[a,\n] => g[a] }"));
}

TEST_CASE("parser: one params block per module") {
    // check 17
    CHECK(parse_fails("params { a: number = 1 }\nparams { b: number = 2 }"));
}

TEST_CASE("parser: an unclosed pattern reports the missing bracket once") {
    diagnostics diags;
    parse("rule r {\n    g[.\n    =>\n    g[a]\n}\n", "test", diags);
    REQUIRE(diags.all.size() == 1);
    CHECK(diags.all[0].message.find("expected ']'") != std::string::npos);
}

TEST_CASE("parser: an unknown attribute is parsed whole — one diagnostic") {
    // check 7
    for (char const* attrs : {"colour=red", "colour=3", "colour=all", "colour={90, 180}"}) {
        diagnostics diags;
        parse(std::string("rule r(") + attrs + ") { g[.] => g[a] }\n", "test", diags);
        INFO(attrs);
        REQUIRE(diags.all.size() == 1);
        CHECK(diags.all[0].message.find("unknown rule attribute 'colour'") != std::string::npos);
    }
}

// A span as "line:col-end_line:end_col".
static std::string span(source_loc const& l) {
    return std::to_string(l.line) + ":" + std::to_string(l.col) + "-" +
           std::to_string(l.end_line) + ":" + std::to_string(l.end_col);
}

TEST_CASE("parser: nodes span what they were parsed from") {
    auto ast = parse_ok(
        "rule fill {\n"                                // line 1
        "    level[. !wall|door]\n"                     // line 2
        "    =>\n"
        "    level[(1 + x * 2) *]\n"                    // line 4
        "}\n"
        "sequence main {\n"                             // line 6
        "    scatter(n + 1) fill when (n > 2)\n"        // line 7
        "}\n");
    auto const& r = ast.rules[0];
    CHECK(span(r.loc) == "1:1-5:2");
    CHECK(span(r.name.loc) == "1:6-1:10");
    auto const& lhs = r.pairs[0].lhs[0];
    CHECK(span(lhs.loc) == "2:5-2:24");
    CHECK(span(lhs.grid.loc) == "2:5-2:10");
    CHECK(span(lhs.cells[0][1]->loc) == "2:13-2:23");            // the whole union
    CHECK(span(lhs.cells[0][1]->args[0]->loc) == "2:13-2:18");   // '!' included
    CHECK(span(lhs.cells[0][1]->args[0]->args[0]->loc) == "2:14-2:18");
    auto const& cell = *r.pairs[0].rhs.pat.cells[0][0];
    CHECK(span(cell.loc) == "4:11-4:22");                        // parentheses included
    CHECK(span(cell.args[0]->loc) == "4:12-4:21");               // the binary spans its operands
    CHECK(span(cell.args[0]->args[1]->loc) == "4:16-4:21");

    auto const& s = ast.sequences[0].stmts[0];
    CHECK(span(ast.sequences[0].loc) == "6:1-8:2");
    CHECK(span(s.loc) == "7:5-7:37");                         // guard included
    CHECK(span(s.count->loc) == "7:13-7:18");
    CHECK(span(s.rule_name.loc) == "7:20-7:24");
    CHECK(span(s.guard->loc) == "7:31-7:36");
}

// ── golden ASTs ──────────────────────────────────────────────────────────────

static std::string read_file(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Each tests/ast/<name>.lvs, parsed alone, must dump - without spans, then
// any diagnostics - exactly as tests/ast/<name>.ast. A parser change that
// alters a tree shows up as a reviewable diff of that file; to accept one, run
// the tests with LS_UPDATE_GOLDEN=1 set, which rewrites the .ast files.
TEST_CASE("parser: golden ASTs") {
    namespace fs = std::filesystem;
    std::vector<fs::path> sources;
    for (auto const& e : fs::directory_iterator(LS_AST_GOLDEN_DIR))
        if (e.path().extension() == ".lvs") sources.push_back(e.path());
    std::sort(sources.begin(), sources.end());
    REQUIRE(!sources.empty());
    bool update = std::getenv("LS_UPDATE_GOLDEN") != nullptr;
    for (auto const& src : sources) {
        diagnostics diags;
        std::string actual = dump_ast(read_file(src), src.filename().string(), false, diags);
        for (auto const& d : diags.all) actual += "; " + d.format() + "\n";
        fs::path golden = fs::path(src).replace_extension(".ast");
        if (update) std::ofstream(golden, std::ios::binary) << actual;
        INFO(golden.string());
        REQUIRE(fs::exists(golden));
        CHECK(read_file(golden) == actual);
    }
}
