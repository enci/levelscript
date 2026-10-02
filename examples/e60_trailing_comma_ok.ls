// @seed 42
// E60: trailing comma in tagset is allowed per grammar.
// @expect run-ok
// @expect grid g count(a) == 4
tag t { a, b, c, }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence main { resize(2, 2) all fill }
