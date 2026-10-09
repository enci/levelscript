// E49: Layer type references a grid name instead of a tagset name.
// @expect error
// @expect stderr-contains undeclared tag
tag t { a }
layers {
    g: grid of t
    h: grid of g
}
sequence main { resize(1,1) }
