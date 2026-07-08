// E44: Weight with a non-integer value (2.5) — parser sees 2 then '.'.
// @expect error
// @expect stderr-contains expected ')'
tag t { a, b }
layers { g: grid of t }
rule r {
    g[.]
    =>
    { any
      (weight=2.5) g[a]
      g[b]
    }
}
program { resize(1,1) }
