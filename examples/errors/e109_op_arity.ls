// E-op-2 (v0.7): more positional arguments than the operation takes (§7.3 #33).
// @expect error
// @expect stderr-contains too many positional arguments
tag t { a }
layers { g: grid of t }
program { resize(2, 2) pad(1, 2) }
