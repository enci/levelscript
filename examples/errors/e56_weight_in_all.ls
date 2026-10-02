// @expect error
// @expect stderr-contains only allowed on '{ any }' items
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
sequence main { resize(2,2) }
