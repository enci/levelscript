// E02: A reserved keyword cannot be used as an identifier (tag name).
// @expect error
// @expect stderr-contains expected '<ident>'
tag program { a }
layers { g: grid of program }
program { resize(1,1) }
