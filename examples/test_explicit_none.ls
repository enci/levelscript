// @seed 42
// T38: 'symmetry=none, rotation=none' is the default — behaves identically
// to omitting the attributes.
// @expect grid g count(b) == 4
tag t { a, b }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule r(symmetry=none, rotation=none) { g[a] => g[b] }
sequence main { resize(2, 2) all fill all r }
