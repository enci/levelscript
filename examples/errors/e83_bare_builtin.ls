// E83: a built-in name not followed by '(' does not resolve.
// @expect error
// @expect stderr-contains unknown identifier
tag t { a }
layers { g: grid of t }
rule bad { where[ (abs > 0) ] => g[a] }
program { resize(1,1) }
