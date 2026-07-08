// E63: some(max=0) — applies no matches, effectively a no-op.
// @expect error
// @expect stderr-contains some(max=0)
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
program {
    resize(2, 2)
    some(max=0) r
}
