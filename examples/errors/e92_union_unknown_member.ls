// E-union-1: a union member must be a value/union of the same tagset.
// @expect error
// @expect stderr-contains not a value or earlier union
tag geometry { wall, floor, blocker = wall | nope }
layers { level: grid of geometry }
program { resize(1,1) }
