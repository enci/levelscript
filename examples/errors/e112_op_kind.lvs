// E-op-7 (v0.7): an argument whose kind does not match the parameter (section 7.3, check 35).
// @expect error
// @expect stderr-contains must be an integer
tag t { a }
layers { g: grid of t }
sequence main { resize(g, 2) }
