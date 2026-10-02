// E-default-3: every input param must carry a default (v0.5.3.1).
// @expect error
// @expect stderr-contains must have a default
params { difficulty: number }
layers { g: grid of number }
sequence main { resize(1,1) }
