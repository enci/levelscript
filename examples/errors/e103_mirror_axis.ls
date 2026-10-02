// E-mirror-1: mirror axis must be horizontal or vertical.
// @expect error
// @expect stderr-contains invalid mirror axis
tag t { a }
layers { g: grid of t }
sequence main { resize(2,2) mirror(diagonal) }
