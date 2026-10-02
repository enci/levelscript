// @seed 42
// @expect run-ok
tag t { a }
layers { g: grid of t }
sequence main {
    resize(2,2)
    all r
}
rule r { g[.] => g[a] }
