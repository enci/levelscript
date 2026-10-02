// E101: a pad margin must be non-negative (section 7.3, check 36 as of v0.7).
// @expect error
// @expect stderr-contains invalid pad argument
tag t { a }
layers { g: grid of t }
sequence main { resize(2,2) pad(-1) }
