// E41: unknown rule attribute name.
// @expect error
// @expect stderr-contains unknown rule attribute 'colour'
tag t { a }
layers { g: grid of t }
rule r(colour=red) {
    g[.]
    =>
    g[a]
}
sequence main { resize(1,1) }
