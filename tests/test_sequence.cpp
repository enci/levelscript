// Named sequences (spec section 6.10) — the conformance suite ls-tests-sequence.md,
// Q01–Q50, one TEST_CASE per Q. Harness conventions follow the suite:
// iterations are read off stmt_stack(), "equivalent to program B" means
// byte-identical levels for seeds 1–20 with default params.
#include "test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using ts::compile_result;
using ts::make;

namespace {

// Every step of a run: the stack, stmt_index, and boundary flag after it.
struct trace {
    std::vector<std::vector<ls::stmt_frame>> stacks;
    std::vector<int>                         index;
    std::vector<bool>                        boundary;
    ls::level                                final;
};

trace run_trace(ls::generator const& gen, uint64_t seed,
                ls::step_mode mode = ls::step_mode::statement) {
    trace t;
    auto r = gen.run(gen.sequence("main"), seed, mode, ls::observe::on);
    while (r.step()) {
        t.stacks.push_back(r.stmt_stack());
        t.index.push_back(r.statement_index());
        t.boundary.push_back(r.at_statement_boundary());
    }
    t.final = r.snapshot();
    return t;
}

// Iterations of the sequence application at `path` (frame indices from the
// top): 1 + the highest `iteration` in the frame directly below it.
int iterations(trace const& t, std::vector<int> const& path) {
    int best = -1;
    for (auto const& st : t.stacks) {
        if (st.size() <= path.size()) continue;
        bool on_path = true;
        for (size_t k = 0; k < path.size(); ++k)
            if (st[k].index != path[k]) { on_path = false; break; }
        if (on_path && st[path.size()].iteration > best) best = st[path.size()].iteration;
    }
    return best + 1;
}

bool levels_equal(ls::level const& a, ls::level const& b) {
    if (a.width() != b.width() || a.height() != b.height() ||
        a.layer_count() != b.layer_count())
        return false;
    for (int l = 0; l < a.layer_count(); ++l) {
        ls::grid ga = a.layer(l), gb = b.layer(l);
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x)
                if (ga.is_empty(x, y) != gb.is_empty(x, y) || ga.at(x, y) != gb.at(x, y))
                    return false;
    }
    return true;
}

void check_equivalent(std::string const& a_src, std::string const& b_src) {
    auto a = make(a_src), b = make(b_src);
    INFO(a.error() << b.error());
    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        INFO("seed " << seed);
        CHECK(levels_equal(a.generate(a.sequence("main"), seed), b.generate(b.sequence("main"), seed)));
    }
}

bool has_warning(compile_result const& r, std::string const& needle = "") {
    for (auto const& d : r.diags.all)
        if (!d.is_error && d.message.find(needle) != std::string::npos) return true;
    return false;
}

int cell_sum(ls::level const& lv, char const* layer) {
    int s = 0;
    ls::grid g = lv[layer];
    for (int y = 0; y < lv.height(); ++y)
        for (int x = 0; x < lv.width(); ++x)
            if (!g.is_empty(x, y)) s += g.at(x, y);
    return s;
}

// The saturating counter of the suite's section 3: `inc` raises the 1x1 cell by 1
// while it is below `cap`.
std::string counter(int cap) {
    return R"(
layers { tiles: grid of number }
rule inc {
    tiles[*]
    where[ (tiles < )" + std::to_string(cap) + R"() ]
    =>
    tiles[ (tiles + 1) ]
}
)";
}

}  // namespace

// ── 1. Acceptance ─────────────────────────────────────────────────────────────

