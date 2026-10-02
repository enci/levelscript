// E-op-8 (v0.7): an enum argument outside its set (§7.3 #35) —
// connectivity must be 4 or 8.
// @expect error
// @expect stderr-contains invalid connectivity
tag t { start, goal, route }
layers { g: grid of t }
sequence main { resize(2, 2) path(from=start, to=goal, into=g, write=route, connectivity=6) }
