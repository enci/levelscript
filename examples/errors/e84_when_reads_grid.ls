// E-when-1: a `when` guard may read params only, not grids.
// @expect error
// @expect stderr-contains cannot be read here
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
program { resize(2,2) all r when (g == a) }