TEST_CASE("sequence Q01: minimal sequence") {
    auto gen = make(R"(
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence s {
    all fill
}
sequence main {
    resize(4, 4)
    one s
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    auto lv = gen.generate(gen.sequence("main"), 1);
    int a = gen.tag("t.a");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(lv["g"].at(x, y) == a);
}

TEST_CASE("sequence Q02: forward references") {
    compile_result r(R"(
tag t { a }
layers { g: grid of t }
sequence main {
    resize(2, 2)
    one s
}
sequence s {
    all fill
}
rule fill { g[.] => g[a] }
)");
    INFO(r.diags.format_all());
    CHECK(r.ok);
}

TEST_CASE("sequence Q03: an unapplied sequence is legal, no warning") {
    compile_result r(R"(
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence unused { all fill }
sequence main { resize(2, 2) }
)");
    INFO(r.diags.format_all());
    CHECK(r.ok);
    CHECK(r.diags.all.empty());
}

TEST_CASE("sequence Q04: a body holds every statement kind") {
    compile_result r(R"(
tag t { a, b }
params { big: number = 0 }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule flip { g[a] => g[b] }
sequence inner { all fill }
sequence outer {
    one inner
    some(max=3, policy=incremental) flip
    all(policy=stabilize) fill
    some(percent=50) flip
    pad(1)                  when (big == 1)
    resize(8, 8)
}
sequence main {
    resize(4, 4)
    one outer
}
)");
    INFO(r.diags.format_all());
    CHECK(r.ok);
}

TEST_CASE("sequence Q05: statements on one line") {
    check_equivalent(R"(
tag t { a, b }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule flip { g[a] => g[b] }
sequence s { all fill all flip }
sequence main { resize(2, 2) one s }
)", R"(
tag t { a, b }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule flip { g[a] => g[b] }
sequence s {
    all fill
    all flip
}
sequence main {
    resize(2, 2)
    one s
}
)");
}

// ── 2. Stability ──────────────────────────────────────────────────────────────

TEST_CASE("sequence Q06: change and change back is stable") {
    auto gen = make(R"(
tag t { a, b }
layers { g: grid of t }
rule fill   { g[.] => g[a] }
rule a_to_b { g[a] => g[b] }
rule b_to_a { g[b] => g[a] }
sequence flip {
    all a_to_b
    all b_to_a
}
sequence main {
    resize(4, 4)
    all fill
    all flip
}
)");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {2}) == 1);
    int a = gen.tag("t.a");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(t.final["g"].at(x, y) == a);
}

TEST_CASE("sequence Q07: writing the same value is stable") {
    auto gen = make(R"(
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule same { g[a] => g[a] }
sequence s { all same }
sequence main {
    resize(3, 3)
    all fill
    all s
}
)");
    REQUIRE(static_cast<bool>(gen));
    CHECK(iterations(run_trace(gen, 1), {2}) == 1);
}

TEST_CASE("sequence Q08: a dimension change is a change (trim)") {
    auto gen = make(R"(
tag t { a }
layers { g: grid of t }
rule seed { g[.] => g[a] }
sequence crop { trim() }
sequence main {
    resize(5, 5)
    one seed
    all crop
}
)");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {2}) == 2);
    CHECK(t.final.width() == 1);
    CHECK(t.final.height() == 1);
    CHECK(t.final["g"].at(0, 0) == gen.tag("t.a"));
}

TEST_CASE("sequence Q09: resize inside a fixpoint stabilises") {
    compile_result r(R"(
layers { }
sequence r { resize(6, 4) }
sequence main {
    resize(5, 5)
    all r
}
)");
    CHECK(r.diags.all.empty());   // no warning
    auto gen = make(R"(
layers { }
sequence r { resize(6, 4) }
sequence main {
    resize(5, 5)
    all r
}
)");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {1}) == 2);
    CHECK(t.final.width() == 6);
    CHECK(t.final.height() == 4);
}

TEST_CASE("sequence Q10: an empty body is stable and contributes no steps") {
    auto gen = make(R"(
layers { }
sequence nothing { }
sequence main {
    resize(2, 2)
    all nothing
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    REQUIRE(t.stacks.size() == 1);   // only the resize
    CHECK(t.index[0] == 0);
    CHECK(t.final.width() == 2);
}

TEST_CASE("sequence Q11: a stable iteration ends the statement despite pending randomness") {
    auto gen = make(R"(
layers { tiles: grid of number }
rule zero { tiles[.] => tiles[0] }
rule poke { tiles[*] => tiles[ (tiles + 100) ] }
sequence maybe {
    one poke  when (random(0, 1) == 0)
}
sequence main {
    resize(4, 4)
    all zero
    some(max=50) maybe
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        INFO("seed " << seed);
        auto t = run_trace(gen, seed);
        int it = iterations(t, {2});
        int sum = cell_sum(t.final, "tiles");
        CHECK((it == 50 || sum == (it - 1) * 100));
    }
}

// ── 3. Counts ─────────────────────────────────────────────────────────────────

TEST_CASE("sequence Q12: one S runs exactly one iteration") {
    auto gen = make(counter(3) + "sequence tick { all inc }\nsequence main {\n resize(1, 1)\n one tick\n}\n");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {1}) == 1);
    CHECK(t.final["tiles"].at(0, 0) == 1);
}

TEST_CASE("sequence Q13: some(max=N) stops early after a stable iteration") {
    auto gen = make(counter(3) + "sequence tick { all inc }\nsequence main {\n resize(1, 1)\n some(max=10) tick\n}\n");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {1}) == 4);
    CHECK(t.final["tiles"].at(0, 0) == 3);
}

TEST_CASE("sequence Q14: some(max=N) runs all N iterations when each changes something") {
    auto gen = make(counter(100) + "sequence tick { all inc }\nsequence main {\n resize(1, 1)\n some(max=5) tick\n}\n");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {1}) == 5);
    CHECK(t.final["tiles"].at(0, 0) == 5);
}

