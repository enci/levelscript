// E-derived-2: width/height are not allowed in a derived param.
// @expect error
// @expect stderr-contains cannot be read here
params { center = width / 2 }
layers { g: grid of number }
sequence main { resize(4,4) }
