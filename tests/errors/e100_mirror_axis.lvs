// E100: mirror takes horizontal or vertical — nothing else (section 7.3, check 35 as of v0.7).
// @expect error
// @expect stderr-contains invalid mirror axis
tag t { a }
layers { g: grid of t }
sequence main { resize(2,2) mirror(diagonal) }
