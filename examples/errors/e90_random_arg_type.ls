// E-rand-2: `random` bounds must be numbers.
// @expect error
// @expect stderr-contains 'random' requires number arguments
tag t { a }
layers { g: grid of t }
rule bad { g[.] => g[ (random(a, 2)) ] }
program { resize(1,1) }
