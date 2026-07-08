// @seed 42
// E50: a small names-only generator compiles and runs cleanly under v0.2.
// @expect run-ok
// @expect grid g count(w) == 4
tag t { wall, floor }
layers { g: grid of t }
rule fill { g[.] => g[wall] }
program {
    resize(2, 2)
    all fill
}
