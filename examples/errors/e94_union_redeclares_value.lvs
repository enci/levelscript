// E-union-3: a union shares the tagset's value namespace.
// @expect error
// @expect stderr-contains redeclares
tag geometry { wall, floor, wall = wall | floor }
layers { level: grid of geometry }
sequence main { resize(1,1) }
