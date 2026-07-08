// @seed 42
// T39-T41: weighted { any } biases selection; all values still appear
// over a large enough grid with any seed.
// @expect grid g count(a) >= 50
// @expect grid g count(b) >= 5
tag t { a, b }
layers { g: grid of t }
rule fill {
    g[.]
    =>
    { any
      (weight=3) g[a]
      (weight=1) g[b]
    }
}
program { resize(10, 10) all fill }
