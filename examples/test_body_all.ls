// @seed 42
// T31: body-level 'all' combinator applies multiple pairs in one sweep.
// Double-buffering means the second pair (g[b]=>g[c]) never matches —
// the snapshot still holds 'a' when the second pair is evaluated.
// @expect grid g count(a) == 0
// @expect grid g count(b) == 9
// @expect grid g count(c) == 0
tag t { a, b, c }
layers { g: grid of t }
rule seed { g[.] => g[a] }
rule cycle { all
    g[a] => g[b]
    g[b] => g[c]
}
program {
    resize(3, 3)
    all seed
    all cycle
}
