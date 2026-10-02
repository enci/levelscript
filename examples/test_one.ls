// T45: 'one' strategy places exactly one cell regardless of grid size.
// @expect grid g count(a) == 1
// @seed 42
tag t { a }
layers { g: grid of t }
rule seed { g[.] => g[a] }
sequence main { resize(5, 5) one seed }
