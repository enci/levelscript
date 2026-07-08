// E80: 'abs' of a tag is a type error.
// @expect error
// @expect stderr-contains requires a number
tag t { a }
layers { g: grid of t }
rule bad { where[ (abs(a) > 0) ] => g[a] }
program { resize(1,1) }
