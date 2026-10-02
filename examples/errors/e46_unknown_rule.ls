// @expect error
// @expect stderr-contains undeclared rule
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main {
    resize(2,2)
    all nope
}
