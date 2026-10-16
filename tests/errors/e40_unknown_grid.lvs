// @expect error
// @expect stderr-contains undeclared grid
tag t { a }
layers { g: grid of t }
rule bad {
    h[.]
    =>
    h[a]
}
sequence main { resize(1,1) }
