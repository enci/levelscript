// T32: Body-level 'any' combinator — exactly one sub-rule fires per application.
// 'once choose' picks one match from either sub-rule; 8 cells stay 'a'.
// @expect grid g count(a) == 8
// @seed 42
tag t { a, b, c }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule choose { any
    g[a] => g[b]
    g[a] => g[c]
}
sequence main {
    resize(3, 3)
    everywhere fill
    once choose
}
