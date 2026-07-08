// E57: the reserved character '*' cannot be used as a tag value name.
// @expect error
// @expect stderr-contains expected tag value name
tag t { * }
layers { g: grid of t }
program { resize(1,1) }
