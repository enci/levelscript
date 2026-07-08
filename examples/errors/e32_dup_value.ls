// @expect error
// @expect stderr-contains duplicate tag value
tag t { a, b, a }
layers { g: grid of t }
program { resize(1,1) }
