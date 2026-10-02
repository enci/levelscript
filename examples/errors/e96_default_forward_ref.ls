// E-default-2: defaults must be acyclic and backward-referencing.
// @expect error
// @expect stderr-contains referenced before it is declared
params {
    a: number = b + 1
    b: number = 2
}
layers { g: grid of number }
sequence main { resize(1,1) }
