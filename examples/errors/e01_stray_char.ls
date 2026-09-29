// @expect error
// @expect stderr-contains unexpected character '$'
tag t { a }
layers { $g: grid of t }
program { resize(1,1) }
