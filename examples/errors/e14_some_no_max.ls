// E14: 'some(5)' missing the 'max=' / 'percent=' keyword.
// @expect error
// @expect stderr-contains expected 'max=' or 'percent='
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main {
    resize(3, 3)
    some(5) r
}
