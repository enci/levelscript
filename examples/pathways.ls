// @seed 42
// Winding corridor generator.
// A small number of random walkers carve through a wall background.
// 'some' picks one random match per iteration so walkers meander freely.
// @expect grid level count(f) >= 5

tag algo    { head, trail }
tag terrain { wall, floor }
layers {
    a:     grid of algo
    level: grid of terrain
}

rule init_bg { level[.] => level[wall] }

// Seed ~5 walkers on a 60x30 grid (1/360 chance per cell).
rule seed {
    a[.]
    =>
    { any
      (weight=359) a[.]
      (weight=1)   a[head]
    }
}

// Walker moves in a random direction into empty space.
rule step(rotation=all) {
    a[head .]
    =>
    a[trail head]
}

rule finalize { all
    a[trail] => level[floor]
    a[head]  => level[floor]
}

program {
    resize(60, 30)
    all init_bg
    all seed
    some(max=600, policy=incremental) step
    all finalize
}
