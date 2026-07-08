// @expect error
// @expect stderr-contains undeclared grid
tag t { a }
layers { g: grid of t }
rule bad {
    h[.]
    =>
    h[a]
}
program { resize(1,1) }
