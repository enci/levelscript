// @expect error
// @expect stderr-contains expected layer name
tag t { a }
layers { $g: grid of t }
program { resize(1,1) }
