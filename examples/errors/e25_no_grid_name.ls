// E25: Pattern body without a preceding grid identifier.
// @expect error
// @expect stderr-contains expected a grid name
tag t { a }
layers { g: grid of t }
rule r {
    [a]
    =>
    g[a]
}
program { resize(1,1) }
