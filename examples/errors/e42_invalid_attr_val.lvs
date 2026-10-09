// E42: invalid value for 'symmetry' attribute.
// @expect error
// @expect stderr-contains invalid value
tag t { a }
layers { g: grid of t }
rule r(symmetry=banana) {
    g[.]
    =>
    g[a]
}
sequence main { resize(1,1) }
