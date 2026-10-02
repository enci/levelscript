// E-derived-1: a derived param may not reference a later one.
// @expect error
// @expect stderr-contains referenced before it is declared
params {
    a = b + 1
    b: number = 0
}
layers { g: grid of number }
sequence main { resize(1,1) }
