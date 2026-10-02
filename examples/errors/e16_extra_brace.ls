// E16: Extra closing '}' at top level.
// @expect error
// @expect stderr-contains expected a declaration
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
}
sequence main { resize(1,1) }
