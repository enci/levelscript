// E09: missing ':' between layer name and 'grid'.
// @expect error
// @expect stderr-contains expected ':'
tag t { a }
layers { g grid of t }
program { resize(1,1) }
