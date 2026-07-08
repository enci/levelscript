// @expect error
// @expect stderr-contains expected
tag t { a }
layers { g: grid of t }
rule r {
    g[.]
    g[a]
}
program { resize(1,1) }
