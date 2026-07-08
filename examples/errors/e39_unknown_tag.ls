// @expect error
// @expect stderr-contains undeclared tag
layers { g: grid of unknown }
program { resize(1,1) }
