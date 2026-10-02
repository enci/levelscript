// Comma between pattern pairs in a rule body is allowed (list_sep accepts ',').
// @seed 42
tag t { a, b, c }
layers { g: grid of t }
rule r { all
    g[a] => g[b], g[b] => g[c]
}
rule seed_a { g[.] => g[a] }
sequence main {
    resize(1, 1)
    all seed_a
    all r
    all r
}
