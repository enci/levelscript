// E41: unknown rule attribute name.
// @expect error
// @expect stderr-contains unknown attribute
tag t { a }
layers { g: grid of t }
rule r(colour=red) {
    g[.]
    =>
    g[a]
}
program { resize(1,1) }
