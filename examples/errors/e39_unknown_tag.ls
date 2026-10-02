// @expect error
// @expect stderr-contains undeclared tag
layers { g: grid of unknown }
sequence main { resize(1,1) }
