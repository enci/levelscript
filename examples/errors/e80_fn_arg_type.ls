// E80: 'abs' of a tag is a type error.
// @expect error
// @expect stderr-contains 'abs' requires number arguments
tag t { a }
layers { g: grid of t }
rule bad { where[ (abs(a) > 0) ] => g[a] }
sequence main { resize(1,1) }
