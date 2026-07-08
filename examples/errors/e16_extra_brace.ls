// E16: Extra closing '}' at top level.
// @expect error
// @expect stderr-contains unexpected token '}'
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
}
program { resize(1,1) }
