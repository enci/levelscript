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

TEST_CASE("lexer: unexpected character is an error") {
    diagnostics diags;
    auto t = lex("a ? b", "test", diags);
    CHECK(diags.has_errors());
    CHECK(t[1].is(token_type::bad));
}
