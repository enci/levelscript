// @seed 42
// Scattered-room dungeon generator (T60 variant).
// Seeds are placed randomly; each expands to a 3x3 room.
// The footprint-based overlap check ensures rooms never collide.
// @expect grid level count(f) >= 9

tag algo    { seed, room }
tag terrain { wall, floor }
layers {
    a:     grid of algo
    level: grid of terrain
}

rule init_bg { level[.] => level[wall] }
rule plant   { a[.]     => a[seed] }

// Expand a seed to a 3x3 floor block.
// All 9 write cells are in the footprint, so overlapping expansions are skipped.
rule expand(symmetry=all, rotation=all) {
    a[
        . . .
        . seed .
        . . . ]
    =>
    a[
        room room room
        room seed room
        room room room ]
}

// Mark empty cells touching a room as wall-slots (using * to leave room intact).
// The '*' on the RHS keeps room out of the write footprint, so all border
// cells can be processed in one pass without conflicting.
rule outline(rotation=all) {
    a[. room]
    =>
    a[seed *]
}

rule finalize { all
    a[room] => level[floor]
    a[seed] => level[floor]
}

sequence main {
    resize(60, 30)
    everywhere init_bg
    scatter(15) plant
    everywhere expand
    everywhere finalize
}
