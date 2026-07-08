// E43: Typo in attribute name ('symetry' instead of 'symmetry').
// @expect error
// @expect stderr-contains unknown attribute
tag t { a }
layers { g: grid of t }
rule r(symetry=all) {
    g[.]
    =>
    g[a]
}
program { resize(1,1) }
