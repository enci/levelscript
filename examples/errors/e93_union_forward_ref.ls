// E-union-2: unions must be acyclic and backward-referencing.
// @expect error
// @expect stderr-contains not a value or earlier union
tag geometry { wall, floor, a = b | wall, b = wall | floor }
layers { level: grid of geometry }
sequence main { resize(1,1) }
