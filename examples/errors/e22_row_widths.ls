// @expect error
// @expect stderr-contains inconsistent row widths
tag t { a, b }
layers { g: grid of t }
rule bad {
    g[
        a a a
        a a ]
    =>
    g[
        b b b
        b b ]
}
sequence main { resize(3,2) }
