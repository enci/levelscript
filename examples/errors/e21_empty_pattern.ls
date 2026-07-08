// @expect error
// @expect stderr-contains empty pattern
tag t { a }
layers { g: grid of t }
rule bad { g[] => g[a] }
program { resize(1,1) }
