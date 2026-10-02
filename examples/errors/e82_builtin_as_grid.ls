// E82: 'min' is a reserved built-in name; cannot name a grid.
// @expect error
// @expect stderr-contains built-in function name
tag t { a }
layers { min: grid of t }
sequence main { resize(1,1) }