TEST_CASE("sequence Q15: all S is a fixpoint") {
    auto gen = make(counter(3) + "sequence tick { all inc }\nsequence main {\n resize(1, 1)\n all tick\n}\n");
    REQUIRE(static_cast<bool>(gen));
    auto t = run_trace(gen, 1);
    CHECK(iterations(t, {1}) == 4);
    CHECK(t.final["tiles"].at(0, 0) == 3);
}

TEST_CASE("sequence Q16: bounded growth through upscale") {
    std::string src = R"(
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence widen { upscale(2, 1) }
sequence main {
    resize(1, 1)
    all fill
    some(max=3) widen
}
)";
    compile_result r(src);
    CHECK(r.ok);
    CHECK(!has_warning(r));
    auto t = run_trace(make(src), 1);
    CHECK(iterations(t, {2}) == 3);
    CHECK(t.final.width() == 8);
    CHECK(t.final.height() == 1);
}

static std::string const q17 = counter(100) + R"(
sequence inner { all inc }
sequence outer { some(max=2) inner }
sequence main {
    resize(1, 1)
    some(max=3) outer
}
)";

TEST_CASE("sequence Q17: nested sequences multiply") {
    auto t = run_trace(make(q17), 1);
    CHECK(t.final["tiles"].at(0, 0) == 6);
    CHECK(iterations(t, {1}) == 3);
    for (int k = 0; k < 3; ++k) {
        int inner = -1;   // inner iterations within outer iteration k
        for (auto const& st : t.stacks)
            if (st.size() == 3 && st[1].iteration == k && st[2].iteration > inner)
                inner = st[2].iteration;
        CHECK(inner + 1 == 2);
    }
}

// ── 4. Draw order and determinism ─────────────────────────────────────────────

static std::string const noise_poke = R"(
layers { tiles: grid of number }
rule noise { tiles[*] => tiles[ (random(0, 9)) ] }
rule poke  { tiles[*] => tiles[ (tiles + 100) ] }
)";

TEST_CASE("sequence Q18: a sequence equals its unrolled statements (snapshot body)") {
    check_equivalent(noise_poke + R"(
sequence s {
    all noise
    one poke
}
sequence main {
    resize(8, 8)
    some(max=3) s
    all noise
}
)", noise_poke + R"(
sequence main {
    resize(8, 8)
    all noise
    one poke
    all noise
    one poke
    all noise
    one poke
    all noise
}
)");
}

TEST_CASE("sequence Q19: a sequence equals its unrolled statements (mixed policies)") {
    check_equivalent(noise_poke + R"(
sequence s {
    some(max=5, policy=incremental) poke
    some(max=2, policy=stabilize) noise
}
sequence main {
    resize(6, 6)
    some(max=2) s
    all noise
}
)", noise_poke + R"(
sequence main {
    resize(6, 6)
    some(max=5, policy=incremental) poke
    some(max=2, policy=stabilize) noise
    some(max=5, policy=incremental) poke
    some(max=2, policy=stabilize) noise
    all noise
}
)");
}

TEST_CASE("sequence Q20: a body guard is evaluated every iteration") {
    check_equivalent(noise_poke + R"(
sequence s {
    one poke
    one poke  when (random(0, 1) == 0)
}
sequence main {
    resize(4, 4)
    some(max=4) s
    all noise
}
)", noise_poke + R"(
sequence main {
    resize(4, 4)
    one poke
    one poke  when (random(0, 1) == 0)
    one poke
    one poke  when (random(0, 1) == 0)
    one poke
    one poke  when (random(0, 1) == 0)
    one poke
    one poke  when (random(0, 1) == 0)
    all noise
}
)");
}

