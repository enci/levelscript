// @expect error
// @expect stderr-contains duplicate tag
tag t { a, b }
tag t { c, d }
layers { g: grid of t }
program { resize(1,1) }
