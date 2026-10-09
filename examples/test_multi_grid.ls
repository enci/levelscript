// @seed 42
// Cross-grid write: a single rule fills two grids at once.
// (v0.3: tag value renamed from 'x' — that is now a reserved expression name.)
// @expect grid g count(a) == 9
// @expect grid h count(s) == 9

tag t { a }
tag u { spot }
layers {
    g: grid of t
    h: grid of u
}

rule both {
    g[.]
    =>
    { all
      g[a]
      h[spot]
    }
}

sequence main {
    resize(3, 3)
    everywhere both
}
