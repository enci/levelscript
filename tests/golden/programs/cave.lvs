// @seed 42
// Cellular-automaton cave generator.
// Fill randomly, then erode isolated walls and fill isolated floors.
// @expect grid level count(f) >= 100
// @expect grid level count(w) >= 100

tag terrain { wall, floor }
layers { level: grid of terrain }

rule fill {
    level[.]
    =>
    { any
      (weight=45) level[wall]
      (weight=55) level[floor]
    }
}

// A wall completely surrounded by floor is removed.
rule erode(symmetry=all, rotation=all) {
    level[
        floor floor floor
        floor wall floor
        floor floor floor ]
    =>
    level[
        floor floor floor
        floor floor floor
        floor floor floor ]
}

// A floor completely surrounded by wall is filled in.
rule dilate(symmetry=all, rotation=all) {
    level[
        wall wall wall
        wall floor wall
        wall wall wall ]
    =>
    level[
        wall wall wall
        wall wall wall
        wall wall wall ]
}

sequence main {
    resize(60, 25)
    everywhere fill
    everywhere erode
    everywhere dilate
    everywhere erode
}
