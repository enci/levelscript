// @expect error
// @expect stderr-contains unexpected character '$'
tag t { a }
layers { $g: grid of t }
sequence main { resize(1,1) }
