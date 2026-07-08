// E-pad-1: pad margin must be non-negative.
// @expect error
// @expect stderr-contains invalid pad argument
tag t { a }
layers { g: grid of t }
program { resize(2,2) pad(-1) }
