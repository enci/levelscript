// E03: Identifiers cannot start with a digit; lexed as Int then Ident.
// @expect error
// @expect stderr-contains expected layer name
tag t { a }
layers { 9grid: grid of t }
program { resize(1,1) }
