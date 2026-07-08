// E-default-1: a default may not read width/height.
// @expect error
// @expect stderr-contains cannot be read here
params { size: number = width / 2 }
layers { g: grid of number }
program { resize(4,4) }
