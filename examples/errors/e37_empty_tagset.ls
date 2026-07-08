// @expect error
// @expect stderr-contains has no values
tag t { }
layers { g: grid of t }
program { resize(1,1) }
