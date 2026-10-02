// @seed 42
// Island terrain generator.
// Randomly fills ocean, grows land masses, then classifies coast.
// @expect grid world count(o) >= 100
// @expect grid world count(l) >= 10

tag terrain { ocean, coast, land, highland, peak }
layers { world: grid of terrain }

rule init { world[.] => world[ocean] }

// Seed a few random land cells.
rule seed {
    world[ocean]
    =>
    { any
      (weight=20) world[ocean]
      (weight=1)  world[land]
    }
}

// Grow land into adjacent ocean.
rule grow(symmetry=all, rotation=all) {
    world[ocean land]
    =>
    world[land land]
}

// Erode isolated ocean cells (surrounded by land on all 4 cardinal sides).
rule fill(symmetry=all, rotation=all) {
    world[
        * land *
        land ocean land
        * land * ]
    =>
    world[
        * land *
        land land land
        * land * ]
}

// Classify coast: ocean cell adjacent to land.
// '*' on RHS keeps land out of the write footprint -- all coast
// cells are found in one pass.
rule coast(rotation=all) {
    world[ocean land]
    =>
    world[coast *]
}

// Classify highland: land cell whose 4 cardinal neighbors are all land.
rule highland(symmetry=all, rotation=all) {
    world[
        * land *
        land land land
        * land * ]
    =>
    world[
        * land *
        land highland land
        * land * ]
}

// Peak: highland cell whose 4 cardinal neighbors are all highland or peak.
rule peak(symmetry=all, rotation=all) {
    world[
        * highland *
        highland highland highland
        * highland * ]
    =>
    world[
        * highland *
        highland peak highland
        * highland * ]
}

sequence main {
    resize(80, 40)
    all init
    some(max=30) seed
    some(max=300, policy=incremental) grow
    all fill
    all coast
    all highland
    all peak
}
