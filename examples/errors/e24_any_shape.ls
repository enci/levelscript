// E24: alternatives inside { any } have different dimensions.
// @expect error
// @expect stderr-contains dimension mismatch
tag t { a, b, c }
layers { g: grid of t }
rule bad {
    g[a]
    =>
    { any
      g[b]
      g[ b c ]
    }
}
sequence main { resize(2,1) }
