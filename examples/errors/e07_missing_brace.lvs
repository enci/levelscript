// E07: missing closing '}' on rule body.
// @expect error
// @expect stderr-contains expected '}'
tag t { a }
layers { g: grid of t }
rule r {
    g[.]
    =>
    g[a]
sequence main { resize(1,1) }
