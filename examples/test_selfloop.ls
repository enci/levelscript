// @seed 42
// T22: A rule that writes the same value it matched terminates under 'all'
// (single sweep, not iterated to stable). Grid stays unchanged.
// @expect grid g count(a) == 9
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
rule loop { g[a] => g[a] }
program {
    resize(3, 3)
    all fill
    all loop
    all loop
}