TEST_CASE("sequence Q21: a false application guard consumes no body draws") {
    check_equivalent(R"(
layers { tiles: grid of number }
params { enabled: number = 0 }
rule noise { tiles[*] => tiles[ (random(0, 9)) ] }
sequence s { all noise }
sequence main {
    resize(4, 4)
    some(max=3) s  when (enabled == 1)
    all noise
}
)", R"(
layers { tiles: grid of number }
params { enabled: number = 0 }
rule noise { tiles[*] => tiles[ (random(0, 9)) ] }
sequence main {
    resize(4, 4)
    all noise
}
)");
}

TEST_CASE("sequence Q22: an application guard is evaluated once") {
    check_equivalent(noise_poke + R"(
sequence s { one poke }
sequence main {
    resize(4, 4)
    some(max=3) s  when (random(0, 0) == 0)
    all noise
}
)", noise_poke + R"(
sequence main {
    resize(4, 4)
    one poke  when (random(0, 0) == 0)
    one poke
    one poke
    all noise
}
)");
}

// ── 5. Compile errors ─────────────────────────────────────────────────────────

static std::string const fill_decls = R"(
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
)";

TEST_CASE("sequence Q23-Q26: a sequence application takes a count only (check 37)") {
    for (char const* stmt : {"all(policy=incremental) s", "one(policy=snapshot) s",
                             "some(max=3, policy=stabilize) s"}) {
        INFO(stmt);
        CHECK(compile_result(fill_decls + "sequence s { all fill }\nsequence main { resize(2, 2)  " +
                             stmt + " }\n")
              .has_error("'policy=' is not valid on sequence 's'"));
    }
    CHECK(compile_result(fill_decls + "sequence s { all fill }\n"
                         "sequence main { resize(2, 2)  some(percent=50) s }\n")
          .has_error("'some(percent=...)' is not valid on sequence 's'"));
}

TEST_CASE("sequence Q27: a direct cycle is rejected even when never applied (check 38)") {
    CHECK(compile_result("sequence s { one s }\nlayers { }\nsequence main { resize(1, 1) }\n")
          .has_error("sequence 's' applies itself"));
}

TEST_CASE("sequence Q28: an indirect cycle (check 38)") {
    CHECK(compile_result("sequence a { one b }\nsequence b { some(max=2) a }\n"
                         "layers { }\nsequence main { resize(1, 1)  one a }\n")
          .has_error("sequence cycle: 'a' -> 'b' -> 'a'"));
}

TEST_CASE("sequence Q29-Q30: rules and sequences share one namespace (check 39)") {
    CHECK(compile_result(fill_decls + "sequence fill { resize(1, 1) }\nsequence main { resize(1, 1) }\n")
          .has_error("already declared as a rule on line 4"));
    CHECK(compile_result(fill_decls + "sequence s { all fill }\nsequence s { one fill }\n"
                         "sequence main { resize(1, 1) }\n")
          .has_error("duplicate sequence 's'"));
}

TEST_CASE("sequence Q31: unknown name, with a suggestion across both kinds (check 8)") {
    CHECK(compile_result(fill_decls + "sequence smooth { all fill }\n"
                         "sequence main { resize(2, 2)  all smoth }\n")
          .has_error("undeclared rule or sequence 'smoth'; did you mean sequence 'smooth'?"));
}

TEST_CASE("sequence Q32: 'sequence' is a keyword") {
    CHECK(compile_result("tag t { a }\nlayers { sequence: grid of t }\nsequence main { resize(1, 1) }\n")
          .has_error("'sequence' - a reserved keyword cannot be used as a name"));
}

TEST_CASE("sequence Q33: no inline rules in a body") {
    compile_result r("tag t { a }\nlayers { g: grid of t }\nsequence s {\n    g[.] => g[a]\n}\n"
                     "sequence main { resize(1, 1)  one s }\n");
    CHECK(r.has_error("expected a statement in sequence 's'; rules are declared with 'rule'"));
    CHECK(r.diags.all.size() == 1);   // one diagnostic for the bad line
}

TEST_CASE("sequence Q34: sequences take no attributes") {
    CHECK(compile_result(fill_decls + "sequence s(rotation=all) { all fill }\n"
                         "sequence main { resize(1, 1)  one s }\n")
          .has_error("expected '{' after sequence name 's'; sequences take no attributes"));
}

TEST_CASE("sequence Q35: rule-level checks still apply inside a body (check 28)") {
    CHECK(compile_result(fill_decls + "sequence s { one(policy=stabilize) fill }\n"
                         "sequence main { resize(1, 1)  one s }\n")
          .has_error("contradictory (a single application cannot reach a sweep fixpoint) (sequence 's')"));
}

