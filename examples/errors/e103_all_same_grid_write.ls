// E103 (v0.6.4): two items of one { all } may not write the SAME grid at the
// same cell — a write-write conflict (section 7.3, check 33). Use a union '|' to combine.
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
