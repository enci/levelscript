// E-nest-2: two writes to one grid at the same cell inside { all }.
// @expect error
// @expect stderr-contains same-grid simultaneous write
tag geometry { wall, floor }
layers { level: grid of geometry }
rule bad {
    level[.]
    =>
    { all level[wall] level[floor] }
}
sequence main { resize(1,1) }
