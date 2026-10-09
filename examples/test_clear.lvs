// @seed 42
// T19: '?' on RHS clears a cell (writes empty).
// @expect grid g count(a) == 0
// @expect grid g count(.) == 4
tag t { a }
layers { g: grid of t }
rule fill  { g[.] => g[a] }
rule clear { g[a] => g[.] }
sequence main { resize(2, 2) everywhere fill everywhere clear }
