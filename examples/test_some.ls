// T46: 'some(max=3)' applies at most 3 times (iterated).
// @expect grid g count(a) == 3
// @seed 42
tag t { a }
layers { g: grid of t }
rule seed { g[.] => g[a] }
sequence main { resize(5, 5) some(max=3) seed }
