// E13: Rule keyword with no body braces.
// @expect error
// @expect stderr-contains expected '{'
tag t { a }
layers { g: grid of t }
rule r
program { resize(1,1) }