// ── 6. The dimension warning (compile-only) ───────────────────────────────────

TEST_CASE("sequence Q36-Q38: all over a dimension-changing sequence warns") {
    compile_result q36("layers { }\nsequence grow { upscale(2, 2) }\nsequence main { resize(1, 1)  all grow }\n");
    CHECK(q36.ok);
    CHECK(has_warning(q36, "'all' over sequence 'grow' may never terminate: 'upscale' changes "
                           "the grid dimensions on every iteration, so no iteration can be stable"));

    compile_result q37("layers { }\nsequence frame { pad(1) }\nsequence main { resize(1, 1)  all frame }\n");
    CHECK(has_warning(q37, "'all' over sequence 'frame' may never terminate: 'pad'"));

    compile_result q38("layers { }\nsequence inner { upscale(2, 1) }\nsequence outer { one inner }\n"
                       "sequence main { resize(1, 1)  all outer }\n");
    CHECK(has_warning(q38, "'all' over sequence 'outer' may never terminate: 'upscale'"));
    CHECK(!has_warning(q38, "sequence 'inner'"));
}

TEST_CASE("sequence Q39: identity factors and a zero margin do not warn") {
    std::string src = "layers { }\nsequence s {\n    upscale(1, 1)\n    pad(0)\n}\n"
                      "sequence main { resize(2, 2)  all s }\n";
    compile_result r(src);
    CHECK(r.ok);
    CHECK(!has_warning(r));
    CHECK(iterations(run_trace(make(src), 1), {1}) == 1);
}

TEST_CASE("sequence Q40-Q41: a guard suppresses the warning") {
    compile_result q40("layers { }\nparams { big: number = 0 }\n"
                       "sequence s { upscale(2, 2)  when (big == 1) }\n"
                       "sequence main { resize(1, 1)  all s }\n");
    CHECK(q40.ok);
    CHECK(!has_warning(q40));
    compile_result q41("layers { }\nparams { big: number = 0 }\n"
                       "sequence inner { upscale(2, 2) }\n"
                       "sequence outer { one inner  when (big == 1) }\n"
                       "sequence main { resize(1, 1)  all outer }\n");
    CHECK(q41.ok);
    CHECK(!has_warning(q41));
}

// Q42 removed from the suite: some(max=0) is a compile error (check 28).
TEST_CASE("sequence Q42 (replaced): some(max=0) of a sequence is rejected") {
    CHECK(compile_result("layers { }\nsequence s { resize(1, 1) }\n"
                         "sequence main { resize(1, 1)  some(max=0) s }\n")
          .has_error("'some(max=0)'"));
}

TEST_CASE("sequence Q43: bounded counts are never warned") {
    compile_result r("layers { }\nsequence grow { upscale(2, 2) }\n"
                     "sequence main {\n    resize(1, 1)\n    one grow\n    some(max=3) grow\n}\n");
    CHECK(r.ok);
    CHECK(!has_warning(r));
}

// ── 7. Embedding API ──────────────────────────────────────────────────────────

TEST_CASE("sequence Q44: statement_count() counts an application as one") {
    auto gen = make(R"(
tag t { a, b }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule flip { g[a] => g[b] }
sequence s {
    all fill
    all flip
    all fill
}
sequence main {
    resize(2, 2)
    some(max=3) s
    all flip
}
)");
    REQUIRE(static_cast<bool>(gen));
    CHECK(gen.statement_count(gen.sequence("main")) == 3);
}

static std::string const q45 = counter(100) + R"(
sequence s {
    all inc
    all inc
}
sequence main {
    resize(1, 1)
    some(max=2) s
}
)";

TEST_CASE("sequence Q45: step_mode::statement steps over leaf statements only") {
    auto t = run_trace(make(q45), 1);
    CHECK(t.index == std::vector<int>{0, 1, 1, 1, 1});
}

TEST_CASE("sequence Q46: stmt_stack() before the first step, and at top level") {
    auto gen = make(q45);
    auto r = gen.run(gen.sequence("main"), 1, ls::step_mode::statement, ls::observe::on);
    CHECK(r.stmt_stack().empty());
    CHECK(r.statement_index() == -1);
    REQUIRE(r.step());
    auto st = r.stmt_stack();
    REQUIRE(st.size() == 1);
    CHECK(st[0].index == 0);
    CHECK(st[0].iteration == 0);
}

