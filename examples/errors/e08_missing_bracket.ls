// E08: missing closing ']' on pattern — parser finds '=>' instead.
// @expect error
// @expect stderr-contains expected ']'
tag t { a }
layers { g: grid of t }
rule r {
    g[.
    =>
    g[a]
}
program { resize(1,1) }
