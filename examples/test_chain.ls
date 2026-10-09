// @seed 42
// Chain of rules: empty → X → Y → Z on a 3x4 grid.
// @expect grid g count(X) == 0
// @expect grid g count(Y) == 0
// @expect grid g count(Z) == 12

tag t { X, Y, Z }
layers { g: grid of t }

rule step1 { g[.] => g[X] }
rule step2 { g[X] => g[Y] }
rule step3 { g[Y] => g[Z] }

sequence main {
    resize(3, 4)
    everywhere step1
    everywhere step2
    everywhere step3
}
