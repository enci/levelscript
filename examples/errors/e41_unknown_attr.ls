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
program { resize(1,1) }
