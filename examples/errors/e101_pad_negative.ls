// E101: a pad margin must be non-negative (§7.3 #36 as of v0.7).
// @expect error
// @expect stderr-contains invalid pad argument
tag t { a }
layers { g: grid of t }
program { resize(2,2) pad(-1) }
