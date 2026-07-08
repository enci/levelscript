// E-op-6 (v0.7): a required operation parameter not supplied (§7.3 #34).
// @expect error
// @expect stderr-contains requires argument 'write='
tag t { start, goal, route }
layers { g: grid of t }
program { resize(2, 2) path(from=start, to=goal, into=g) }
