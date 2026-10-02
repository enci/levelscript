// E-op-1 (v0.7): an op_call whose name is not in the operation table (section 7.3, check 32).
// @expect error
// @expect stderr-contains unknown operation
tag t { a }
layers { g: grid of t }
sequence main { carve(3, 3) }
