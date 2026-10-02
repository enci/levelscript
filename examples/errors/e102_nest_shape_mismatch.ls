// E102 (v0.6.4): the shape check is recursive — a leaf deep in the write tree
// must still match the LHS footprint dimensions (§5.4).
// @expect error
// @expect stderr-contains dimension mismatch
tag t { a, b }
layers { g: grid of t  h: grid of t }
rule bad {
    g[a a]
    =>
    { all
      g[b b]
      { any h[a] h[a a a] }
    }
}
sequence main { resize(3,1) }
