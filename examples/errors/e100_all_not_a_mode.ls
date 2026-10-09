// E100: 'all' is a combinator, not a statement mode.
// @expect error
// @expect stderr-contains expected a statement
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) all r }
