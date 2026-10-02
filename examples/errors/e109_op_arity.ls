// E-op-2 (v0.7): more positional arguments than the operation takes (section 7.3, check 33).
// @expect error
// @expect stderr-contains too many positional arguments
tag t { a }
layers { g: grid of t }
sequence main { resize(2, 2) pad(1, 2) }
