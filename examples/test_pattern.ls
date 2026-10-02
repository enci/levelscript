// @seed 42
// 1x2 pattern: every pair [S S] becomes [F F].
// On a 1x6 row of S, `all` applies matches in a seeded-shuffle order (section 6.5);
// which non-overlapping pairs win varies, but the row is fully covered and
// every fired pair produces two F's. So: no empties, and F is a positive
// even count.
// @expect grid g count(.) == 0
// @expect grid g count(F) >= 2

tag t { S, F }
layers { g: grid of t }

rule fill { g[.] => g[S] }
rule pair { g[ S S ] => g[ F F ] }

sequence main {
    resize(6, 1)
    all fill
    all pair
}
