// E14 (0.8): 'scatter' requires a count.
// @expect error
// @expect stderr-contains 'scatter' takes a count
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main {
    resize(3, 3)
    scatter r
}
