// E-op-3 (v0.7): a named-only parameter supplied positionally (§7.3 #33) —
// every parameter of `path` is named.
// @expect error
// @expect stderr-contains named-only
tag t { a }
layers { g: grid of t }
sequence main { resize(2, 2) path(g) }