TEST_CASE("sequence Q47: stmt_stack() through two levels of nesting") {
    auto t = run_trace(make(q17), 1);
    REQUIRE(t.stacks.size() == 7);   // resize + 3 outer x 2 inner
    int want[6][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0}, {2, 1}};
    for (int k = 0; k < 6; ++k) {
        INFO("step " << k + 1);
        auto const& st = t.stacks[k + 1];
        REQUIRE(st.size() == 3);
        CHECK((st[0].index == 1 && st[0].iteration == 0));
        CHECK((st[1].index == 0 && st[1].iteration == want[k][0]));
        CHECK((st[2].index == 0 && st[2].iteration == want[k][1]));
    }
}

TEST_CASE("sequence Q48: step_mode::application inside a sequence") {
    auto t = run_trace(make(counter(100) + R"(
sequence s { some(max=3, policy=incremental) inc }
sequence main {
    resize(1, 1)
    some(max=2) s
}
)"), 1, ls::step_mode::application);
    // A "working" step did work: an application, or a whole statement
    // (the resize). The boundary step that closes a statement after its
    // applications only reports the boundary.
    int working = 0;
    for (size_t k = 0; k < t.stacks.size(); ++k) {
        bool closes_apps = t.boundary[k] && k > 0 && !t.boundary[k - 1];
        if (closes_apps) continue;
        ++working;
        if (k > 0) CHECK(t.stacks[k].size() == 2);
    }
    CHECK(working == 7);
}

// ── 8. Integration ────────────────────────────────────────────────────────────

static std::string const cave_decls = R"(
tag terrain { wall, floor }
layers { level: grid of terrain }
rule fill {
    level[.]
    =>
    { any
      (weight=45) level[wall]
      (weight=55) level[floor]
    }
}
rule erode {
    level[
        floor floor floor
        floor wall  floor
        floor floor floor ]
    =>
    level[
        * *     *
        * floor *
        * *     * ]
}
rule grow {
    level[
        wall wall  wall
        wall floor wall
        wall wall  wall ]
    =>
    level[
        * *    *
        * wall *
        * *    * ]
}
)";

TEST_CASE("sequence Q49: cellular cave with a fixpoint sequence") {
    auto gen = make(cave_decls + R"(
sequence smooth {
    all erode
    all grow
}
sequence main {
    resize(40, 25)
    all fill
    all smooth
}
)");
    INFO(gen.error());
    REQUIRE(static_cast<bool>(gen));
    int wall = gen.tag("terrain.wall");
    // The same fill on its own: identical draws, so the identical grid.
    auto fill_only = make(cave_decls + "sequence main {\n resize(40, 25)\n all fill\n}\n");
    REQUIRE(static_cast<bool>(fill_only));
    auto isolated = [&](ls::grid g) {
        int n = 0;
        for (int y = 1; y < 24; ++y)
            for (int x = 1; x < 39; ++x) {
                bool v = g.at(x, y) == wall, like = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if ((dx || dy) && (g.at(x + dx, y + dy) == wall) == v) like = true;
                if (!like) ++n;
            }
        return n;
    };
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        INFO("seed " << seed);
        auto t = run_trace(gen, seed);
        // 2 iterations: one that smooths, one stable. A fill with no isolated
        // cell at all (~1.7% of seeds) is stable at once: 1 iteration.
        bool nothing_to_smooth =
            isolated(fill_only.generate(fill_only.sequence("main"), seed)["level"]) == 0;
        CHECK(iterations(t, {2}) == (nothing_to_smooth ? 1 : 2));
        // no isolated cell survives: every interior cell has a like neighbour
        ls::grid g = t.final["level"];
        for (int y = 1; y < 24; ++y)
            for (int x = 1; x < 39; ++x) {
                bool v = g.at(x, y) == wall, like = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if ((dx || dy) && (g.at(x + dx, y + dy) == wall) == v) like = true;
                CHECK(like);
            }
    }
}

TEST_CASE("sequence Q50: an early stop is unobservable when nothing follows") {
    std::string a = cave_decls + R"(
sequence smooth { all erode  all grow }
sequence main {
    resize(40, 25)
    all fill
    some(max=3) smooth
}
)";
    check_equivalent(a, cave_decls + R"(
sequence main {
    resize(40, 25)  all fill  all erode  all grow  all erode  all grow  all erode  all grow
}
)");
    CHECK(iterations(run_trace(make(a), 1), {2}) == 2);
}
