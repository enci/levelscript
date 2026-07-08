// @expect error
// @expect stderr-contains not allowed
tag t { a }
tag u { x }
layers {
    g: grid of t
    h: grid of u
}
rule bad {
    g[.]
    =>
    { all
      (weight=2) g[a]
      h[x]
    }
}
program { resize(2,2) }
