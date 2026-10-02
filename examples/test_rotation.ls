// @seed 42
// T36: rotation=all expands a 1x2 pattern to 4 variants (all 4 cardinal directions).
// On a 4x4 all-S grid, `all` applies matches in a seeded-shuffle order (section 6.5),
// firing horizontal and vertical variants; conflicting overlaps are skipped.
// The exact S/F split depends on the seed, but each fired pair writes two F's
// and the grid never keeps an empty cell.
// @expect grid g count(.) == 0
// @expect grid g count(F) >= 2
tag t { S, F }
layers { g: grid of t }
rule fill { g[.] => g[S] }
rule pair(rotation=all) { g[ S S ] => g[ F F ] }
sequence main {
    resize(4, 4)
    all fill
    all pair
}
