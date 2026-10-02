// @seed 42
// Fill all cells of a 5x5 grid then clear them.
// @expect grid g count(a) == 0
// @expect grid g count(b) == 25

tag t { a, b }
layers { g: grid of t }

rule fill  { g[.] => g[a] }
rule clear { g[a] => g[b] }

sequence main {
    resize(5, 5)
    all fill
    all clear
}
