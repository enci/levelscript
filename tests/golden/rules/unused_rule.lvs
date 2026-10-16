// @seed 42
// T05: Rules can be declared without being invoked. Grid stays empty.
// @expect grid g count(a) == 0
tag t { a }
layers { g: grid of t }
rule unused { g[.] => g[a] }
sequence main { resize(2, 2) }
