// @expect error
// @expect stderr-contains dimension mismatch
tag t { a, b }
layers { g: grid of t }
rule bad {
    g[a a]
    =>
    g[b b b]
}
program { resize(3,1) }
