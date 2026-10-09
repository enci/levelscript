// E-ord-1: `ordered` is body-level only, not a write-side combinator (section 7.3, check 29).
// @expect error
// @expect stderr-contains cannot appear on the write side
tag t { a, b }
layers { g: grid of t }
rule r { g[a] => { ordered g[b] g[a] } }
sequence main { resize(1,1) }
