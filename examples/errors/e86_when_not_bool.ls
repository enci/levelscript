// E-when-3: a guard expression must be boolean.
// @expect error
// @expect stderr-contains guard must be a boolean
tag t { a }
params { n: number = 0 }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) all r when (n + 1) }
