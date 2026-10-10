#include "lexer.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace ls;

static std::vector<token> lex_ok(std::string_view src) {
    diagnostics diags;
    auto toks = lex(src, "test", diags);
    REQUIRE(!diags.has_errors());
    return toks;
}

TEST_CASE("lexer: keywords, idents, punctuation") {
    auto t = lex_ok("tag geo { wall }");
    REQUIRE(t.size() == 6);   // tag geo { wall } <end>
    CHECK(t[0].is(token_type::kw_tag));
    CHECK(t[1].is(token_type::ident));
    CHECK(t[1].text == "geo");
    CHECK(t[2].is(token_type::lbrace));
    CHECK(t[3].is(token_type::ident));
    CHECK(t[4].is(token_type::rbrace));
    CHECK(t[5].is(token_type::end));
}

TEST_CASE("lexer: arrow vs equals") {
    auto t = lex_ok("= =>");
    CHECK(t[0].is(token_type::equals));
    CHECK(t[1].is(token_type::arrow));
}

TEST_CASE("lexer: newlines are tokens, comments are not") {
    auto t = lex_ok("a // comment\nb");
    REQUIRE(t.size() == 4);
    CHECK(t[0].text == "a");
    CHECK(t[1].is(token_type::newline));
    CHECK(t[2].text == "b");
}

TEST_CASE("lexer: integers carry their value") {
    auto t = lex_ok("resize(60, 40)");
    CHECK(t[2].is(token_type::integer));
    CHECK(t[2].int_val == 60);
    CHECK(t[4].int_val == 40);
}

TEST_CASE("lexer: positions are 1-based line:col") {
    auto t = lex_ok("a\n  b");
    CHECK(t[0].line == 1);
    CHECK(t[0].col == 1);
    CHECK(t[2].line == 2);
    CHECK(t[2].col == 3);
}

TEST_CASE("lexer: '?name' is one VARIABLE token (0.9, 2.3)") {
    auto t = lex_ok("?a ?all ?x_1(");
    REQUIRE(t.size() == 5);
    CHECK(t[0].is(token_type::variable));
    CHECK(t[0].text == "?a");
    CHECK(t[1].is(token_type::variable));   // keywords are names here too
    CHECK(t[1].text == "?all");
    CHECK(t[2].text == "?x_1");
    CHECK(t[3].is(token_type::lparen));
    CHECK(t[2].col == 9);
    CHECK(t[3].col == 13);
}

TEST_CASE("lexer: '?' without an adjacent name is an error (0.9, 2.4)") {
    for (char const* src : {"? a", "?", "?1", "?("}) {
        diagnostics diags;
        auto t = lex(src, "test", diags);
        INFO(src);
        CHECK(diags.has_errors());
        CHECK(t[0].is(token_type::bad));
    }
}

TEST_CASE("lexer: unexpected character is an error") {
    diagnostics diags;
    auto t = lex("a ? b", "test", diags);
    CHECK(diags.has_errors());
    CHECK(t[1].is(token_type::bad));
}

TEST_CASE("lexer: contextual names lex as identifiers (spec section 2.4)") {
    auto t = lex_ok("max symmetry rotation horizontal vertical none");
    REQUIRE(t.size() == 7);
    for (int i = 0; i < 6; ++i) CHECK(t[i].is(token_type::ident));
    CHECK(t[0].text == "max");
    CHECK(t[5].text == "none");
    // the true keywords stay reserved
    auto k = lex_ok("all once scatter everywhere grow settle weight");
    CHECK(k[0].is(token_type::kw_all));
    CHECK(k[1].is(token_type::kw_once));
    CHECK(k[2].is(token_type::kw_scatter));
    CHECK(k[3].is(token_type::kw_everywhere));
    CHECK(k[4].is(token_type::kw_grow));
    CHECK(k[5].is(token_type::kw_settle));
    CHECK(k[6].is(token_type::kw_weight));
}

TEST_CASE("lexer: the 0.7 count and policy words are plain names (0.8)") {
    auto t = lex_ok("one some policy snapshot incremental stabilize percent max");
    REQUIRE(t.size() == 9);
    for (int i = 0; i < 8; ++i) CHECK(t[i].is(token_type::ident));
}

TEST_CASE("lexer: '%' is a token") {
    auto t = lex_ok("50%");
    REQUIRE(t.size() == 3);
    CHECK(t[0].is(token_type::integer));
    CHECK(t[1].is(token_type::percent));
}
