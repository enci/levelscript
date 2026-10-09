// E63 (0.8): a count written as the literal 0 applies nothing (section 7.3, check 28).
// @expect error
// @expect stderr-contains a count of 0
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main {
    resize(2, 2)
    scatter(0) r
}
