// E10: Missing 'of' keyword in layer entry.
// @expect error
// @expect stderr-contains expected 'of'
tag t { a }
layers { g: grid t }
program { resize(1,1) }
