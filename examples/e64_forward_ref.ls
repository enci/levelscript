// @seed 42
// @expect run-ok
tag t { a }
layers { g: grid of t }
program {
    resize(2,2)
    all r
}
rule r { g[.] => g[a] }
