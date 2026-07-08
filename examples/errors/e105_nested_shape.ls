// E-nest-1: a leaf pattern deep in the write tree has the wrong dimensions.
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
program { resize(3,1) }
