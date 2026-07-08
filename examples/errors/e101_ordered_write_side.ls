// E-ord-1: `ordered` is body-level only, not a write-side combinator (§7.3 #29).
// @expect error
// @expect stderr-contains expected 'any' or 'all'
tag t { a, b }
layers { g: grid of t }
rule r { g[a] => { ordered g[b] g[a] } }
program { resize(1,1) }
