// E-rand-3: `random` is a reserved built-in name.
// @expect error
// @expect stderr-contains built-in function name
tag t { a }
layers { random: grid of t }
sequence main { resize(1,1) }
