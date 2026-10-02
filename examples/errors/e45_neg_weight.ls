// E45: Negative weight — '-' is a glyph token, not part of an integer.
// @expect error
// @expect stderr-contains expected
tag t { a, b }
layers { g: grid of t }
rule r {
    g[.]
    =>
    { any
      (weight=-3) g[a]
      g[b]
    }
}
sequence main { resize(1,1) }
